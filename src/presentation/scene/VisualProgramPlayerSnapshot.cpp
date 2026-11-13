#include <hksr/framework/presentation/scene/VisualProgramPlayer.hpp>
#include <algorithm>
#include <stdexcept>

namespace hksr::framework {
VisualProgramSnapshot VisualProgramPlayer::Capture() const {
    if (status_.closing || end_pending_)
        return {program_->id, {}, {}};
    if (restore_)
        return restore_->value;
    VisualProgramSnapshot out;
    out.program = program_->id;
    auto capture = [&](const auto& effects, const auto& choices, const auto& variables, const std::string& name) {
        VisualContextSnapshot value;
        value.name = name;
        value.variables = variables;
        if (services_.capture_inputs)
            value.inputs = services_.capture_inputs(*effects);
        value.inputs.choices = choices;
        return value;
    };
    if (context_) {
        if (!program_->restore)
            throw std::runtime_error("visual scene has no restore recipe: " + program_->id);
        out.scene = capture(context_, choices_, variables_, {});
    }
    std::vector<std::shared_ptr<Lifetime>> ordered;
    for (const auto& [name, lifetime] : contexts_)
        ordered.push_back(lifetime);
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a->token < b->token; });
    for (const auto& lifetime : ordered) {
        if (lifetime->participants && !lifetime->participants())
            continue;
        if (lifetime->phase != ContextPhase::Active)
            throw std::runtime_error("visual context is changing: " + lifetime->name);
        if (!program_->contexts.at(lifetime->name).restore)
            throw std::runtime_error("visual context has no restore recipe: " + lifetime->name);
        VisualContextSnapshot saved;
        if (lifetime->state_only) {
            saved.name = lifetime->name;
            saved.variables = lifetime->variables;
            saved.inputs = lifetime->inputs;
        } else
            saved = capture(lifetime->effects, lifetime->choices, lifetime->variables, lifetime->name);
        if (!services_.capture_inputs || lifetime->state_only)
            saved.inputs = lifetime->inputs;
        saved.inputs.choices = lifetime->choices;
        saved.instance = lifetime->key.instance;
        saved.binding = lifetime->binding;
        saved.numbers = lifetime->numbers;
        saved.values = lifetime->values;
        saved.effects = lifetime->effects->CaptureEffects(program_->contexts.at(lifetime->name).restore_effects);
        out.contexts.push_back(std::move(saved));
    }
    return out;
}
void VisualProgramPlayer::Restore(VisualProgramSnapshot value) {
    if (status_.closing || context_ || !contexts_.empty() || restore_ || pending_ || action_)
        throw std::logic_error("visual restoration requires an unentered scene");
    ValidateVisualRestore(*program_, value);
    restore_ = Restoration{std::move(value), false, {}};
}
bool VisualProgramPlayer::ServiceRestore() {
    if (!restore_)
        return true;
    if (!status_.issues.empty())
        return false;
    // Native providers may queue Select/Leave/End. The restoration value and
    // cursor must outlive a reentrant cancellation; never dereference reset state.
    const auto snapshot = restore_->value;
    const auto intent = next_;
    auto ready = [&](const VisualContextSnapshot& saved) {
        if (state_contexts_.count(saved.name))
            return true;
        if (!services_.restore_inputs_ready)
            return true;
        auto inputs = saved.inputs;
        if (saved.binding) {
            inputs.targets["@context.receiver"] = saved.binding->receiver;
            inputs.targets["@context.caster"] = saved.binding->caster;
        }
        const auto result = services_.restore_inputs_ready(inputs);
        if (result.state == VisualStepState::Failed)
            throw std::runtime_error(result.detail);
        if (result.state == VisualStepState::Pending)
            status_.waiting.push_back(result.detail);
        return result.state == VisualStepState::Done;
    };
    bool done = true;
    // Reserve all phase owners in original order before any resource waits.
    // Otherwise a slower early context could incorrectly override a later one.
    for (const auto& saved : snapshot.contexts)
        if (!contexts_.count({saved.name, saved.instance})) {
            try {
                StartContext({saved.name, saved.instance}, &saved);
            } catch (const std::exception& e) {
                status_.issues.push_back(std::string("visual restore: ") + e.what());
                done = false;
            }
            if (!restore_ || status_.closing || end_pending_)
                return false;
        }
    if (snapshot.scene && !restore_->scene_started) {
        try {
            if (ready(*snapshot.scene)) {
                if (!restore_ || status_.closing || end_pending_)
                    return false;
                restore_->scene_started = true;
                context_ = std::make_shared<VisualEffectContext>(services_.effects);
                variables_ = snapshot.scene->variables;
                choices_ = snapshot.scene->inputs.choices;
                if (services_.prepare_inputs)
                    services_.prepare_inputs(context_, snapshot.scene->inputs, true, false);
                if (!restore_ || status_.closing || end_pending_)
                    return false;
                if (services_.attach)
                    services_.attach(context_);
                if (!restore_ || status_.closing || end_pending_)
                    return false;
            } else
                done = false;
        } catch (const std::exception& e) {
            status_.issues.push_back(std::string("visual restore: ") + e.what());
            done = false;
        }
    }
    if (context_)
        context_->Service();
    if (!restore_ || status_.closing || end_pending_)
        return false;
    for (const auto& saved : snapshot.contexts) {
        const auto found = contexts_.find({saved.name, saved.instance});
        if (found == contexts_.end()) {
            done = false;
            continue;
        }
        auto lifetime = found->second;
        try {
            if (lifetime->phase != ContextPhase::Active && ready(saved))
                DriveContext(lifetime);
        } catch (const std::exception& e) {
            status_.issues.push_back(saved.name + ": " + e.what());
        }
        if (!restore_ || status_.closing || end_pending_)
            return false;
    }
    if (!restore_ || next_ != intent)
        return false;
    for (const auto& [name, lifetime] : contexts_)
        done = done && lifetime->phase == ContextPhase::Active;
    if (snapshot.scene && restore_->scene_started) {
        auto cursor = restore_->scene;
        done = Instructions(*program_->restore, cursor, {intent, {}}) && done;
        if (!restore_)
            return false;
        restore_->scene = std::move(cursor);
    }
    if (!done || !status_.issues.empty() || next_ != intent)
        return false;
    if (snapshot.scene)
        restored_inputs_ = snapshot.scene->inputs;
    restore_.reset();
    return true;
}
}
