#include <hksr/framework/character/CharacterInstance.hpp>
#include <hksr/model/ModelAccess.hpp>
#include <stdexcept>
#include <algorithm>
#include "SkinEventPreparation.hpp"

namespace hksr::framework {
CharacterInstance::CharacterInstance(api::GameWorld* world, const CharacterPackage& package,
                                     std::shared_ptr<const ModelAssetInput> payload, std::string look_source,
                                     CharacterMethods methods, api::PresentationContext::EntityEventFactory events,
                                     VisualRoot root, std::shared_ptr<api::CharacterSkin> skin)
    : world_(world), skin_(std::move(skin)), configuration_factory_(std::move(methods.configuration)),
      events_ready_(std::move(methods.events_ready)), models_(std::move(methods.models)),
      entities_(models_->Entities()), npc_(skin_ ? methods.create_skin(skin_) : methods.create()) {
    if (!world_)
        throw std::invalid_argument("character creation epoch requires its owner world");
    if (!npc_)
        throw std::invalid_argument("character creation factory returned no session");
    avatar_id_ = package.avatar_id;
    outfit_factory_ = std::move(methods.outfit);
    if (package.source == SourceKind::External && (!package.appearance || !payload))
        throw std::invalid_argument("external source requires its appearance and preloaded payload");
    if (package.source == SourceKind::Official && payload)
        throw std::invalid_argument("official source does not consume an external payload");
    if (payload) {
        alternate_ = std::make_unique<NpcModels>(*npc_, world_, payload, models_);
        alternate_->SetEnabled(false);
    }
    if (package.construction == ConstructionKind::StoryActor) {
        if (!package.appearance || package.source != SourceKind::Official || payload)
            throw std::invalid_argument("independent story actor requires an official model source");
        direct_config_ = package.appearance->character_config;
        direct_roots_ = models_->CreateRoots();
        if (events && !skin_)
            events_ = events(*npc_, world_);
    } else if (package.appearance) {
        appearance_ = std::make_unique<NpcAppearance>(*npc_, world_, *package.appearance, std::move(payload), root,
                                                      std::move(look_source), models_);
        if (events)
            events_ = events(*npc_, world_);
        else
            status_.errors.push_back("appearance event supplementation service not supplied");
    }
}
bool CharacterInstance::PrepareSkinBaseline(const api::WorldFrame& frame, AppearanceServices appearance) {
    if (!skin_ || skin_events_ready_)
        return true;
    if (frame.world != world_ || !frame.disposing.read.returned || frame.disposing.value)
        return false;
    const auto& creation = npc_->Status();
    api::EntityToken token{};
    for (const auto& entity : creation.entities)
        if (entity.token.entity == creation.factory.entity && !entity.disposed)
            token = entity.token;
    if (!token.entity || creation.removal_requested || creation.closed)
        return false;
    if (!configuration_factory_ || !events_ready_)
        throw std::runtime_error("skin event configuration service unavailable");
    if (!skin_model_type_) {
        const auto type = models_->ComponentType(appearance.assets.objects);
        if (!type.call.returned || !type.object)
            throw std::runtime_error("skin baseline model Type unavailable");
        if (!direct_roots_)
            direct_roots_ = models_->CreateRoots();
        direct_handles_.push_back(0);
        direct_handles_.back() = direct_roots_->Keep(type.object);
        if (!direct_roots_->Status(direct_handles_.back()).ready)
            throw std::runtime_error("skin baseline model Type retention failed");
        skin_model_type_ = type.object;
    }
    const auto model = entities_.Component(token.entity, skin_model_type_);
    if (!model.call.returned)
        throw std::runtime_error("skin baseline model component unreadable");
    bool ready = false;
    if (model.object) {
        const auto complete =
            api::ModelLoadCompleted(models_->Models().Read(static_cast<api::CharacterModelComponent*>(model.object)));
        if (!complete.read.returned)
            throw std::runtime_error("skin baseline model load state unreadable");
        ready = complete.value;
    }
    if (!detail::PrepareSkinEventInputs(
            skin_configuration_, [&] { return ready && events_ready_(token, world_); },
            skin_->Status().adventure_event_files, [this, token] { return configuration_factory_(token, world_); }))
        return false;
    skin_events_ready_ = true;
    status_.event_preparation_attempted = true;
    return true;
}
void CharacterInstance::Create(const api::NpcCreateInput& input) {
    if (input.world != world_)
        throw std::invalid_argument("character creation world differs from its owner");
    npc_->Create(input);
}
void CharacterInstance::Create(const api::StoryCreateInput& input) {
    if (input.world != world_)
        throw std::invalid_argument("character creation world differs from its owner");
    npc_->Create(input);
}
void CharacterInstance::Remove() {
    npc_->Remove();
    if (events_)
        events_->RequestClose();
    if (appearance_)
        appearance_->RequestClose();
    if (alternate_)
        alternate_->RequestClose();
}
void CharacterInstance::SetExplorationInputsEnabled(bool enabled) {
    status_.exploration_inputs_enabled = enabled;
    if (enabled) {
        status_.alternate_payload_enabled = false;
        if (alternate_)
            alternate_->SetEnabled(false);
    }
    if (appearance_)
        appearance_->SetEnabled(enabled && AlternatePayloadDetached());
    // Re-enable only AFTER the private Art has actually been adopted below.
    if (events_)
        events_->SetEnabled(false);
}
void CharacterInstance::SetAlternatePayloadEnabled(bool enabled) {
    status_.alternate_payload_enabled = enabled;
    if (enabled)
        SetExplorationInputsEnabled(false);
    if (alternate_)
        alternate_->SetEnabled(enabled && ExplorationInputsDetached());
}
void CharacterInstance::SetExplorationEventsEnabled(bool enabled) {
    status_.exploration_events_enabled = enabled;
    if (!enabled && events_)
        events_->SetEnabled(false);
}
bool CharacterInstance::AlternatePayloadDetached() const {
    return !alternate_ || alternate_->InputsDetached();
}
bool CharacterInstance::ExplorationInputsDetached() const {
    return (!appearance_ || appearance_->InputsDetached()) && (!events_ || events_->InputsDetached());
}
void CharacterInstance::Service(const api::WorldFrame& frame, AppearanceServices services,
                                api::AnimationInputTypes types) {
    if (status_.closed)
        return;
    const auto& npc = npc_->Status();
    api::EntityToken owner{};
    for (const auto& receipt : npc.entities)
        if (receipt.token.entity == npc.factory.entity)
            owner = receipt.token;
    const bool gone =
        npc.closed || (owner.entity && (api::TrackedEntities().IsDisposed(owner) ||
                                        api::TrackedEntities().Current(owner.entity).generation != owner.generation));
    const bool own_frame = frame.world == world_ && frame.disposing.read.returned && !frame.disposing.value;
    if (outfit_ && (gone || (own_frame && npc.removal_requested)))
        outfit_->Close(!gone);
    if (!outfit_attempted_ && avatar_id_ && outfit_factory_ && owner.entity && !gone && !npc.removal_requested &&
        own_frame) {
        const auto current = entities_.Read(owner.entity);
        if (current.loaded.read.returned && current.loaded.value && current.owner_world.read.returned &&
            current.owner_world.value == world_) {
            outfit_attempted_ = true;
            try {
                outfit_ = outfit_factory_();
                outfit_->Prepare(owner.entity, avatar_id_);
                for (const auto& error : outfit_->Status().errors)
                    status_.errors.push_back("outfit: " + error);
            } catch (const std::exception& e) {
                status_.errors.push_back(std::string("outfit: ") + e.what());
            }
        }
    }
    // Each subsystem makes its own ownership/argument checks. An unavailable
    // event file does not suppress geometry or prevent official NPC disposal.
    // Retirement calls run before MakeEntityDie; native/GC draining continues
    // afterward, including later worlds without old-world method calls.
    if (events_)
        events_->Service(frame);
    if (appearance_)
        appearance_->SetEnabled(status_.exploration_inputs_enabled && AlternatePayloadDetached());
    if (appearance_)
        appearance_->Service(frame, services);
    if (alternate_) {
        alternate_->SetEnabled(status_.alternate_payload_enabled && ExplorationInputsDetached());
        alternate_->Service(frame, services.assets, services.scene);
    }
    if (events_ && status_.exploration_inputs_enabled && status_.exploration_events_enabled &&
        !npc_->Status().removal_requested && frame.world == world_ && frame.disposing.read.returned &&
        !frame.disposing.value) {
        const auto* visual = appearance_ ? appearance_->Active() : nullptr;
        if (!direct_config_.empty()) {
            if (!status_.event_preparation_attempted) {
                status_.event_preparation_attempted = true;
                try {
                    auto keep = [&](api::ObjectResult result) {
                        if (!result.call.returned || !result.object)
                            throw std::runtime_error("independent event input unavailable");
                        direct_handles_.push_back(0);
                        direct_handles_.back() = direct_roots_->Keep(result.object);
                        if (!direct_roots_->Status(direct_handles_.back()).ready)
                            throw std::runtime_error("independent event input retention failed");
                        return result.object;
                    };
                    auto* path = keep(services.assets.objects.NewStringUtf8(direct_config_.c_str()));
                    auto* config = keep(services.config.Load(path));
                    events_->Prepare(types, services.config.Read(config).event_paths);
                } catch (const std::exception& e) {
                    status_.errors.push_back(e.what());
                }
            }
            if (events_->Status().prepared) {
                events_->SetEnabled(true);
                events_->Service(frame);
            }
        }
        if (visual && visual->Slot().adopted && visual->Slot().supply.returned && visual->Inputs().Character().ready) {
            if (!status_.event_preparation_attempted) {
                status_.event_preparation_attempted = true;
                events_->Prepare(types, visual->Inputs().Character().data.event_paths);
            }
            events_->SetEnabled(true);
            events_->Service(frame);
        }
    }
    npc_->Service(frame);
    if (outfit_ && npc_->Status().closed)
        outfit_->Close(false);
    const bool skin_configuration_closed =
        !skin_configuration_ || !npc_->Status().closed || skin_configuration_->Close();
    bool direct_closed = true;
    if (npc_->Status().closed && direct_roots_)
        for (auto id : direct_handles_)
            direct_closed = direct_roots_->Release(id).complete && direct_closed;
    status_.closed = direct_closed && skin_configuration_closed && npc_->Status().closed &&
                     (!outfit_ || outfit_->Status().closed) && (!appearance_ || appearance_->Status().closed) &&
                     (!events_ || events_->Status().closed) &&
                     (!alternate_ || alternate_->Status().closed);
}
}
