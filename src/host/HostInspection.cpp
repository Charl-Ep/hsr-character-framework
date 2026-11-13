#include <hksr/framework/host/FrameworkHost.hpp>
#include <stdexcept>
#include <type_traits>
#include <sstream>
#include <algorithm>
#include <set>

namespace hksr::framework {
void FrameworkHost::ReadControllerActions(const api::WorldFrame& frame) {
    if (!coordinator_->Catalog())
        return;
    const auto id = controller_query_.value_or(observed_character_.load());
    auto clear_checks = [&](std::uint64_t actor) {
        if (const auto old = controller_actions_.find(actor);
            old != controller_actions_.end() && old->second->checked_domain) {
            auto out = std::make_shared<CharacterControllerActions>(*old->second);
            out->checked_domain.reset();
            out->checked_states.clear();
            old->second = std::move(out);
        }
    };
    if (checked_character_ != id) {
        clear_checks(checked_character_);
        checked_character_ = id;
    }
    if (!id)
        return;
    const auto* session = characters_->Active(id, frame);
    if (controller_query_ && !session) {
        controller_query_.reset();
        controller_catalog_error_ = "requested controller owner is not active";
        return;
    }
    const auto* handoff = session ? session->Handoff() : nullptr;
    if (!session || session->Status().phase != SessionPhase::Active || !handoff ||
        handoff->Status().phase != HandoffPhase::Ready || !handoff->Status().ready_domain) {
        clear_checks(id);
        controller_stamps_.erase(id);
        return;
    }
    const auto creation = characters_->Status(id).creations;
    const auto domain = *handoff->Status().ready_domain;
    try {
        const auto& npc = session->Character().Creation();
        api::EntityToken owner;
        for (const auto& receipt : npc.entities)
            if (receipt.token.entity == npc.factory.entity && !receipt.disposed)
                owner = receipt.token;
        auto alive = [&] {
            return owner.entity && !api::TrackedEntities().IsDisposed(owner) &&
                   api::TrackedEntities().Current(owner.entity).generation == owner.generation;
        };
        if (npc.removal_requested || !alive()) {
            clear_checks(id);
            return;
        }
        const auto model = services_->ReadAnimator(owner);
        if (!model) {
            clear_checks(id);
            return;
        }
        if (model_observer_) {
            try {
                model_observer_(id, creation, domain, owner.entity, model->component, model->animator);
            } catch (const std::exception& e) {
                Trace(std::string("model observer: ") + e.what());
            } catch (...) {
                Trace("model observer: unknown observation failure");
            }
            if (!alive())
                return;
        }
        const ControllerStamp current{creation, domain, model->animator, model->controller};
        const auto cached = controller_actions_.find(id);
        const auto old_stamp = checked_controller_stamps_.find(id);
        const bool replaced = old_stamp == checked_controller_stamps_.end() || old_stamp->second.creation != creation ||
                              old_stamp->second.domain != domain || old_stamp->second.animator != current.animator ||
                              old_stamp->second.controller != current.controller ||
                              cached == controller_actions_.end() || cached->second->checked_domain != domain;
        std::shared_ptr<CharacterControllerActions> checked;
        if (replaced) {
            checked = cached == controller_actions_.end()
                          ? std::make_shared<CharacterControllerActions>()
                          : std::make_shared<CharacterControllerActions>(*cached->second);
            if (checked->creation != creation)
                checked->domains.clear();
            checked->creation = creation;
            checked->checked_domain = domain;
            checked->checked_model_epoch = ++checked_controller_epoch_;
            checked->checked_states.clear();
            checked_controller_stamps_.insert_or_assign(id, current);
        }
        // Only source-referenced legacy body states are checked. One actor,
        // at most three Hash/HasState pairs per frame, once per actual model.
        std::set<std::string> names;
        const auto catalog = coordinator_->Catalog()->Snapshot();
        if (const auto* actions = catalog ? catalog->Actions(id) : nullptr;
            actions && domain == ActionDomain::Battle) {
            for (const auto& visual : actions->actions.visual_states)
                for (const auto* part : {&visual.value.occur, &visual.value.loop, &visual.value.recover})
                    if (part->resolved_state.available && !part->resolved_state.value.empty())
                        names.insert(part->resolved_state.value);
            for (const auto& row : actions->death_names)
                for (auto member : {&api::VisualReactionInput::die, &api::VisualReactionInput::die_hit,
                                    &api::VisualReactionInput::die_fly, &api::VisualReactionInput::die_limbo})
                    if ((row.second.*member).available && !(row.second.*member).value.empty())
                        names.insert((row.second.*member).value);
        }
        unsigned budget = 3;
        for (const auto& name : names) {
            const auto key = std::make_pair(0, name);
            if ((checked ? checked->checked_states : cached->second->checked_states).count(key))
                continue;
            if (!budget)
                break;
            --budget;
            if (!checked)
                checked = std::make_shared<CharacterControllerActions>(*cached->second);
            api::Observed<bool> value;
            try {
                value = handoff->InspectState(model->animator, name, 0, *objects_);
            } catch (const std::exception& e) {
                Trace(std::string("directory state read: ") + e.what());
            }
            if (!alive())
                return;
            checked->checked_states.emplace(key, value);
        }
        if (checked)
            controller_actions_[id] = std::move(checked);
        if (!controller_query_)
            return; // selecting/creating a character is not a request to enumerate states
        controller_query_.reset();
        if (!animator_catalog_) {
            if (!catalog_metadata_)
                throw std::runtime_error("controller catalog metadata unavailable");
            animator_catalog_ = Measure("catalog.controller-init",
                                        [&] { return std::make_unique<api::AnimatorCatalog>(*catalog_metadata_); });
        }
        const ControllerStamp stamp{creation, domain, model->animator, model->controller};
        const auto old = controller_stamps_.find(id);
        if (old != controller_stamps_.end() && old->second.creation == creation && old->second.domain == domain &&
            old->second.animator == stamp.animator && old->second.controller == stamp.controller)
            return;
        auto values = Measure("catalog.controller", [&] { return animator_catalog_->Read(model->animator); });
        if (const auto pending = animator_catalog_->UnresolvedReferences())
            Trace("controller catalog cleanup unresolved_references=" + std::to_string(pending));
        if (!alive())
            return;
        controller_stamps_.insert_or_assign(id, stamp); // one read per ready controller, not per-frame failure retry
        auto out = std::make_shared<CharacterControllerActions>();
        const auto prior = controller_actions_.find(id);
        if (prior != controller_actions_.end() && prior->second->creation == creation)
            *out = *prior->second;
        out->creation = creation;
        Trace("controller catalog character=" + std::to_string(id) + " domain=" +
              std::to_string(static_cast<int>(domain)) + " layers=" + std::to_string(values.layers.size()) +
              " states=" + std::to_string(values.states.size()) + " complete=" + std::to_string(values.complete));
        for (const auto& row : values.states)
            Trace("controller state character=" + std::to_string(id) + " layer=" + std::to_string(row.layer) +
                  " machine=" + std::to_string(row.machine) + " index=" + std::to_string(row.index) +
                  " name=" + row.name + " path=" + row.path + " full=" + row.full_path +
                  " hash=" + std::to_string(row.full_path_hash) + " loop=" + std::to_string(row.loop) + " accepted=" +
                  std::to_string(row.recognized) + " name_roundtrip=" + std::to_string(row.name_roundtrip));
        for (const auto& issue : values.issues)
            Trace("controller catalog issue: " + issue);
        out->domains.insert_or_assign(domain, std::move(values));
        controller_actions_[id] = std::move(out);
    } catch (const std::exception& e) {
        clear_checks(id);
        auto& previous = controller_read_errors_[id];
        if (previous != e.what()) {
            previous = e.what();
            Trace("controller catalog read: " + previous);
        }
    }
}
void FrameworkHost::ReadNpcActions(std::uint64_t id, const api::WorldFrame& frame) {
    auto& browser = Browser();
    const auto* session = characters_->Active(id, frame);
    if (!session)
        throw std::runtime_error("selected character has no active instance in this world");
    const auto& npc = session->Character().Creation();
    api::EntityToken owner;
    for (const auto& receipt : npc.entities)
        if (receipt.token.entity == npc.factory.entity && !receipt.disposed)
            owner = receipt.token;
    auto alive = [&] {
        return owner.entity && !api::TrackedEntities().IsDisposed(owner) &&
               api::TrackedEntities().Current(owner.entity).generation == owner.generation;
    };
    if (npc.removal_requested || !alive())
        throw std::runtime_error("selected character is retiring");
    const auto component = services_->NpcMotionComponent(owner, frame.world);
    auto out = std::make_shared<CharacterNpcActions>();
    out->creation = characters_->Status(id).creations;
    out->source = browser.NpcMotions(component);
    npc_actions_[id] = std::move(out);
}
}
