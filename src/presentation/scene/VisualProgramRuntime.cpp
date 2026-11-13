#include <hksr/framework/presentation/scene/VisualProgramRuntime.hpp>
#include <hksr/framework/presentation/scene/VisualProgramBindings.hpp>
#include <hksr/framework/presentation/scene/VisualProgramPlayer.hpp>
#include <hksr/framework/presentation/movement/VisualMovement.hpp>
#include <hksr/presentation/PresentationResources.hpp>
#include <hksr/model/ModelServices.hpp>
#include <hksr/model/ActorVisualConfiguration.hpp>
#include <hksr/model/ActorModelChange.hpp>
#include <hksr/entity/LifecycleHooks.hpp>
#include <hksr/entity/EntityAccess.hpp>
#include <hksr/entity/EntityOwner.hpp>
#include <hksr/presentation/attachments/AttachmentVisibility.hpp>
#include <hksr/presentation/attachments/AttachmentScale.hpp>
#include <hksr/world/WorldPlacement.hpp>
#include <hksr/framework/tasks/TaskCatalog.hpp>
#include <hksr/framework/presentation/playback/AnimatorLayerWeights.hpp>
#include <hksr/framework/presentation/model/ActorModelScopes.hpp>
#include <hksr/framework/presentation/timing/ActorTimingScopes.hpp>
#include <hksr/framework/presentation/model/ActorRenderingScopes.hpp>
#include <hksr/framework/presentation/audio/ActorBgmScopes.hpp>
#include <hksr/framework/presentation/effects/ActorEventEffects.hpp>
#include <hksr/animation/ActorAnimationSession.hpp>
#include <hksr/animation/AnimationResources.hpp>
#include <hksr/animation/AnimationControl.hpp>
#include <stdexcept>
#include <sstream>
#include <atomic>
#include <algorithm>
#include "VisualProgramCallbacks.hpp"
#include "VisualBodyResources.hpp"
#include "VisualSceneTransients.hpp"

namespace hksr::framework {
namespace {
bool Alive(api::EntityOrigin origin) {
    return origin.token.entity && origin.token.generation && origin.world &&
           !api::TrackedEntities().IsDisposed(origin.token) &&
           api::TrackedEntities().Current(origin.token.entity).generation == origin.token.generation;
}
VisualEventRule SkinRule(VisualEventRule rule, const std::shared_ptr<api::CharacterSkin>& skin) {
    if (!skin)
        return rule;
    std::map<std::string, std::string> paths;
    for (const auto& [path, key] : rule.effect_keys)
        if (const auto mapped = skin->MapResource(path)) {
            const auto [entry, inserted] = paths.emplace(*mapped, key);
            if (!inserted && entry->second != key)
                throw std::runtime_error("skin resource mapping has conflicting native effect keys");
        }
    rule.effect_keys = std::move(paths);
    return rule;
}
}
struct VisualProgramRuntime::ActorScene {
    api::EntityOrigin owner;
    VisualProgramInput input;
    VisualProgramBindings bindings;
    explicit ActorScene(api::EntityOrigin value) : owner(value), transients(value) {}
    struct Inputs {
        std::weak_ptr<VisualEffectContext> context;
        std::map<std::string, std::uint64_t> targets;
        VisualSpatialAnchors anchors;
        std::map<std::string, api::EntityOrigin> resolved;
        bool restored = false;
        std::optional<api::Position3> movement_origin;
        VisualSpatialAnchors positions;
        std::map<std::string, std::string> aliases;
        std::map<std::string, std::vector<std::string>> collections;
        std::uint64_t placement_epoch = 0;
    };
    std::map<VisualEffectContext*, Inputs> context_inputs;
    struct Channel {
        std::vector<VisualEventRoutes::RuleId> rules;
        api::ManagedRootId receiver = 0;
        std::uint64_t generation = 0;
    };
    std::map<VisualContextKey, Channel> channels;
    std::shared_ptr<VisualProgramPlayer> player;
    std::shared_ptr<ActorEventEffects> native_effects;
    std::unique_ptr<VisualPartners> partners;
    detail::VisualBodyResources body;
    detail::VisualSceneTransients transients;
    struct BodyScope {
        std::shared_ptr<VisualEffectContext> context;
        std::uint64_t token = 0;
        std::unique_ptr<VisualContextWork> work;
    };
    std::map<VisualEffectContext*, BodyScope> body_scopes;
    std::string last_body_issue;
    std::uint64_t BodyToken(const std::shared_ptr<VisualEffectContext>& context) {
        auto found = body_scopes.find(context.get());
        if (found == body_scopes.end()) {
            static std::atomic<std::uint64_t> next_scope{0};
            found = body_scopes
                        .emplace(context.get(),
                                 BodyScope{context, ++next_scope, std::make_unique<VisualContextWork>(context)})
                        .first;
        }
        return found->second.token;
    }
    std::unique_ptr<api::ManagedRoots> roots;
    std::vector<api::ManagedRootId> releasing;
    std::string last_trace;
    api::ManagedRootId Keep(void* object) {
        const auto id = roots->Keep(object);
        if (!roots->Status(id).ready) {
            releasing.push_back(id);
            throw std::runtime_error("visual input retention failed");
        }
        return id;
    }
    void ReleasePending() {
        for (auto it = releasing.begin(); it != releasing.end();) {
            if (roots->Release(*it).complete) {
                roots->Forget(*it);
                it = releasing.erase(it);
            } else
                ++it;
        }
    }
};
VisualProgramRuntime::VisualProgramRuntime(api::PresentationContext& context, api::ManagedObjects& objects,
                                           VisualEventRoutes::Trace trace)
    : api_(context), objects_(objects),
      routes_([this](const auto& source) { return api_.ReadEventDefinitions(source); },
              [this](const auto& source, const auto& task) { return sequences_->Track(source, task); }, trace,
              [this](const auto& trigger) { return api_.ReadEffectTriggerPath(trigger); }),
      trace_(std::move(trace)), shared_visual_(std::make_shared<SharedVisualServices>()) {
    auto callbacks = detail::ConnectVisualProgramCallbacks(
        routes_,
        [this](void* text) {
            const auto file = api_.Resources().Components().ReadText(text);
            if (!file.read.returned)
                throw std::runtime_error("visual event source path unread: " + file.error);
            return file.utf8;
        },
        [this](const api::EffectSubmission& value) {
            if (!value.trigger.serial)
                return;
            const auto actors = native_effects_;
            for (const auto& actor : actors)
                if (actor.effects->Accepts(value)) {
                    const auto path = api_.ReadEffectTriggerPath(value.trigger);
                    if (!path.read.returned)
                        throw std::runtime_error("native event effect path unread: " + path.error);
                    actor.effects->Track(value, path.utf8);
                    break;
                }
        },
        [this](const api::EffectSubmission& value, bool returned) {
            for (const auto& actor : native_effects_)
                actor.effects->Returned(value, returned);
        });
    sequences_ = api_.CreateSequenceObserver(std::move(callbacks.sequence));
    constructions_ = api_.CreateEventConstructionObserver(
        std::move(callbacks.before_construction), std::move(callbacks.tasks), std::move(callbacks.after_construction));
    effects_ = api_.CreateEffectObserver(std::move(callbacks.before_effect), std::move(callbacks.after_effect));
}
VisualProgramRuntime::~VisualProgramRuntime() = default;
void VisualProgramRuntime::Activate() {
    if (closing_)
        return;
    sequences_->ActivateOnCurrentThread();
    constructions_->ActivateOnCurrentThread();
    effects_->ActivateOnCurrentThread();
}
void VisualProgramRuntime::BeginFrame(const api::WorldFrame& frame) {
    frame_ = frame;
    const auto scenes = scenes_;
    for (const auto& scene : scenes)
        if (scene->partners)
            scene->partners->BeginFrame(frame);
}
VisualPartners& VisualProgramRuntime::Partners(const std::shared_ptr<ActorScene>& scene) {
    if (!scene->partners) {
        std::weak_ptr<ActorScene> weak = scene;
        VisualPartnerServices services;
        services.create = scene->bindings.skin ? api_.Partners(scene->bindings.skin) : api_.Partners();
        services.controls = [this, owner = scene->owner](api::EntityToken partner) {
            return api_.PartnerControls(partner, owner.token, owner.world);
        };
        services.target = [this, weak](auto& context, const std::string& name) {
            const auto current = weak.lock();
            if (!current)
                throw std::runtime_error("partner parent scene ended");
            return ResolveTarget(*current, context, name);
        };
        services.trace = trace_;
        services.animations = api_.Animations();
        services.objects = &objects_;
        if (scene->bindings.skin)
            services.resource = [skin = scene->bindings.skin](const auto& path) { return skin->MapResource(path); };
        scene->partners =
            std::make_unique<VisualPartners>(scene->owner, scene->input.program->partners, std::move(services));
        if (frame_)
            scene->partners->BeginFrame(*frame_);
    }
    return *scene->partners;
}
std::shared_ptr<VisualProgramPlayer>
VisualProgramRuntime::Create(api::EntityOrigin owner, const VisualProgramInput& input,
                             const VisualProgramBindings& runtime_bindings,
                             const std::shared_ptr<const PresentationProfile>& profile) {
    if (closing_ || !Alive(owner) || !input.program)
        throw std::invalid_argument("visual program requires a live creation and program");
    auto scene = std::make_shared<ActorScene>(owner);
    scene->input = input;
    scene->bindings = runtime_bindings;
    scene->roots = api_.CreateRoots();
    for (const auto& actor : native_effects_)
        if (actor.owner.token.entity == owner.token.entity && actor.owner.token.generation == owner.token.generation &&
            actor.owner.world == owner.world)
            scene->native_effects = actor.effects;
    if (!scene->native_effects) {
        scene->native_effects = std::make_shared<ActorEventEffects>(owner, api_.EffectServices());
        native_effects_.push_back({owner, scene->native_effects});
    }
    scenes_.push_back(scene); // own partial declaration/setup before any later operation can fail
    VisualProgramServices services;
    services.character = runtime_bindings.character;
    services.effects = api_.EffectServices();
    services.choices = input.choices;
    services.task_contexts = runtime_bindings.task_contexts;
    std::weak_ptr<ActorScene> weak = scene;
    auto current = [weak] {
        auto value = weak.lock();
        if (!value)
            throw std::logic_error("visual scene ended");
        return value;
    };
    services.bind_participants = [weak](const VisualContextBinding& binding) {
        const auto scene = weak.lock();
        if (!scene)
            throw std::runtime_error("visual scene ended");
        auto resolve = [&](std::uint64_t id) {
            const auto origin = id == scene->bindings.character     ? scene->owner
                                : scene->bindings.resolve_character ? scene->bindings.resolve_character(id)
                                                                 : api::EntityOrigin{};
            if (!Alive(origin) || origin.world != scene->owner.world)
                throw std::runtime_error("context participant is not a live owned character: " + std::to_string(id));
            return origin;
        };
        const auto receiver = resolve(binding.receiver), caster = resolve(binding.caster);
        return [weak, binding, receiver, caster] {
            const auto scene = weak.lock();
            if (!scene || !Alive(scene->owner))
                return false;
            auto same = [&](std::uint64_t id, api::EntityOrigin captured) {
                const auto now = id == scene->bindings.character     ? scene->owner
                                 : scene->bindings.resolve_character ? scene->bindings.resolve_character(id)
                                                                  : api::EntityOrigin{};
                return Alive(captured) && now.token.entity == captured.token.entity &&
                       now.token.generation == captured.token.generation && now.world == captured.world;
            };
            return same(binding.receiver, receiver) && same(binding.caster, caster);
        };
    };
    services.read_phase = [this, current](const std::string& name) {
        const auto scene = current();
        const auto& program = scene->input.program;
        auto& cache = phase_cache_[program.get()];
        cache.program = program;
        if (const auto found = cache.values.find(name); found != cache.values.end())
            return found->second;
        if (!phase_reader_)
            throw std::runtime_error("phase data reader unavailable");
        auto value = std::make_shared<const api::PhaseVisualConfigSources>(
            phase_reader_(program->source, program->phases.at(name)));
        if (!value->visuals.context_available) {
            std::string why = "phase source unread: " + name;
            for (const auto& issue : value->issues)
                why += "; " + issue.error;
            throw std::runtime_error(why);
        }
        if (value->visuals.character.phase_present && !value->standby.available)
            throw std::runtime_error("phase standby unread: " + name + "; " + value->standby.error);
        if (trace_)
            for (const auto& issue : value->issues)
                trace_("visual phase " + name + ": " + issue.operation + ": " + issue.error);
        cache.values.emplace(name, value);
        return value;
    };
    services.select_inputs = [current] {
        auto value = current();
        auto targets = value->input.target_characters;
        auto anchors = value->input.anchors;
        auto choices = value->input.choices;
        VisualProgramParameters result;
        if (value->bindings.providers.read_parameters) {
            auto parameters = value->bindings.providers.read_parameters(value->bindings.character);
            for (auto& [key, id] : parameters.targets)
                targets[key] = id;
            for (auto& [key, choice] : parameters.choices)
                choices[key] = std::move(choice);
            for (const auto& [key, pose] : parameters.anchors)
                anchors[key] = pose;
            result.position_targets = std::move(parameters.position_targets);
            result.target_aliases = std::move(parameters.target_aliases);
            result.target_collections = std::move(parameters.target_collections);
        }
        result.targets = std::move(targets);
        result.choices = std::move(choices);
        result.anchors = std::move(anchors);
        return result;
    };
    services.available_targets = [this, current](const VisualProgramParameters& inputs) {
        return AvailableEntities(*current(), inputs);
    };
    for (const auto& [name, target] : runtime_bindings.targets) {
        (void)target;
        services.bound_targets.push_back(name);
    }
    services.create = [this, current](auto& context, const auto& key, const auto& value) {
        return CreateEffect(*current(), context, key, value);
    };
    services.partner = [this, current](auto context, const auto& op) {
        return Partners(current()).Execute(std::move(context), op);
    };
    services.end_partners = [current](auto context) {
        const auto scene = current();
        if (scene->partners)
            scene->partners->End(context);
    };
    services.partners_drained = [current] {
        const auto scene = current();
        return !scene->partners || scene->partners->Drained();
    };
    services.partner_scope_drained = [current](auto context) {
        const auto scene = current();
        return !scene->partners || scene->partners->Drained(context);
    };
    services.partner_animation = [this, current](const auto& key) { return Partners(current()).Animation(key); };
    services.prepare_inputs = [this, current](auto context, const VisualProgramParameters& inputs, bool restored,
                                             bool needs_movement_origin) {
        auto scene = current();
        std::vector<std::string> bound;
        if (!restored)
            for (const auto& [name, target] : scene->bindings.targets) {
                (void)target;
                bound.push_back(name);
            }
        const auto bindings = ResolveVisualTargetBindings(scene->input.program->target_defaults, inputs,
                                                          AvailableEntities(*scene, inputs, restored), bound);
        ActorScene::Inputs captured{context, inputs.targets, inputs.anchors, {}, restored};
        captured.collections = inputs.target_collections;
        if (scene->bindings.placement_intent)
            captured.placement_epoch = scene->bindings.placement_intent->Epoch();
        const auto pending = scene->bindings.pending_placement ? scene->bindings.pending_placement() : std::nullopt;
        if (needs_movement_origin) {
            if (pending)
                captured.movement_origin = pending->position;
            else {
                const auto origin = api_.ReadLogicalPosition(scene->owner.token, scene->owner.world);
                if (!origin.read.returned || !Alive(scene->owner))
                    throw std::runtime_error("movement action origin unread");
                captured.movement_origin = origin.value;
            }
        }
        captured.positions = CaptureVisualPositionTargets(bindings, [&] {
            if (pending)
                return VisualSpatialAnchor{pending->position, pending->rotation};
            const auto pose = api_.ReadEntityPlacement(scene->owner.token, scene->owner.world);
            if (!Alive(scene->owner) || !pose.ready)
                throw std::runtime_error("position target owner pose unread: " + pose.error);
            return VisualSpatialAnchor{pose.position, pose.rotation};
        });
        for (const auto& [name, value] : bindings)
            if (const auto* entity = std::get_if<VisualEntityTarget>(&value))
                if (name != entity->binding)
                    captured.aliases[name] = entity->binding;
        scene->context_inputs[context.get()] = std::move(captured);
    };
    services.scope_inputs = [current](const auto& context) {
        const auto scene = current();
        VisualProgramParameters result;
        const auto& inputs = scene->context_inputs.at(const_cast<VisualEffectContext*>(&context));
        result.targets = inputs.targets;
        result.anchors = inputs.anchors;
        result.position_targets = inputs.positions;
        result.target_aliases = inputs.aliases;
        result.target_collections = inputs.collections;
        return result;
    };
    services.capture_inputs = [current](const auto& context) {
        const auto scene = current();
        VisualProgramParameters result;
        const auto found = scene->context_inputs.find(const_cast<VisualEffectContext*>(&context));
        if (found == scene->context_inputs.end())
            throw std::runtime_error("visual context inputs unavailable");
        const auto& inputs = found->second;
        for (const auto& [name, target] : inputs.resolved)
            if (name != "Caster" && !inputs.restored && scene->bindings.targets.count(name) && !inputs.targets.count(name))
                throw std::runtime_error("visual target has no persistent character identity: " + name);
        result.targets = inputs.targets;
        result.anchors = inputs.anchors;
        result.position_targets = inputs.positions;
        result.target_aliases = inputs.aliases;
        result.target_collections = inputs.collections;
        return result;
    };
    services.restore_inputs_ready = [this, current](const VisualProgramParameters& inputs) {
        const auto scene = current();
        for (const auto& [name, id] : inputs.targets) {
            if (!id)
                continue;
            if (!scene->bindings.resolve_character)
                return VisualStepResult{VisualStepState::Failed, "visual target resolver unavailable"};
            if (scene->bindings.restore_target) {
                const auto readiness = scene->bindings.restore_target(id);
                if (readiness.state != VisualStepState::Done)
                    return readiness;
            }
            const auto origin = scene->bindings.resolve_character(id);
            if (!Alive(origin))
                return VisualStepResult{VisualStepState::Pending,
                                        "waiting for restored target " + name + " character=" + std::to_string(id)};
            if (origin.world != scene->owner.world)
                return VisualStepResult{VisualStepState::Failed,
                                        "restored visual target belongs to another world: " + name};
            const auto state = api_.Models()->Entities().Read(origin.token.entity);
            if (!state.loaded.read.returned || !state.loaded.value)
                return VisualStepResult{VisualStepState::Pending, "waiting for target model: " + name};
        }
        return VisualStepResult{VisualStepState::Done, {}};
    };
    services.context_started = [this, current](const VisualContextKey& name, std::uint64_t generation, auto context) {
        Attach(*current(), context, name, generation);
    };
    services.context_stopped = [this, current](const VisualContextKey& name, std::uint64_t generation) {
        Detach(*current(), name, generation);
    };
    services.effect_state = [this, current](auto& context, auto id, const VisualEffectState& op) {
        const auto scene = current();
        if (!Alive(scene->owner))
            return VisualStepResult{VisualStepState::Failed, "visual owner ended"};
        const auto result = context.SetAnimatorState(id, op.state, op.transition, op.restart);
        return VisualStepResult{result.state == api::EffectControlState::Applied     ? VisualStepState::Done
                                : result.state == api::EffectControlState::NotLoaded ? VisualStepState::Pending
                                                                                     : VisualStepState::Failed,
                                result.detail};
    };
    services.layout = [this, current](auto& context, auto id, auto target, const VisualEffectLayout& op) {
        const auto scene = current();
        if (!Alive(scene->owner))
            return VisualStepResult{VisualStepState::Failed, "layout owner ended"};
        if (!layouts_)
            layouts_ = api_.CreateEffectLayouts();
        api::EffectLayoutCommand command;
        command.action = op.action;
        command.perform = op.perform;
        command.fade = op.fade;
        const auto result = context.OperateLayout(*layouts_, id, target, command);
        return VisualStepResult{
            !result.references_released ? VisualStepState::Failed
            : (result.state == api::EffectControlState::Applied || result.state == api::EffectControlState::NoMatch)
                ? VisualStepState::Done
            : result.state == api::EffectControlState::NotLoaded   ? VisualStepState::Pending
            : result.state == api::EffectControlState::Unsupported ? VisualStepState::Skipped
                                                                   : VisualStepState::Failed,
            result.detail};
    };
    services.follow = [this, current](auto& context, auto id, const VisualFollow& op) {
        const auto scene = current();
        std::vector<api::EntityOrigin> targets;
        for (const auto& name : op.targets)
            targets.push_back(ResolveTarget(*scene, context, name));
        context.UseEntityMotion(Motion(), scene->owner);
        const auto result = context.BindTargets(id, targets);
        return VisualStepResult{
            result.state == api::EffectControlState::Applied ? VisualStepState::Done
            : (result.state == api::EffectControlState::NotLoaded || result.state == api::EffectControlState::Pending)
                ? VisualStepState::Pending
                : VisualStepState::Failed,
            result.detail};
    };
    services.attach = [this, current](auto context) { Attach(*current(), std::move(context)); };
    services.point_hit = [this, current](std::shared_ptr<VisualEffectContext> context, const std::string& name,
                                         const HitInput& command) -> std::optional<SpatialHitEndpoint> {
        const auto scene = current();
        const auto point = PositionTarget(*scene, *context, name);
        if (!point)
            return {};
        auto effects = scene->transients.FindPointHit(context, name);
        if (!effects) {
            std::ostringstream label;
            label << "position hit owner=" << scene->owner.token.generation << " target=" << name
                  << " point=" << point->position.x << "," << point->position.y << "," << point->position.z << " ";
            SpatialHitServices calls;
            calls.create = [this, label = label.str()] { return api_.EffectServices()->CreateRequest(label); };
            std::weak_ptr<ActorScene> weak = scene;
            calls.start = [this, weak](auto& request, const std::string& name, const auto& options) {
                const auto scene = weak.lock();
                if (!scene || !Alive(scene->owner))
                    throw std::runtime_error("position hit owner ended");
                const auto path = objects_.NewStringUtf8(name.c_str());
                if (!path.call.returned || !path.object)
                    throw std::runtime_error("position hit path allocation failed");
                const auto pin = scene->Keep(path.object);
                scene->releasing.push_back(pin);
                if (!Alive(scene->owner))
                    throw std::runtime_error("position hit owner ended during input retention");
                api::EffectSubmissionOptions submission;
                if (scene->bindings.skin)
                    submission.path_mode = api::EffectPathMode::Resolved;
                request.StartSpatial(scene->owner.token.entity, path.object, options, submission);
            };
            effects = std::make_shared<SpatialHitEffects>(*point, std::move(calls));
            scene->transients.KeepPointHit(context, name, effects, frame_ ? frame_->sequence : 0);
        }
        auto prepared = command;
        if (prepared.effect && scene->bindings.skin) {
            const auto path = scene->bindings.skin->MapResource(prepared.effect->path);
            if (path)
                prepared.effect->path = *path;
            else
                prepared.effect.reset();
        }
        auto result = effects->Submit(prepared);
        effects->Service(0);
        return result;
    };
    services.hit = [this, current](VisualEffectContext& context, const std::string& target_name,
                                   const HitInput& command) -> std::optional<HitDelivery> {
        const auto scene = current();
        if (!Alive(scene->owner))
            throw std::runtime_error("hit source scene ended");
        const auto target = ResolveTarget(*scene, context, target_name);
        if (!scene->bindings.resolve_hit_receiver)
            throw std::runtime_error("owned hit receiver resolver unavailable");
        const auto receiver = scene->bindings.resolve_hit_receiver(target);
        if (!receiver)
            return {};
        if (!Alive(scene->owner) || !Alive(target) || receiver->Generation() != target.token.generation)
            throw std::runtime_error("hit receiver generation changed");
        auto hit = command;
        if (hit.direction || hit.effect) {
            const auto position = api_.ReadLogicalPosition(scene->owner.token, scene->owner.world);
            if (!Alive(scene->owner) || !Alive(target))
                throw std::runtime_error("hit endpoint ended during capture");
            if (!position.read.returned)
                throw std::runtime_error("hit source logical position unread");
            hit.origin = HitOrigin{position.value,
                                   scene->owner.token.entity == target.token.entity &&
                                       scene->owner.token.generation == target.token.generation,
                                   scene->owner, scene->bindings.skin};
        }
        return receiver->Post(std::move(hit));
    };
    services.face = [this, current](VisualEffectContext& context, const VisualFace& command) {
        const auto scene = current();
        if (closing_ || !Alive(scene->owner))
            throw std::runtime_error("facing scene ended");
        if (!api_.ReadAnimator(scene->owner.token))
            return VisualStepResult{VisualStepState::Pending, "waiting for owned model"};
        const auto pose = TargetPose(*scene, context, command.target);
        const auto call = api_.FaceEntity(scene->owner.token, scene->owner.world, pose.position, command.ratio,
                                          command.angle_offset, command.duration);
        return VisualStepResult{call.returned ? VisualStepState::Done : VisualStepState::Failed,
                                call.returned ? "" : "owned facing submission failed"};
    };
    services.attachment_visibility = [this, current](std::shared_ptr<VisualEffectContext> context,
                                                     const VisualAttachmentVisibility& command) {
        const auto scene = current();
        if (closing_ || !Alive(scene->owner))
            throw std::runtime_error("attachment scene ended");
        if (!api_.ReadAnimator(scene->owner.token))
            return VisualStepResult{VisualStepState::Pending, "waiting for owned model"};
        if (!scene->body.attachments)
            scene->body.attachments = api_.CreateAttachmentVisibility(scene->owner.token, scene->owner.world);
        const auto scope = scene->BodyToken(context);
        for (const auto& point : command.points)
            scene->body.attachments->Set(scope, point, command.visible, command.delay);
        return VisualStepResult{VisualStepState::Done, {}};
    };
    services.bgm_sync = [this, current](std::shared_ptr<VisualEffectContext> context, const VisualBgmSync& command) {
        const auto scene = current();
        if (closing_ || !Alive(scene->owner))
            return VisualStepResult{VisualStepState::Failed, "BGM sync actor ended"};
        if (scene->body.bgm && !scene->body.bgm->Current()) {
            if (!scene->body.bgm->Close())
                return VisualStepResult{VisualStepState::Pending, scene->body.bgm->Error()};
            scene->body.bgm.reset();
        }
        if (!scene->body.bgm) {
            auto client = api_.CreateActorBgmSync(scene->owner.token, scene->owner.world);
            if (!client)
                return VisualStepResult{VisualStepState::Pending, "waiting for BGM sync model"};
            scene->body.bgm = std::make_unique<ActorBgmScopes>(std::move(client));
        }
        return scene->body.bgm->Execute(scene->BodyToken(context), command);
    };
    services.attachment_scale = [this, current](std::shared_ptr<VisualEffectContext> context,
                                                const VisualAttachmentScale& command) {
        const auto scene = current();
        if (closing_ || !Alive(scene->owner))
            throw std::runtime_error("attachment scene ended");
        if (!api_.ReadAnimator(scene->owner.token))
            return VisualStepResult{VisualStepState::Pending, "waiting for owned model"};
        if (!scene->body.attachment_scale)
            scene->body.attachment_scale = api_.CreateAttachmentScale(scene->owner.token, scene->owner.world);
        const auto scope = scene->BodyToken(context);
        for (const auto& point : command.points)
            scene->body.attachment_scale->Set(scope, point, command.scale);
        return VisualStepResult{VisualStepState::Done, {}};
    };
    auto timing = [this, current] {
        const auto scene = current();
        if (closing_ || !Alive(scene->owner))
            throw std::runtime_error("actor timing scene ended");
        if (!scene->body.timing) {
            scene->body.timing = api_.CreateActorTiming(scene->owner.token, scene->owner.world);
            scene->body.timing_scopes = std::make_unique<ActorTimingScopes>(*scene->body.timing);
        }
        return scene;
    };
    services.layer_weight = [timing](std::shared_ptr<VisualEffectContext> context, const VisualLayerWeight& command) {
        const auto scene = timing();
        if (!scene->body.timing->Layer(command.layer))
            return VisualStepResult{VisualStepState::Skipped, "owned actor layer/component unavailable"};
        scene->body.timing_scopes->Layer(scene->BodyToken(context), command.layer, command.weight, command.duration);
        return VisualStepResult{VisualStepState::Done, {}};
    };
    services.animator_speed = [timing](std::shared_ptr<VisualEffectContext> context,
                                       const VisualAnimatorSpeed& command) {
        const auto scene = timing();
        if (!scene->body.timing->Speed())
            return VisualStepResult{VisualStepState::Skipped, "owned actor CFF lock-speed component unavailable"};
        scene->body.timing_scopes->Speed(scene->BodyToken(context), command.speed);
        return VisualStepResult{VisualStepState::Done, {}};
    };
    services.actor_time_scale = [this, current, timing](std::shared_ptr<VisualEffectContext> context,
                                                        const VisualActorTimeScale& command) {
        const auto scene = current();
        if (closing_ || !Alive(scene->owner))
            throw std::runtime_error("actor clock scene ended");
        auto recipient = command.recipient;
        const auto inputs = scene->context_inputs.find(context.get());
        std::set<std::string> aliases;
        while (inputs != scene->context_inputs.end() && inputs->second.aliases.count(recipient)) {
            if (!aliases.insert(recipient).second)
                throw std::runtime_error("cyclic actor clock recipient alias");
            recipient = inputs->second.aliases.at(recipient);
        }
        if (PositionTarget(*scene, *context, recipient))
            return VisualStepResult{VisualStepState::Skipped, "actor clock recipient is a position without an actor"};
        const auto target = ResolveTarget(*scene, *context, recipient);
        if (api_.Owner().OwnedToken(target.token.entity).generation != target.token.generation)
            return VisualStepResult{VisualStepState::Skipped, "actor clock recipient is not an owned actor"};
        ActorTimingScopes* scopes = nullptr;
        if (target.token.entity == scene->owner.token.entity &&
            target.token.generation == scene->owner.token.generation)
            scopes = timing()->body.timing_scopes.get();
        else {
            auto found = std::find_if(
                scene->body.foreign_timings.begin(), scene->body.foreign_timings.end(), [&](const auto& entry) {
                    return entry.origin.world == target.world && entry.origin.token.entity == target.token.entity &&
                           entry.origin.token.generation == target.token.generation;
                });
            if (found == scene->body.foreign_timings.end()) {
                auto receiver = api_.CreateActorTiming(target.token, target.world);
                auto scoped = std::make_unique<ActorTimingScopes>(*receiver);
                scene->body.foreign_timings.push_back({target, std::move(receiver), std::move(scoped)});
                found = std::prev(scene->body.foreign_timings.end());
            }
            scopes = found->scopes.get();
        }
        const auto scope = scene->BodyToken(context);
        if (command.active)
            scopes->Scale(scope, command.key, command.input);
        else {
            const auto result = command.finite_only ? scopes->ClearFinite(scope, command.fade)
                                                    : scopes->ClearScale(scope, command.key, command.fade);
            if (result == TimingClearState::Pending)
                return VisualStepResult{VisualStepState::Pending, "waiting for owned actor clock fade"};
            if (result == TimingClearState::Failed)
                return VisualStepResult{VisualStepState::Failed, "owned actor clock removal failed"};
        }
        return VisualStepResult{VisualStepState::Done, {}};
    };
    services.rendering = [this, current](std::shared_ptr<VisualEffectContext> context,
                                         const VisualRenderingOperation& command) {
        const auto scene = current();
        if (closing_ || !Alive(scene->owner))
            throw std::runtime_error("renderer source scene ended");
        auto recipient = command.recipient;
        const auto inputs = scene->context_inputs.find(context.get());
        std::set<std::string> aliases;
        while (inputs != scene->context_inputs.end() && inputs->second.aliases.count(recipient)) {
            if (!aliases.insert(recipient).second)
                throw std::runtime_error("cyclic rendering recipient alias");
            recipient = inputs->second.aliases.at(recipient);
        }
        if (PositionTarget(*scene, *context, recipient))
            return VisualStepResult{VisualStepState::Skipped, "renderer recipient is a position without a model"};
        const auto target = ResolveTarget(*scene, *context, recipient);
        if (api_.Owner().OwnedToken(target.token.entity).generation != target.token.generation)
            return VisualStepResult{VisualStepState::Skipped, "renderer recipient is not an owned actor"};
        if (!rendering_)
            rendering_ = std::make_unique<ActorRenderingScopes>(
                [this](api::EntityOrigin actor) { return api_.CreateActorRendering(actor.token, actor.world); });
        const bool applied = rendering_->Apply(target, scene->BodyToken(context), command);
        return VisualStepResult{applied ? VisualStepState::Done : VisualStepState::Skipped,
                                applied ? std::string{} : "owned rendering component or named target unavailable"};
    };
    services.model = [this, current](std::shared_ptr<VisualEffectContext> context,
                                     const VisualModelOperation& operation) {
        const auto scene = current();
        if (closing_ || !Alive(scene->owner))
            throw std::runtime_error("model presentation scene ended");
        try {
            if (const auto* change = std::get_if<VisualModelChange>(&operation.command)) {
                if (!PrepareModelChange(scene->owner))
                    return VisualStepResult{VisualStepState::Pending, "releasing prior model contributions"};
                auto path = change->path;
                if (scene->bindings.skin) {
                    path = path ? scene->bindings.skin->MapResource(*path)
                                : std::optional<std::string>(scene->bindings.skin->Status().battle_model);
                    if (!path)
                        return VisualStepResult{VisualStepState::Skipped, "model resource suppressed by selected skin"};
                }
                auto request = api_.CreateActorModelChange(scene->owner.token, scene->owner.world, path);
                if (!request)
                    return VisualStepResult{VisualStepState::Pending, "waiting for owned model component"};
                auto& call = scene->transients.KeepModelChange(std::move(request));
                call.Service();
                const auto status = call.Status();
                if (trace_)
                    trace_("model-change key=" + change->key +
                           " phase=" + std::to_string(static_cast<int>(status.phase)) + " submitted=" +
                           std::to_string(status.submitted) + " changed=" + std::to_string(status.change.changed) +
                           " path=" + change->path.value_or("<default>") + " detail=" + status.error);
                call.Close(); // GC cleanup does not restore the new model.
                if (status.phase == api::ActorModelChangePhase::Ready) {
                    scene->player->OwnedModelChanged();
                    return VisualStepResult{VisualStepState::Done, {}};
                }
                if (status.phase == api::ActorModelChangePhase::Unchanged)
                    return VisualStepResult{VisualStepState::Skipped, "official model change reported no change"};
                return VisualStepResult{VisualStepState::Failed,
                                        status.error.empty() ? "official model change did not succeed" : status.error};
            }
            if (std::holds_alternative<VisualModelConfiguration>(operation.command) ||
                std::holds_alternative<VisualEmotionEnable>(operation.command)) {
                const auto* configuration = std::get_if<VisualModelConfiguration>(&operation.command);
                if (scene->body.visual_configuration && !scene->body.ConfigurationCurrent(operation)) {
                    if (!scene->body.visual_configuration->Close())
                        return VisualStepResult{VisualStepState::Pending,
                                                "releasing prior model configuration binding"};
                    scene->body.visual_configuration.reset();
                }
                if (!scene->body.visual_configuration) {
                    scene->body.visual_configuration =
                        api_.CreateActorVisualConfiguration(scene->owner.token, scene->owner.world);
                    if (!scene->body.visual_configuration)
                        return VisualStepResult{VisualStepState::Pending,
                                                "waiting for owned visual configuration model"};
                }
                if (configuration) {
                    const auto result = scene->body.visual_configuration->Apply(configuration->input);
                    if (!result.unsupported_fields.empty()) {
                        std::string detail = "unsupported visual configuration fields: ";
                        for (std::size_t i = 0; i < result.unsupported_fields.size(); ++i) {
                            if (i)
                                detail += ", ";
                            detail += result.unsupported_fields[i];
                        }
                        if ((configuration->input.event_files || configuration->input.reset) &&
                            std::find(result.unsupported_fields.begin(), result.unsupported_fields.end(),
                                      "event_files") == result.unsupported_fields.end())
                            detail += "; independent event files applied";
                        if (trace_)
                            trace_("visual-configuration " + detail);
                        return VisualStepResult{VisualStepState::Skipped, std::move(detail)};
                    }
                } else {
                    const auto& enable = std::get<VisualEmotionEnable>(operation.command);
                    if (!scene->body.visual_configuration->EnableEmotion(enable.enabled, enable.transit_out,
                                                                         enable.transit_in))
                        return VisualStepResult{
                            VisualStepState::Skipped,
                            "owned actor has no supported emotion component; alternate Mono branch unavailable"};
                }
                return VisualStepResult{VisualStepState::Done, {}};
            }
            if (const auto* command = std::get_if<VisualCharacterScale>(&operation.command)) {
                auto found = scene->context_inputs.find(context.get());
                if (found == scene->context_inputs.end())
                    throw std::runtime_error("character scale inputs not captured");
                auto names = command->targets;
                const auto& collection =
                    command->read_targets.empty() ? command->target_collection : command->read_targets;
                if (!collection.empty()) {
                    const auto targets = found->second.collections.find(collection);
                    if (targets == found->second.collections.end())
                        throw api::ModelPresentationUnsupported("character scale target collection unavailable: " +
                                                                collection);
                    names = targets->second;
                }
                std::vector<api::EntityOrigin> targets;
                targets.reserve(names.size());
                for (const auto& name : names) {
                    if (found->second.positions.count(name))
                        throw api::ModelPresentationUnsupported(
                            "character scale requires an actor, not a position target");
                    targets.push_back(ResolveTarget(*scene, *context, name));
                }
                if (!character_scale_)
                    character_scale_ = api_.CreateModelScaleProgram();
                const auto result = character_scale_->Set(scene->BodyToken(context), targets, command->input);
                if (result.state == api::ModelScaleProgramState::Pending)
                    return VisualStepResult{VisualStepState::Pending, result.detail};
                if (result.state == api::ModelScaleProgramState::Unsupported)
                    return VisualStepResult{VisualStepState::Skipped, result.detail};
                if (result.state == api::ModelScaleProgramState::Failed)
                    return VisualStepResult{VisualStepState::Failed, result.detail};
                if (!command->save_targets.empty())
                    found->second.collections[command->save_targets] = std::move(names);
                return VisualStepResult{VisualStepState::Done, {}};
            }
            if (scene->body.model_scopes && !scene->body.model_scopes->Current()) {
                if (!scene->body.model_scopes->Close())
                    return VisualStepResult{VisualStepState::Pending, scene->body.model_scopes->Error()};
                scene->body.model_scopes.reset();
            }
            if (!scene->body.model_scopes) {
                auto owned = api_.CreateActorModelPresentation(scene->owner.token, scene->owner.world);
                if (!owned)
                    return VisualStepResult{VisualStepState::Pending, "waiting for owned model/Art"};
                scene->body.model_scopes = std::make_unique<ActorModelScopes>(std::move(owned));
            }
            const auto scope = scene->BodyToken(context);
            std::visit(
                [&](const auto& command) {
                    using T = std::decay_t<decltype(command)>;
                    if constexpr (std::is_same_v<T, VisualFaceMaterial>)
                        scene->body.model_scopes->Face(scope, command.transition);
                    else if constexpr (std::is_same_v<T, VisualModelScale>)
                        scene->body.model_scopes->Scale(scope, command.scale);
                    else if constexpr (std::is_same_v<T, VisualModelScaleFollow>)
                        scene->body.model_scopes->Follow(scope, command.target.empty()
                                                                    ? api::EntityOrigin{}
                                                                    : ResolveTarget(*scene, *context, command.target));
                },
                operation.command);
            return VisualStepResult{VisualStepState::Done, {}};
        } catch (const api::ModelPresentationUnsupported& error) {
            return VisualStepResult{VisualStepState::Skipped, error.what()};
        }
    };
    services.movement = [this, current](VisualEffectContext& context,
                                        const VisualMovement& command) -> std::optional<MovementRequestEndpoint> {
        const auto scene = current();
        if (closing_ || !Alive(scene->owner))
            throw std::runtime_error("movement scene ended");
        auto found = scene->context_inputs.find(&context);
        if (found == scene->context_inputs.end())
            throw std::logic_error("movement scope inputs not captured");
        auto& inputs = found->second;
        const auto intent = scene->bindings.placement_intent;
        if (intent && inputs.placement_epoch != intent->Epoch())
            return intent->Bind(inputs.placement_epoch, scene->owner.token.generation, {});
        if (!api_.ReadAnimator(scene->owner.token))
            return {};
        const auto from = api_.ReadEntityPlacement(scene->owner.token, scene->owner.world);
        if (!from.ready)
            throw std::runtime_error("movement origin unreadable: " + from.error);
        if (!inputs.movement_origin)
            inputs.movement_origin = from.position;
        const auto plan = ResolveVisualMovement(
            command, from.position, *inputs.movement_origin,
            [&](auto kind, const std::string& name) {
                if (kind == MovementDestination::Anchor) {
                    const auto anchor = inputs.anchors.find(name);
                    if (anchor == inputs.anchors.end())
                        throw std::runtime_error("explicit movement anchor missing: " + name);
                    return anchor->second;
                }
                return TargetPose(*scene, context, name);
            },
            [&](const std::string& name) {
                if (PositionTarget(*scene, context, name))
                    return api::MovementGeometry{};
                const auto entity = ResolveTarget(*scene, context, name);
                if (auto live = api_.ReadMovementGeometry(entity.token, entity.world))
                    return *live;
                // Story actors may have no CharacterData. Use their selected source
                // radius, not collision bounds or a main-player component.
                if (!scene->bindings.resolve_movement_source || !radius_reader_)
                    throw std::runtime_error("movement source geometry unavailable");
                const auto source = scene->bindings.resolve_movement_source(entity);
                auto found = base_radii_.find(source);
                if (found == base_radii_.end())
                    found = base_radii_.emplace(source, radius_reader_(source)).first;
                return api::MovementGeometry{found->second, {}};
            });
        auto start = [&] {
            return BindMovementRequest(
                scene->owner.token.generation,
                command.instant
                    ? api_.StartPosition(scene->owner.token, scene->owner.world, plan.segments.front().target)
                    : api_.StartMovement(scene->owner.token, scene->owner.world, command.state, plan));
        };
        return intent ? intent->Bind(inputs.placement_epoch, scene->owner.token.generation, start) : start();
    };
    services.detach = [this, current] { Detach(*current()); };
    services.projectiles = [this, current](std::shared_ptr<VisualEffectContext> context,
                                           const VisualProjectilePlan& plan) {
        const auto scene = current();
        ProjectileBatchServices calls;
        calls.create = [this, owner = scene->owner] {
            return BindProjectileGroup(api_.CreateProjectiles(owner.token, owner.world, {}));
        };
        calls.parameters = [this, program = scene->input.program](const std::string& name) {
            const auto found = program->projectiles.find(name);
            if (found == program->projectiles.end())
                throw std::runtime_error("unknown projectile parameters: " + name);
            if (!projectile_reader_)
                throw std::runtime_error("projectile parameter reader unavailable");
            return BindProjectileTemplate(projectile_reader_(found->second));
        };
        std::weak_ptr<ActorScene> weak = scene;
        calls.target = [this, weak, context](const std::string& name) {
            const auto scene = weak.lock();
            if (!scene)
                throw std::runtime_error("projectile scene ended");
            const auto origin = ResolveTarget(*scene, *context, name);
            return api_.CaptureProjectileTarget(origin.token, origin.world);
        };
        auto batch = std::make_shared<VisualProjectileBatch>(scene->owner, plan, std::move(calls));
        scene->transients.KeepProjectile(batch);
        return ProjectileRequestEndpoint{scene->owner.token.generation,
                                         [this, batch] {
                                             if (frame_)
                                                 batch->Service(*frame_);
                                         },
                                         [batch] { batch->Cancel(); }, [batch] { return batch->Status(); }};
    };
    services.remove_in_flight = [this](auto context, const auto& key, auto options) {
        return routes_.RemoveInFlight(std::move(context), key, options);
    };
    services.remove_event_effect = [current](const VisualRemoveEventEffect& command) {
        const auto scene = current();
        if (!Alive(scene->owner))
            return VisualStepResult{VisualStepState::Failed, "native effect owner ended"};
        auto path = std::optional<std::string>(command.path);
        if (scene->bindings.skin)
            path = scene->bindings.skin->MapResource(command.path);
        return path ? scene->native_effects->Remove(*path, command.options)
                    : VisualStepResult{VisualStepState::Done, {}};
    };
    const auto trace = trace_;
    VisualTimelines::Resource timeline_resource;
    if (runtime_bindings.skin)
        timeline_resource = [skin = runtime_bindings.skin](const auto& path) { return skin->MapResource(path); };
    ConnectVisualTimelines(services, std::make_shared<VisualTimelines>(
                                         [this, owner] { return api_.CreateTimeline(owner.token, owner.world); },
                                         [trace, owner](const auto& text) {
                                             if (trace) {
                                                 std::ostringstream out;
                                                 out << "visual owner=" << owner.token.entity
                                                     << " generation=" << owner.token.generation << " " << text;
                                                 trace(out.str());
                                             }
                                         },
                                         shared_visual_, std::move(timeline_resource)));
    scene->player = std::make_shared<VisualProgramPlayer>(input.program, std::move(services));
    try {
        if (profile && profile->source == input.program->source)
            for (const auto& rule : profile->native_events) {
                if (rule.scope == input.program->scope)
                    scene->channels[{}].rules.push_back(routes_.Declare(owner, SkinRule(rule, runtime_bindings.skin)));
                else
                    for (const auto& [name, context] : input.program->contexts)
                        if (!context.event_scope.empty() && rule.scope == context.event_scope)
                            scene->channels[name].rules.push_back(
                                routes_.Declare(owner, SkinRule(rule, runtime_bindings.skin)));
            }
    } catch (...) {
        scene->player->End();
        throw;
    }
    return scene->player;
}
void VisualProgramRuntime::Attach(ActorScene& scene, std::shared_ptr<VisualEffectContext> context,
                                  const VisualContextKey& name, std::uint64_t generation) {
    Detach(scene, name);
    const auto found = scene.channels.find(name);
    if (found == scene.channels.end())
        return;
    auto& channel = found->second;
    channel.generation = generation;
    if (channel.rules.empty())
        return;
    auto* receiver = api_.ReadEventReceiver(scene.owner.token, scene.owner.world);
    if (!receiver)
        throw std::runtime_error("current model event receiver unavailable");
    channel.receiver = scene.Keep(receiver);
    try {
        for (auto rule : channel.rules)
            routes_.Attach(rule, receiver, context);
    } catch (...) {
        Detach(scene, name, generation);
        throw;
    }
}
void VisualProgramRuntime::Detach(ActorScene& scene, const VisualContextKey& name, std::uint64_t generation) {
    const auto found = scene.channels.find(name);
    if (found == scene.channels.end())
        return;
    auto& channel = found->second;
    if (generation && generation != channel.generation)
        return;
    for (auto rule : channel.rules)
        routes_.Detach(rule);
    if (channel.receiver) {
        scene.releasing.push_back(channel.receiver);
        channel.receiver = 0;
    }
}
std::vector<std::string>
VisualProgramRuntime::AvailableEntities(ActorScene& scene, const VisualProgramParameters& inputs, bool restored) const {
    std::map<std::string, api::EntityOrigin> targets =
        restored ? std::map<std::string, api::EntityOrigin>{} : scene.bindings.targets;
    for (const auto& [name, id] : inputs.targets) {
        targets.erase(name);
        if (id && scene.bindings.resolve_character)
            targets[name] = scene.bindings.resolve_character(id);
    }
    targets["Caster"] = scene.owner;
    std::vector<std::string> out;
    for (const auto& [name, target] : targets)
        if (Alive(target) && target.world == scene.owner.world)
            out.push_back(name);
    return out;
}
std::optional<VisualSpatialAnchor> VisualProgramRuntime::PositionTarget(ActorScene& scene, VisualEffectContext& context,
                                                                        const std::string& name) const {
    if (!Alive(scene.owner))
        throw std::runtime_error("position target owner ended");
    const auto inputs = scene.context_inputs.find(&context);
    if (inputs == scene.context_inputs.end())
        throw std::runtime_error("target inputs have not been captured");
    const auto point = inputs->second.positions.find(name);
    return point == inputs->second.positions.end() ? std::nullopt : std::optional<VisualSpatialAnchor>(point->second);
}
VisualSpatialAnchor VisualProgramRuntime::TargetPose(ActorScene& scene, VisualEffectContext& context,
                                                     const std::string& name) {
    if (const auto point = PositionTarget(scene, context, name))
        return *point;
    const auto target = ResolveTarget(scene, context, name);
    const auto pose = api_.ReadEntityPlacement(target.token, target.world);
    if (!Alive(scene.owner) || !Alive(target) || !pose.ready)
        throw std::runtime_error("target pose unread: " + name + "; " + pose.error);
    return {pose.position, pose.rotation};
}
api::EntityOrigin VisualProgramRuntime::ResolveTarget(ActorScene& scene, VisualEffectContext& context,
                                                      const std::string& name) {
    if (name.compare(0, 8, "partner:") == 0) {
        if (!scene.partners)
            throw std::runtime_error("partner target has not been created: " + name);
        return scene.partners->Resolve(name.substr(8));
    }
    const auto snapshot = scene.context_inputs.find(&context);
    auto* inputs = snapshot == scene.context_inputs.end() ? nullptr : &snapshot->second;
    if (inputs) {
        if (inputs->positions.count(name))
            throw std::runtime_error("operation requires an entity, not a position: " + name);
        if (const auto alias = inputs->aliases.find(name); alias != inputs->aliases.end())
            return ResolveTarget(scene, context, alias->second);
    }
    api::EntityOrigin target = scene.owner;
    if (name != "Caster") {
        const auto resolved =
            inputs ? inputs->resolved.find(name) : std::map<std::string, api::EntityOrigin>::iterator{};
        const auto found = scene.bindings.targets.find(name);
        if (inputs && resolved != inputs->resolved.end())
            target = resolved->second;
        else if (found != scene.bindings.targets.end() && (!inputs || (!inputs->restored && !inputs->targets.count(name))))
            target = found->second;
        else {
            const auto& targets = inputs ? inputs->targets : scene.input.target_characters;
            const auto character = targets.find(name);
            if (character == targets.end() || !scene.bindings.resolve_character)
                throw std::runtime_error("explicit visual target missing: " + name);
            target = scene.bindings.resolve_character(character->second);
        }
    }
    if (!Alive(scene.owner) || !Alive(target) || target.world != scene.owner.world)
        throw std::runtime_error("visual target belongs to an ended generation or another world");
    const auto entities = api_.Models();
    const auto state = entities->Entities().Read(target.token.entity);
    if (!Alive(scene.owner) || !Alive(target) || !state.owner_world.read.returned ||
        state.owner_world.value != target.world || !state.loaded.read.returned || !state.loaded.value)
        throw std::runtime_error("visual target not loaded in its owner world");
    if (inputs)
        inputs->resolved.emplace(name, target);
    return target;
}
std::shared_ptr<VisualEntityMotion> VisualProgramRuntime::Motion() {
    if (motion_)
        return motion_;
    // Resolve only when a program actually uses entity-follow, not at startup
    // for every character or on each frame.
    if (!entity_follow_)
        entity_follow_ = api_.CreateEffectEntityFollow();
    VisualEntityMotionServices services;
    services.read = [this](auto ref) { return entity_follow_->Read(ref); };
    services.set = [this](auto ref, const auto& expected, const auto& desired, auto current) {
        return entity_follow_->Set(ref, expected, desired, std::move(current));
    };
    services.target = [this](void* entity) { return entity_follow_->ReadTarget(entity); };
    services.acquire = [this](api::EntityOrigin origin) {
        return api_.AcquireMovementHost(origin.token, origin.world);
    };
    services.eligible = [this](api::EntityOrigin origin) {
        if (!Alive(origin) || api_.Owner().OwnedToken(origin.token.entity).generation != origin.token.generation)
            return false;
        const auto state = api_.Models()->Entities().Read(origin.token.entity);
        return Alive(origin) && state.owner_world.read.returned && state.owner_world.value == origin.world &&
               state.loaded.read.returned && state.loaded.value;
    };
    services.trace = trace_;
    motion_ = std::make_shared<VisualEntityMotion>(api::TrackedEntities(), std::move(services));
    return motion_;
}
VisualEffectAdmission VisualProgramRuntime::CreateEffect(ActorScene& scene, VisualEffectContext& context,
                                                         const std::string& key, const VisualEffectInput& input) {
    api::EntityOrigin target;
    std::optional<api::SpatialEffectOptions> spatial;
    if (!input.anchor.empty()) {
        if (!input.target.empty())
            throw std::invalid_argument("effect has both entity and spatial targets");
        const auto snapshot = scene.context_inputs.find(&context);
        const auto& anchors = snapshot == scene.context_inputs.end() ? scene.input.anchors : snapshot->second.anchors;
        const auto found = anchors.find(input.anchor);
        if (found == anchors.end())
            throw std::runtime_error("explicit visual anchor missing: " + input.anchor);
        const auto& pose = found->second;
        spatial =
            api::SpatialEffectOptions{pose.position,         pose.rotation,         input.options.unique_name,
                                      input.options.scale,   input.options.fade_in, input.options.simulate_immediately,
                                      input.options.resident};
    } else if (const auto point = PositionTarget(scene, context, input.target)) {
        if (!input.allow_position || input.options.attach_to_target)
            throw std::runtime_error("effect requires an entity target: " + key);
        // This path has no target transform. Offsets are applied to the captured
        // world pose, replacing initial attach-point lookup in this projection.
        // Persistent attachment/follow operations retain their entity contract.
        auto pose = TransformVisualTarget(*point, input.options.position_offset);
        pose.rotation = RotateVisualTarget(pose.rotation, input.options.rotation_offset);
        spatial =
            api::SpatialEffectOptions{pose.position,         pose.rotation,         input.options.unique_name,
                                      input.options.scale,   input.options.fade_in, input.options.simulate_immediately,
                                      input.options.resident};
    } else
        target = ResolveTarget(scene, context, input.target);
    const auto selected =
        scene.bindings.skin ? scene.bindings.skin->MapResource(input.path) : std::optional<std::string>(input.path);
    if (!selected)
        return {VisualEffectAdmissionKind::Suppressed};
    const auto path = objects_.NewStringUtf8(selected->c_str());
    if (!path.call.returned || !path.object)
        throw std::runtime_error("visual effect path allocation failed");
    const auto pin = scene.Keep(path.object);
    scene.releasing.push_back(pin);
    if (!Alive(scene.owner) || (!spatial && !Alive(target)))
        throw std::runtime_error("visual owner ended while preparing effect");
    auto submission = input.submission;
    auto* prepared_path = path.object;
    if (scene.bindings.skin)
        submission.path_mode = api::EffectPathMode::Resolved;
    if (input.mutex) {
        const auto mapped =
            scene.bindings.skin ? path : api_.EffectServices()->MapPath(scene.owner.token.entity, path.object);
        if (!mapped.call.returned || !mapped.object)
            throw std::runtime_error("visual effect path mapping failed");
        scene.releasing.push_back(scene.Keep(mapped.object));
        prepared_path = mapped.object;
        const auto text = api_.Resources().Components().ReadText(mapped.object);
        if (!text.read.returned)
            throw std::runtime_error("mapped effect path unread: " + text.error);
        if (!Alive(scene.owner) || (!spatial && !Alive(target)))
            throw std::runtime_error("effect participant ended during path mapping");
        const auto admission = context.CheckMutex(key, target.token.entity, prepared_path, text.utf8, *input.mutex);
        if (trace_)
            trace_("visual-admission key=" + key + " path=" + text.utf8 + " result=" +
                   (admission ? (admission->kind == VisualEffectAdmissionKind::Replayed ? "replayed" : "suppressed")
                              : "create"));
        if (admission)
            return *admission;
        // Query and submission must use the same mapped resource, not map it
        // a second time through a possibly non-idempotent replacement table.
        submission.path_mode = api::EffectPathMode::Resolved;
    }
    if (!Alive(scene.owner) || (!spatial && !Alive(target)))
        throw std::runtime_error("effect participant ended during admission");
    const auto id = spatial ? context.StartSpatial(key, scene.owner.token.entity, prepared_path, *spatial, submission)
                            : context.StartEntity(key, scene.owner.token.entity, target.token.entity, prepared_path,
                                                  input.options, submission);
    if (trace_) {
        std::ostringstream out;
        out << "visual-submit owner=" << scene.owner.token.entity << " generation=" << scene.owner.token.generation
            << " target=" << target.token.entity << " target_generation=" << target.token.generation << " key=" << key
            << " receipt=" << id;
        if (spatial)
            out << " anchor=" << input.anchor << " position=" << spatial->position.x << "," << spatial->position.y
                << "," << spatial->position.z << " rotation=" << spatial->rotation.x << "," << spatial->rotation.y
                << "," << spatial->rotation.z << "," << spatial->rotation.w;
        trace_(out.str());
    }
    return id;
}
void VisualProgramRuntime::Collect() {
    const auto actors = native_effects_;
    for (const auto& actor : actors)
        actor.effects->Service(closing_ || !Alive(actor.owner));
    native_effects_.erase(std::remove_if(native_effects_.begin(), native_effects_.end(),
                                         [this](const auto& actor) {
                                             return (closing_ || !Alive(actor.owner)) && actor.effects->Drained();
                                         }),
                          native_effects_.end());
    if (motion_)
        motion_->Collect();
    sequences_->Collect();
    routes_.Collect();
    std::map<std::uint64_t, ActorScene*> readers;
    for (const auto& scene : scenes_)
        if (scene->bindings.providers.take_signals)
            readers[scene->bindings.character] = scene.get();
    for (auto it = scenes_.begin(); it != scenes_.end();) {
        auto& scene = **it;
        scene.transients.CollectModelChanges([this](const auto& error) {
            if (trace_)
                trace_("model change cleanup: " + error);
        });
        // A retiring scene must not consume a newer creation's mailbox.
        if (scene.player && scene.bindings.providers.take_signals && readers.at(scene.bindings.character) == &scene)
            for (const auto& signal : scene.bindings.providers.take_signals(scene.bindings.character)) {
                std::uint64_t receipt = 0;
                std::string error;
                try {
                    if (!Alive(scene.owner) || signal.generation != scene.owner.token.generation)
                        throw std::runtime_error("visual signal targets an ended creation");
                    receipt = scene.player->Signal(signal.intent, signal.name);
                    if (!receipt)
                        error = "visual signal token is no longer active";
                } catch (const std::exception& e) {
                    error = e.what();
                }
                if (trace_) {
                    std::ostringstream out;
                    out << "visual-signal character=" << scene.bindings.character << " generation=" << signal.generation
                        << " intent=" << signal.intent << " name=" << signal.name << " receipt=" << receipt;
                    if (!error.empty())
                        out << " error=[" << error << "]";
                    trace_(out.str());
                }
            }
        if (scene.player && (closing_ || !Alive(scene.owner)) && !scene.player->Status().closing)
            scene.player->End();
        if (scene.body.model_scopes)
            try {
                scene.body.model_scopes->Service();
            } catch (const std::exception& error) {
                if (trace_ && scene.last_body_issue != error.what()) {
                    scene.last_body_issue = error.what();
                    trace_("model contribution update: " + scene.last_body_issue);
                }
            }
        if (scene.body.bgm && !scene.body.bgm->Service()) {
            const auto& error = scene.body.bgm->Error();
            if (trace_ && !error.empty() && scene.last_body_issue != error) {
                scene.last_body_issue = error;
                trace_("BGM contribution update: " + error);
            }
        }
        for (auto scope = scene.body_scopes.begin(); scope != scene.body_scopes.end();) {
            const auto context = scope->second.context;
            if (!context || context->Closing() || context->Finishing() || closing_ || !Alive(scene.owner)) {
                const bool natural =
                    context && context->Finishing() && !context->Closing() && !closing_ && Alive(scene.owner);
                const auto body = scene.body.Release(scope->second.token, natural);
                const bool character_scale = !character_scale_ || character_scale_->Release(scope->second.token);
                const bool rendering = !rendering_ || rendering_->Release(scope->second.token);
                if (body.complete && character_scale && rendering)
                    scope = scene.body_scopes.erase(scope);
                else {
                    const auto error = !body.complete     ? body.error
                                       : !character_scale ? character_scale_->Error()
                                                          : rendering_->Error();
                    if (trace_ && !error.empty() && error != scene.last_body_issue) {
                        scene.last_body_issue = error;
                        trace_("body contribution release: " + error);
                    }
                    ++scope;
                }
            } else
                ++scope;
        }
        scene.transients.Service(frame_ ? &*frame_ : nullptr,
                                 closing_ || !Alive(scene.owner) || !scene.player || scene.player->Status().closing);
        if (scene.player && scene.player->Status().closing)
            scene.player->Service();
        if (scene.partners && frame_)
            scene.partners->Service(*frame_);
        scene.ReleasePending();
        for (auto input = scene.context_inputs.begin(); input != scene.context_inputs.end();) {
            if (input->second.context.expired())
                input = scene.context_inputs.erase(input);
            else
                ++input;
        }
        if (trace_ && scene.player) {
            const auto& status = scene.player->Status();
            std::ostringstream out;
            out << "visual-program owner=" << scene.owner.token.entity << " generation=" << scene.owner.token.generation
                << " program=" << scene.input.program->id << " intent=" << status.intent
                << " playback=" << status.playback << " model=" << status.model << " action=" << status.action
                << " state=" << status.state << " prepared=" << status.prepared << " playing=" << status.playing
                << " finished=" << status.finished << " completing=" << status.completing
                << " completed=" << status.completed << " closing=" << status.closing
                << " drained=" << scene.player->Drained();
            out << " signal=" << status.signal << " signal_submitted=" << status.last_signal
                << " signal_completed=" << status.completed_signal;
            for (const auto& [name, token] : scene.player->ActiveContexts())
                out << " context=[" << name.definition << "#" << name.instance << ":" << token << "]";
            for (const auto& value : status.waiting)
                out << " waiting=[" << value << "]";
            for (const auto& value : status.issues)
                out << " issue=[" << value << "]";
            for (const auto& value : status.notes)
                out << " note=[" << value << "]";
            const auto text = out.str();
            if (text != scene.last_trace) {
                trace_(text);
                scene.last_trace = text;
            }
        }
        if ((!scene.player || scene.player->Drained()) && scene.transients.Drained() && scene.body_scopes.empty()) {
            if (!scene.body.Close(detail::BodyDrain::Retire)) {
                ++it;
                continue;
            }
            for (const auto& [name, channel] : scene.channels)
                Detach(scene, name);
            scene.ReleasePending();
            if (scene.releasing.empty()) {
                for (const auto& [name, channel] : scene.channels)
                    for (auto rule : channel.rules)
                        routes_.Unbind(rule);
                it = scenes_.erase(it);
                continue;
            }
        }
        ++it;
    }
}
bool VisualProgramRuntime::PrepareModelChange(api::EntityOrigin origin) {
    bool drained = !motion_ || motion_->ReleaseParticipant(origin);
    if (character_scale_)
        drained = character_scale_->ReleaseParticipant(origin) && drained;
    if (rendering_)
        drained = rendering_->ReleaseParticipant(origin) && drained;
    for (const auto& scene : scenes_) {
        if (scene->owner.world == origin.world && scene->owner.token.entity == origin.token.entity &&
            scene->owner.token.generation == origin.token.generation) {
            const bool body = scene->body.Close(detail::BodyDrain::ModelChange);
            bool character_scale = true;
            if (character_scale_)
                for (const auto& scope : scene->body_scopes)
                    character_scale = character_scale_->Release(scope.second.token) && character_scale;
            bool rendering = true;
            if (rendering_)
                for (const auto& scope : scene->body_scopes)
                    rendering = rendering_->Release(scope.second.token) && rendering;
            if (body && character_scale && rendering) {
                scene->body.Reset();
                scene->body_scopes.clear();
            } else
                drained = false;
        }
        drained = scene->transients.ReleaseParticipant(origin) && drained;
    }
    return drained;
}
bool VisualProgramRuntime::Close() {
    closing_ = true;
    Collect();
    if (!scenes_.empty() || !native_effects_.empty())
        return false;
    if (character_scale_ && !character_scale_->Close())
        return false;
    if (rendering_ && !rendering_->Close())
        return false;
    constructions_->Stop();
    effects_->Stop();
    if (!sequences_->Close())
        return false;
    if (motion_) {
        motion_->Collect();
        if (!motion_->Empty())
            return false;
    }
    if (entity_follow_ && !entity_follow_->Close())
        return false;
    routes_.Clear();
    return true;
}
}
