#include <hksr/framework/presentation/playback/ActionPlayback.hpp>
#include <hksr/model/ModelAccess.hpp>
#include <hksr/framework/presentation/scene/VisualProgramPlayer.hpp>
#include <hksr/framework/presentation/playback/TaskPlaybackBindings.hpp>
#include <hksr/framework/tasks/TaskPresentation.hpp>
#include "../../support/AnimationComponentDeclarations.hpp"
#include <Windows.h>
#include <array>
#include <cstring>
#include <map>
#include <stdexcept>

namespace {
using namespace hksr;
using namespace hksr::framework;
int entity, world, model, animator, other_animator, style, model_type, style_type, motion_type, emotion_type, info,
    next_root;
int transform_type, game_object, root_transform, effect_manager;
struct Logical {
    api::Position3 position{2, 3, 4};
} logical;
auto* owner = reinterpret_cast<api::GameEntity*>(&entity);
auto* own_world = reinterpret_cast<api::GameWorld*>(&world);
std::map<int, void*> roots;
std::vector<std::unique_ptr<std::string>> strings;
struct Layer {
    int state = 10, next = 0;
    float time = 0, weight = 0.25f;
    bool transition = false;
};
std::array<Layer, 4> layers;
std::map<std::string, int> names{{"Attack", 10}, {"Idle", 11},    {"Hit", 20},         {"Heavy", 21},
                                 {"Face", 30},   {"Neutral", 40}, {"ChangedFace", 41}, {"MissingFace", 42}};
int alive_state = 1;
float hit_angle = 0;
int hit_angle_writes = 0;
struct Motion {
    float clock = 0;
} motion;
bool motion_present = false;
float motion_height = 0;
bool changing = false, replaced = false, override_exists = true, fail_weight = false, hit_exists = true;
std::function<void()> on_weight;
unsigned body_plays = 0, layer_plays = 0, weight_writes = 0;
void CheckAt(bool value, int line) {
    if (!value)
        throw std::runtime_error("hit request test failed at line " + std::to_string(line));
}
#define Check(...) CheckAt((__VA_ARGS__), __LINE__)
template <class T> api::Method M(T f) {
    return {reinterpret_cast<void*>(f), &info};
}
int __fastcall Parameter(std::int16_t tag, void*) {
    Check(tag == 1);
    return 123;
}
void __fastcall SetFloat(void* object, int key, float value, void*) {
    Check(object == &animator && key == 123);
    hit_angle = value;
    ++hit_angle_writes;
}
int __fastcall Keep(void* value, int, int, void*) {
    roots[++next_root] = value;
    return next_root;
}
void* __fastcall Target(int id, void*) {
    return roots.at(id);
}
void __fastcall Free(int id, void*) {
    roots.erase(id);
}
unsigned __fastcall Id(void* value, void*) {
    Check(value == owner);
    return 1;
}
api::GameWorld* __fastcall World(void* value, void*) {
    Check(value == owner);
    return own_world;
}
bool __fastcall Yes(void*, void*) {
    return true;
}
int __fastcall EntityType(void* value, void*) {
    Check(value == owner);
    return 1;
}
int __fastcall Alive(void* value, void*) {
    Check(value == owner);
    return alive_state;
}
float __fastcall Height(void* value, float, void*) {
    Check(value == &motion);
    return motion_height;
}
float __fastcall FallHeight(void* value, void*) {
    Check(value == &motion);
    return motion_height;
}
void* __fastcall Component(void* value, void* type, void*) {
    Check(value == owner);
    if (type == &motion_type)
        return motion_present ? &motion : nullptr;
    if (type == &emotion_type)
        return nullptr;
    if (type == &transform_type)
        return &logical;
    return type == &model_type ? static_cast<void*>(&model) : &style;
}
void* __fastcall GameObject(void* value, void*) {
    Check(value == owner);
    return &game_object;
}
void* __fastcall RootTransform(void* value, void*) {
    Check(value == &game_object);
    return &root_transform;
}
api::Position3 __fastcall TransformVector(void* value, api::Position3 vector, void*) {
    Check(value == &logical);
    return vector;
}
std::string visual_layer = "Visual";
void* __fastcall LayerName(void*, int layer, void*) {
    Check(layer == 0);
    return &visual_layer;
}
int __fastcall TextLength(std::string* value, void*) {
    return static_cast<int>(value->size());
}
std::uint16_t __fastcall TextChar(std::string* value, int index, void*) {
    return static_cast<unsigned char>(value->at(index));
}
void* __fastcall Animator(void*, void*) {
    return replaced ? &other_animator : &animator;
}
bool __fastcall Changing(void*, void*) {
    return changing;
}
int __fastcall Hash(std::string* name, void*) {
    return names.at(*name);
}
bool __fastcall Has(void*, int layer, int hash, void*) {
    return hash != 42 && (hash != 20 || hit_exists) && ((hash != 30 && hash != 40 && hash != 41) || layer == 3);
}
int __fastcall Count(void*, void*) {
    return 4;
}
int __fastcall Index(void*, std::string* name, void*) {
    return *name == "Visual" ? 0 : *name == "Add" ? 1 : *name == "Override" && override_exists ? 2 : -1;
}
float __fastcall Weight(void* value, int layer, void*) {
    Check(value == &animator);
    return layers.at(layer).weight;
}
void __fastcall SetWeight(void* value, int layer, float weight, void*) {
    Check(value == &animator);
    layers.at(layer).weight = weight;
    ++weight_writes;
    if (on_weight) {
        auto callback = std::move(on_weight);
        on_weight = {};
        callback();
    }
    if (fail_weight) {
        fail_weight = false;
        RaiseException(0xE0480001, 0, 0, nullptr);
    }
}
void __fastcall Play(void* value, int hash, int layer, float time, void*) {
    Check(value == &animator);
    layers.at(layer).state = hash;
    layers.at(layer).time = time;
    ++layer_plays;
}
void __fastcall Body(void* value, int hash, float, float time, void*) {
    Check(value == &style);
    layers[0].state = hash;
    layers[0].time = time;
    ++body_plays;
}
void __fastcall Fade(void* value, int hash, float, int layer, float time, float, void*) {
    Play(value, hash, layer, time, nullptr);
}
void __fastcall Info(void* value, int layer, int next, unsigned char* bytes, void*) {
    Check(value == &animator);
    const auto& data = layers.at(layer);
    const auto hash = next ? data.next : data.state;
    std::memset(bytes, 0, 36);
    std::memcpy(bytes, &hash, 4);
    std::memcpy(bytes + 12, &data.time, 4);
}
bool __fastcall Transition(void*, int layer, void*) {
    return layers.at(layer).transition;
}
struct Objects : api::ManagedObjects {
    TestAnimationComponentDeclarations declarations{{M(Keep), M(Target), M(Free), {}, {}}};
    api::ObjectResult ManagedType(const char* space, const char* name) override {
        using Role = api::AnimationComponent;
        if (declarations.Is(Role::Model, space, name))
            return {{true, 0}, &model_type};
        if (declarations.Is(Role::FreeStyle, space, name))
            return {{true, 0}, &style_type};
        if (declarations.Is(Role::Transform, space, name))
            return {{true, 0}, &transform_type};
        if (declarations.Is(Role::Motion, space, name))
            return {{true, 0}, &motion_type};
        if (declarations.Is(Role::Emotion, space, name))
            return {{true, 0}, &emotion_type};
        throw std::runtime_error("undeclared animation component type");
    }
    api::ObjectResult NewStringUtf8(const char* value) override {
        strings.push_back(std::make_unique<std::string>(value));
        return {{true, 0}, strings.back().get()};
    }
    api::ObjectResult AllocateObject(const char*, const char*) override {
        throw std::runtime_error("unexpected allocation");
    }
};
HitCommand Additive() {
    return {{{"Add", "Hit", 0.75f}, {"Override", "Hit", 0.75f}}};
}
struct EffectParams {
    unsigned id = 0;
    bool released = false;
    void* path = nullptr;
    void* target = nullptr;
    void* callback = nullptr;
};
std::vector<std::unique_ptr<EffectParams>> effect_params;
std::vector<std::unique_ptr<std::array<unsigned char, 128>>> effect_delegates;
int effect_starts = 0, effect_updates = 0, effect_removes = 0, effect_replays = 0;
bool block_effect = false;
void* __fastcall EffectMap(void* caster, void* path, void*) {
    Check(caster == owner);
    return path;
}
bool __fastcall EffectEmpty(std::string* path, void*) {
    return path->empty();
}
api::EffectReference __fastcall EffectAcquire(void* path, void*) {
    auto p = std::make_unique<EffectParams>();
    p->id = static_cast<unsigned>(effect_params.size() + 1);
    p->path = path;
    const auto ref = api::EffectReference{p->id, 0, p.get()};
    effect_params.push_back(std::move(p));
    return ref;
}
void __fastcall EffectSubmit(void* manager, EffectParams* p, void*) {
    Check(manager == &effect_manager && p->target == owner);
    ++effect_starts;
}
unsigned __fastcall EffectId(EffectParams* p, void*) {
    return p->id;
}
bool __fastcall EffectReleased(EffectParams* p, void*) {
    return p->released;
}
int __fastcall EffectState(EffectParams*, void*) {
    return 3;
}
void* __fastcall EffectInstance(EffectParams* p, void*) {
    return p;
}
bool __fastcall EffectRemove(void* manager, api::EffectReference ref, bool, void*) {
    Check(manager == &effect_manager);
    auto* p = static_cast<EffectParams*>(ref.parameters);
    p->released = true;
    ++effect_removes;
    return true;
}
void __fastcall EffectRelease(EffectParams* p, void*) {
    p->released = true;
}
std::shared_ptr<api::PresentationEffects> EffectServices(const api::ReferenceMethods& refs) {
    api::EffectAccessMethods methods;
    methods.map_path = M(EffectMap);
    methods.path_empty = M(EffectEmpty);
    methods.acquire = M(EffectAcquire);
    methods.submit = M(EffectSubmit);
    methods.id = M(EffectId);
    methods.released = M(EffectReleased);
    methods.state = M(EffectState);
    methods.instance = M(EffectInstance);
    methods.remove = M(EffectRemove);
    methods.release = M(EffectRelease);
    api::EffectRequestServices services{refs, api::EffectAccess(methods)};
    services.manager = [] { return api::ObjectResult{{true, 0}, &effect_manager}; };
    services.completion = {[] {
                               auto p = std::make_unique<std::array<unsigned char, 128>>();
                               p->fill(0);
                               auto* raw = p->data();
                               effect_delegates.push_back(std::move(p));
                               return api::ObjectResult{{true, 0}, raw};
                           },
                           {24, 40, 64}};
    services.configure_hit = [](void* raw, void* caster, void* target, void* callback,
                                const api::HitEffectPlacement& placement) {
        Check(caster == owner && target == owner && placement.offset.y == 0 && !placement.ignore_prefab_attach_point);
        auto* p = static_cast<EffectParams*>(raw);
        p->target = target;
        p->callback = callback;
        return api::EffectInputResult{{true, 0}, {true, 0}};
    };
    services.loaded_hit = [](auto, void*, auto) { return api::EffectControlResult{api::EffectControlState::Applied}; };
    services.update_hit = [](void* manager, api::EffectReference ref, const auto&) {
        Check(manager == &effect_manager && ref.id);
        ++effect_updates;
        return api::EffectControlResult{api::EffectControlState::Applied};
    };
    services.replay_hit = [](api::EffectReference ref, api::EffectMutexKind kind) {
        Check(ref.id && kind == api::EffectMutexKind::Default);
        ++effect_replays;
        return api::EffectControlResult{api::EffectControlState::Applied};
    };
    services.hit_mutex = [](void*, void* target, void*, const api::EffectMutexRule&, float,
                            const std::vector<api::EffectReference>& refs) {
        Check(target == owner);
        api::EffectMutexDecision result;
        result.kind.read.returned = true;
        result.kind.value = block_effect ? api::EffectMutexKind::Default : api::EffectMutexKind::None;
        if (block_effect && !refs.empty())
            result.replay = refs.front();
        return result;
    };
    services.hit_rules = [](const std::string&) { return api::ConfigValue<api::HitEffectRules>{true, {}, {}}; };
    services.match_hit = [](api::EffectReference ref, void*, void* path) {
        return api::Observed<bool>{*static_cast<std::string*>(static_cast<EffectParams*>(ref.parameters)->path) ==
                                       *static_cast<std::string*>(path),
                                   {true, 0}};
    };
    return std::make_shared<api::PresentationEffects>(std::move(services));
}
}
void TestHitRequests() {
    api::AnimationControlMethods methods;
    methods.hash = M(Hash);
    methods.has_state = M(Has);
    methods.play = M(Play);
    methods.free_style_state = M(Body);
    methods.cross_fade = M(Fade);
    methods.layer_count = M(Count);
    methods.layer_index = M(Index);
    methods.layer_weight = M(Weight);
    methods.set_layer_weight = M(SetWeight);
    methods.state_info = M(Info);
    methods.in_transition = M(Transition);
    methods.layer_name = M(LayerName);
    methods.ready_parameter_hash = M(Parameter);
    methods.float_set = M(SetFloat);
    methods.hit_motion = {M(Height), M(FallHeight), offsetof(Motion, clock), true};
    api::ModelAccessMethods models;
    models.animator = M(Animator);
    models.changing = M(Changing);
    api::ProfileInputMethods profiles;
    profiles.animator_initialized = M(Yes);
    const api::ReferenceMethods refs{M(Keep), M(Target), M(Free), {}, {}};
    const auto token = api::TrackedEntities().Track(owner);
    api::EntityAccessMethods entity_methods{M(Id), M(World), M(Yes), M(Component),  {},
                                            {},    {},       {},     M(EntityType), M(Alive)};
    api::ActorHitGeometryBindings spatial;
    spatial.placement.game_object = M(GameObject);
    spatial.scene.transform = M(RootTransform);
    spatial.geometry.transform_vector = M(TransformVector);
    spatial.geometry.logical_position_available = true;
    spatial.geometry.logical_position_offset = offsetof(Logical, position);
    auto session = std::make_unique<api::ActorAnimationSession>(
        token, refs, api::EntityAccess(entity_methods), api::ModelAccess(models), api::AnimationControl(methods),
        api::ProfileInputs(profiles), std::move(spatial));
    api::ComponentConfigMethods text_methods;
    text_methods.string_length = M(TextLength);
    text_methods.string_character = M(TextChar);
    api::ComponentConfig text(text_methods);
    ActionPlayback player(token, own_world, std::move(session), EffectServices(refs), &text);
    Objects objects;
    api::WorldFrame frame;
    frame.world = own_world;
    frame.disposing = {false, {true, 0}};
    double playback_rate = 1;
    auto tick = [&] {
        ++frame.sequence;
        player.Service(frame, true, objects, playback_rate);
    };
    auto attack = player.QueueAnimation({"Attack"});
    tick();
    tick();
    Check(attack->accepted && attack->entered);
    const auto initial_body = body_plays;
    auto directed = Additive();
    directed.angle = -90.f;
    auto hit = player.QueueHit(directed);
    tick();
    Check(!hit->accepted && layers[1].weight == 0.75f && layers[2].weight == 0.75f);
    Check(hit_angle == -90 && hit_angle_writes == 1);
    tick();
    tick();
    Check(hit->accepted && hit->entered && hit->expression_released && body_plays == initial_body &&
          !attack->cancelled);
    layers[1].time = layers[2].time = 1.5f;
    tick();
    tick();
    Check(hit->clip_complete && !hit->state_left && !hit->released); // time=1 is not a logical return
    layers[1].state = layers[2].state = 10;
    layers[2].transition = true;
    layers[2].next = 20;
    tick();
    tick();
    Check(!hit->state_left);
    layers[2].transition = false;
    tick();
    tick();
    Check(hit->state_left && hit->released && !hit->cancelled && layers[1].weight == 0.25f &&
          layers[2].weight == 0.25f);
    Check(body_plays == initial_body && layers[0].state == 10);

    // Optional override layer does not prevent an ordinary additive hit.
    override_exists = false;
    hit = player.QueueHit(Additive());
    tick();
    tick();
    tick();
    Check(hit->accepted && hit->entered);
    auto replacement = player.QueueHit(Additive());
    tick();
    tick();
    tick();
    Check(hit->released && hit->cancelled && replacement->accepted);
    player.ReleaseHit(hit->serial);
    tick();
    Check(!replacement->released && layers[1].weight == 0.75f);
    layers[1].weight = 0.6f;
    player.ReleaseHit(replacement->serial, true);
    tick();
    Check(layers[1].weight == 0.6f);
    override_exists = true;
    layers[1].weight = 0.25f;

    // A missing state must not first expose an old clip by raising its weight.
    hit_exists = false;
    const auto untouched = weight_writes;
    hit = player.QueueHit(Additive());
    tick();
    Check(hit->released && !hit->error.empty() && weight_writes == untouched);
    hit_exists = true;

    // Partial native failure restores only the writes actually owned.
    fail_weight = true;
    hit = player.QueueHit(Additive());
    tick();
    Check(hit->released && hit->cancelled && !hit->error.empty() && layers[1].weight == 0.25f &&
          layers[2].weight == 0.25f);
    // Invalid replacement leaves the existing receiver request intact.
    hit = player.QueueHit(Additive());
    tick();
    tick();
    auto invalid = Additive();
    invalid.layers.push_back(invalid.layers[0]);
    bool rejected = false;
    try {
        player.QueueHit(invalid);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    Check(rejected && !hit->cancelled);
    player.ReleaseHit(hit->serial);
    tick();

    // Expression is layered after body acceptance; its release is independent
    // of the still-playing hit and never resets the body animation.
    auto expressive = Additive();
    expressive.expression = "Face";
    expressive.expression_transition = 0.2f;
    layers[3].state = 10;
    layers[3].time = 0.4f;
    hit = player.QueueHit(expressive);
    tick();
    tick();
    tick();
    Check(layers[3].state == 30);
    player.ReleaseHitExpression(hit->serial);
    tick();
    Check(layers[3].state == 10 && !hit->released);
    tick();
    Check(layers[3].state == 10);
    player.ReleaseHit(hit->serial);
    tick();
    hit = player.QueueHit(expressive);
    tick();
    tick();
    tick();
    auto face = player.QueueAnimation({"Face", 3});
    tick();
    tick();
    player.ReleaseHit(hit->serial);
    tick();
    Check(layers[3].state == 30 && face->entered && !face->cancelled);
    layers[3].state = 10;
    expressive.expression_seconds = .5f;
    hit = player.QueueHit(expressive);
    tick();
    tick();
    tick();
    Check(layers[3].state == 30);
    frame.delta_time = .25f;
    tick();
    Check(layers[3].state == 30);
    player.Service(frame, true, objects);
    Check(layers[3].state == 30); // same frame does not advance timer twice
    tick();
    Check(layers[3].state == 10 && !hit->released);
    frame.delta_time = 0;
    player.ReleaseHit(hit->serial);
    tick();
    // The same authored duration uses the receiving actor's playback rate;
    // neither the world delta nor body/layer ownership is modified.
    hit = player.QueueHit(expressive);
    tick();
    tick();
    tick();
    Check(layers[3].state == 30);
    playback_rate = 2;
    frame.delta_time = .125f;
    tick();
    Check(layers[3].state == 30);
    player.Service(frame, true, objects, playback_rate);
    Check(layers[3].state == 30);
    tick();
    Check(layers[3].state == 10 && hit->expression_released && !hit->released && frame.delta_time == .125f);
    player.ReleaseHit(hit->serial);
    tick();
    Check(hit->released);
    playback_rate = 1;
    frame.delta_time = 0;

    HitCommand heavy;
    heavy.layers = {{"Add", "", 0}, {"Override", "", 0}};
    heavy.body = HitBody{"Visual", "Heavy"};
    hit = player.QueueHit(heavy);
    tick();
    tick();
    tick();
    Check(hit->accepted && hit->entered && attack->cancelled && layers[0].state == 21);
    layers[0].time = 2;
    tick();
    tick();
    Check(hit->clip_complete && !hit->released && layers[0].state == 21);
    const auto heavy_plays = body_plays;
    player.ReleaseHit(hit->serial);
    tick();
    Check(hit->released && body_plays == heavy_plays && layers[1].weight == 0.25f); // no fabricated StandBy
    // A new body command owns the successor; old retirement must not cancel it.
    hit = player.QueueHit(heavy);
    tick();
    tick();
    auto successor = player.QueueAnimation({"Attack"});
    tick();
    tick();
    Check(hit->released && hit->cancelled && successor->entered && !successor->cancelled && layers[0].state == 10);

    // Reentrant cancellation while writing a weight is drained before another
    // layer/animation can be submitted. Servicing the player cannot recurse.
    on_weight = [&] {
        player.CancelPending();
        player.Service(frame, true, objects);
    };
    const auto before_layers = layer_plays;
    hit = player.QueueHit(Additive());
    tick();
    Check(hit->released && hit->cancelled && layer_plays == before_layers && layers[1].weight == 0.25f);
    hit = player.QueueHit(Additive());
    tick();
    replaced = true;
    const auto before_weights = weight_writes;
    tick();
    Check(hit->released && hit->cancelled && weight_writes == before_weights);
    replaced = false;
    layers[1].weight = layers[2].weight = 0.25f;
    hit = player.QueueHit(Additive());
    tick();
    player.CancelPending();
    player.ServiceEffects(frame);
    Check(hit->released && layers[1].weight == 0.25f);
    hit = player.QueueHit(Additive());
    tick();
    auto foreign = frame;
    foreign.world = reinterpret_cast<api::GameWorld*>(1);
    const auto own_weights = weight_writes;
    player.Service(foreign, true, objects);
    Check(hit->cancelled && !hit->released && weight_writes == own_weights);
    player.ServiceEffects(frame);
    Check(hit->released && layers[1].weight == 0.25f);
    // Cross-actor producers post values; this receiver resolves and plays them
    // only on its own frame. Admission is not a claim of visual completion.
    player.ConfigureHitResolver([](const HitInput& input) {
        Check(input.animation == "DamageHit");
        return HitResolution{true, Additive()};
    });
    auto inbox = player.IncomingHits();
    auto delivered = inbox->Post({"DamageHit"});
    player.Service(foreign, true, objects);
    Check(!delivered.status->cancelled && !delivered.status->delivered);
    tick();
    Check(delivered.status->delivered && delivered.status->playback);
    tick();
    tick();
    Check(delivered.status->playback->accepted && delivered.status->playback->entered);
    delivered.cancel();
    tick();
    Check(!delivered.status->playback->cancelled);
    player.ReleaseHit(delivered.status->playback->serial);
    tick();
    Check(delivered.status->playback->released);
    // The production policy assembles inputs from this receiver's catalog,
    // control sources and actual animation session, not an injected HitCommand.
    player.ConfigureHitResolver({});
    player.SetPresentationDomain(true);
    std::vector<ActorVoiceCue> voice_cues;
    bool voice_fault = false;
    player.ConfigureVoices(1217, "receiver",
                           [&](const ActorVoiceCue& input) -> std::unique_ptr<api::OwnedSoundRequest> {
                               Check(input.actor.token.entity == owner && input.actor.world == own_world &&
                                     input.avatar_id == 1217 && input.source_config == "receiver");
                               Check(input.cue == api::VoiceCue::LightHit);
                               voice_cues.push_back(input);
                               if (voice_fault)
                                   throw std::runtime_error("hit voice fixture failure");
                               return {};
                           },
                           {});
    auto character = std::make_shared<api::CharacterHitInput>();
    character->loaded = true;
    character->source = "receiver";
    character->resilience = {true, 3, {}};
    character->repeat_occur_at = {true, -1, {}};
    character->phase_present = {true, false, {}};
    character->shape = {true, {api::HitShapeKind::Capsule, .3f, 1.5f, {}}, {}};
    character->hit_box_attach_point = {true, "", {}};
    auto weights = std::make_shared<api::HitAnimationDefaults>();
    weights->resilience_weights = {true, {.25f, .5f, .75f, 1.f}, {}};
    weights->avatar_weight = {true, .6f, {}};
    weights->minimum_frame_interval = {true, 2, {}};
    auto defaults = std::make_shared<api::VisualReactionInput>();
    defaults->present = true;
    defaults->hit = {true, "Hit", {}};
    defaults->hit_h = {true, "Heavy", {}};
    auto presentation = std::make_shared<api::ReadyAnimationDefaults>();
    presentation->hit_additive_layer = {true, "Add", {}};
    presentation->hit_override_layer = {true, "Override", {}};
    presentation->hit_expression = {true, "Face", {}};
    presentation->default_expression = {true, "Neutral", {}};
    auto visuals = std::make_shared<api::VisualConfigSources>();
    visuals->context_available = true;
    visuals->defaults.loaded = true;
    visuals->defaults.behaviors.enumerated = true;
    visuals->expression_transition = {true, .2f, {}};
    api::VisualBehaviorInput return_visual;
    return_visual.flag = {true, 99, {}};
    return_visual.emotion = {true, "ChangedFace", {}};
    return_visual.loop.resolved_state = {true, "Idle", {}};
    visuals->defaults.behaviors.rows.push_back(return_visual);
    bool available = false;
    HitControlInputs controls;
    player.ConfigureReceiverPolicy(
        [&](bool visual) -> std::optional<HitCatalogInputs> {
            if (!available)
                return {};
            return HitCatalogInputs{character, weights, defaults, presentation, visual ? visuals : nullptr};
        },
        [&](bool) { return controls; });
    auto actual = inbox->Post({"DamageHit"});
    tick();
    Check(!actual.status->delivered && actual.status->error.empty());
    available = true;
    tick();
    tick();
    if (!actual.status->error.empty())
        throw std::runtime_error("receiver delivery: " + actual.status->error);
    if (actual.status->playback && !actual.status->playback->error.empty())
        throw std::runtime_error("receiver playback: " + actual.status->playback->error);
    Check(actual.status->playback && actual.status->playback->accepted);
    Check(voice_cues.size() == 1 && voice_cues.back().state == "Hit");
    Check(layers[1].weight == .6f && layers[3].state == 30);
    auto repeated = inbox->Post({"DamageHit"});
    tick();
    Check(repeated.status->delivered && repeated.status->ignored && voice_cues.size() == 1);
    HitInput effect_hit;
    effect_hit.origin = HitOrigin{{2, 3, 7}, true, {token, own_world}};
    effect_hit.effect = HitEffectInput{"hit-path"};
    auto only_effect = inbox->Post(effect_hit);
    tick();
    if (!only_effect.status->error.empty())
        throw std::runtime_error(only_effect.status->error);
    Check(only_effect.status->delivered && only_effect.status->ignored && effect_starts == 1 && effect_removes == 0);
    effect_hit.animation = "DamageHit";
    auto first_effect = inbox->Post(effect_hit);
    tick();
    auto throttled_effect = inbox->Post(effect_hit);
    tick();
    Check(throttled_effect.status->ignored && effect_starts == 3 && throttled_effect.status->effects_delivered);
    HitEffectOverride replacement_effect;
    replacement_effect.path = "override";
    replacement_effect.replay_on_mutex = true;
    controls.effects = {replacement_effect};
    auto override_hit = inbox->Post(effect_hit);
    tick();
    Check(override_hit.status->error.empty() && effect_starts == 4);
    auto update_hit = inbox->Post(effect_hit);
    tick();
    Check(update_hit.status->error.empty() && effect_starts == 4 && effect_updates == 1);
    block_effect = true;
    auto replay_hit = inbox->Post(effect_hit);
    tick();
    Check(replay_hit.status->error.empty() && effect_starts == 4 && effect_replays == 1);
    block_effect = false;
    player.CancelPending();
    tick();
    Check(effect_removes == 0); // action cancellation does not own delivered incoming effects
    player.SetPresentationDomain(false);
    tick();
    Check(effect_removes == 4);
    player.SetPresentationDomain(true);
    controls.effects.clear();
    tick();
    tick();
    controls.force_heavy = true;
    const auto before_heavy_voice = voice_cues.size();
    auto forced = inbox->Post({"DamageHit"});
    tick();
    tick();
    tick();
    Check(forced.status->playback && forced.status->playback->entered && layers[0].state == 21 &&
          layers[1].weight == 0);
    Check(voice_cues.size() == before_heavy_voice); // heavy native VO is not doubled by LightHit
    Check(forced.status->playback->body_end_reached && !forced.status->playback->clip_complete);
    player.SetCompletionState("Idle");
    // Expression returns to the latest source, not its pre-hit snapshot. The
    // body remains independent and does not return just because its clip ended.
    controls.visual_flag = 99;
    frame.delta_time = .5f;
    tick();
    frame.delta_time = 0;
    Check(forced.status->playback->expression_released && layers[3].state == 41 && !forced.status->playback->released);
    layers[0].time = 2;
    tick();
    tick();
    Check(!forced.status->playback->body_returned);
    layers[0].state = 10;
    tick();
    tick();
    Check(!forced.status->playback->released); // unrelated successor is not reset
    layers[0].state = 11;
    motion_present = true;
    motion_height = .05f;
    tick();
    Check(!forced.status->playback->released);
    motion_height = 0;
    alive_state = 3;
    tick();
    Check(!forced.status->playback->released);
    const auto returned_body_plays = body_plays;
    alive_state = 1;
    tick();
    Check(forced.status->playback->body_returned && forced.status->playback->released &&
          !forced.status->playback->cancelled);
    Check(body_plays == returned_body_plays && layers[0].state == 11); // observe native successor, do not replay it
    motion_present = false;
    controls = {};
    tick();
    tick();
    auto return_wait = inbox->Post({"DamageHit"});
    tick();
    tick();
    tick();
    available = false;
    frame.delta_time = .5f;
    tick();
    Check(layers[3].state == 30 && !return_wait.status->playback->expression_released);
    available = true;
    tick();
    frame.delta_time = 0;
    Check(layers[3].state == 40 && return_wait.status->playback->expression_released);
    layers[1].state = layers[2].state = 10;
    tick();
    tick();
    Check(return_wait.status->playback->released && layers[0].state == 11);
    player.CancelPending();
    tick();
    controls = {};
    character->resilience.available = false;
    auto unread = inbox->Post({"DamageHit"});
    const auto writes = weight_writes;
    tick();
    Check(!unread.status->error.empty() && weight_writes == writes);
    character->resilience.available = true;
    // Explicit body/face writes do not consult gameplay life or a receiver's
    // current visual flag. The script supplied both destinations.
    player.CancelPending();
    tick();
    available = true;
    controls = {};
    motion_present = true;
    motion_height = .4f;
    AnimationCommand skill_body{"Attack", 0, 0, 0, .58f};
    skill_body.track = AnimationTrack{3, 0, {}, false};
    auto skill = player.QueueAnimation(skill_body);
    tick();
    tick();
    layers[0].time = .7f;
    controls.visual_flag = 99;
    AnimationCommand return_body{"Idle", 0, 0, 0, 0.f};
    return_body.track = AnimationTrack{1, 6, {}, false, 1.f};
    return_body.restart = false;
    auto recovery = player.QueueAnimation(return_body);
    const auto recovery_plays = body_plays;
    alive_state = 3;
    auto expression = player.QueueExpression("Neutral", .2f);
    tick();
    alive_state = 1;
    Check(expression->accepted && expression->complete && !expression->entered && !expression->progress &&
          layers[3].state == 40);
    Check(!recovery->accepted && body_plays == recovery_plays);
    tick();
    Check(!recovery->accepted && layers[0].state == 10);
    layers[0].time = 1.1f;
    tick();
    tick();
    tick();
    Check(recovery->accepted && layers[0].state == 11 && body_plays == recovery_plays + 1);
    player.ReleaseAnimation(skill->serial);
    player.ReleaseAnimation(recovery->serial);
    player.ReleaseExpression(expression->serial);
    Check(layers[3].state == 40); // observation release does not restore the skill face
    auto no_replay = player.QueueAnimation(return_body);
    tick();
    Check(no_replay->accepted && body_plays == recovery_plays + 1);
    player.ReleaseAnimation(no_replay->serial);
    // A moving skill can finish its task list before its return range. The
    // explicitly authored previous_end retains even an End=0 record through
    // the required clip boundary, independent of enum values or skill names.
    auto moving_body = skill_body;
    moving_body.normalized_end = 0.f;
    auto moving_skill = player.QueueAnimation(moving_body);
    tick();
    tick();
    layers[0].time = .782f;
    auto moving_return = player.QueueAnimation(return_body);
    tick();
    tick();
    Check(!moving_return->accepted && layers[0].state == 10);
    layers[0].time = .91f;
    tick();
    Check(!moving_return->accepted && layers[0].state == 10);
    layers[0].time = 1.01f;
    tick();
    tick();
    tick();
    Check(moving_return->accepted && layers[0].state == 11);
    player.ReleaseAnimation(moving_skill->serial);
    player.ReleaseAnimation(moving_return->serial);
    motion_present = false;
    controls = {};
    skill = player.QueueAnimation(skill_body);
    tick();
    tick();
    changing = true;
    auto waiting_expression = player.QueueExpression("ChangedFace", .2f);
    tick();
    Check(!waiting_expression->accepted);
    player.ReleaseExpression(waiting_expression->serial);
    changing = false;
    tick();
    Check(waiting_expression->released && !waiting_expression->accepted && layers[3].state == 40);
    auto ambiguous = player.QueueExpression("Attack", .2f);
    const auto before_ambiguous = layer_plays;
    tick();
    Check(ambiguous->released && !ambiguous->accepted && !ambiguous->error.empty() && layer_plays == before_ambiguous);
    auto missing_face = player.QueueExpression("MissingFace", .2f);
    tick();
    Check(missing_face->skipped && missing_face->released && !missing_face->accepted && !missing_face->complete &&
          missing_face->error.empty() && !missing_face->skip_reason.empty() && layer_plays == before_ambiguous);
    // Cancelling pending writes keeps the actor's expression player reusable;
    // it is not a close and must not consume a later setter's receipt.
    changing = true;
    auto cancelled_face = player.QueueExpression("Neutral", .2f);
    tick();
    Check(!cancelled_face->accepted);
    player.CancelPending();
    Check(cancelled_face->cancelled && cancelled_face->released && !cancelled_face->complete);
    changing = false;
    auto later_face = player.QueueExpression("Neutral", .2f);
    tick();
    Check(later_face->serial > cancelled_face->serial && later_face->accepted && later_face->complete &&
          !later_face->cancelled && layers[3].state == 40);
    player.ReleaseExpression(later_face->serial);
    VisualProgramServices visual_services;
    visual_services.effects =
        std::make_shared<api::PresentationEffects>(api::EffectRequestServices{{}, api::EffectAccess({}), {}, {}, {}});
    visual_services.create = [](auto&, const auto&, const auto&) -> VisualEffectId {
        throw std::runtime_error("unexpected recovery effect");
    };
    auto visual_scene = std::make_shared<VisualProgramPlayer>(std::make_shared<VisualProgram>(), visual_services);
    TaskPresentationServices ports;
    ports.animation = [&](const auto& participant) -> std::optional<TaskAnimationEndpoint> {
        Check(participant.name == "body");
        return BindTaskAnimation(player);
    };
    TaskSequenceInput recovery_plan_input;
    TaskAnimationCommand body_command{{"body"}, "Attack", 0, 0, 0, .58f};
    body_command.track = AnimationTrack{3, 0, {}, false};
    TaskAnimationCommand explicit_return{{"body"}, "Idle", 0, 0, 0, 0.f};
    explicit_return.track = AnimationTrack{12, 14, {}, false, 1.f};
    explicit_return.restart = false;
    recovery_plan_input.nodes = {{"body", TaskSubmit{"body", body_command}},
                            {"entry-finish", TaskWaitRequest{"body", TaskWaitSignal::Progress, .7}},
                            {"face", TaskSubmit{"face", TaskExpressionCommand{"Neutral", .2f}}},
                            {"return", TaskSubmit{"return", explicit_return}},
                            {"admitted", TaskWaitRequest{"return", TaskWaitSignal::RequestComplete}},
                            {"entry", TaskSerial{{0, 1, 2, 3, 4}}}};
    recovery_plan_input.entry = 5;
    auto recovery_plan = std::make_shared<TaskSequence>(recovery_plan_input);
    TaskPresentation recovery_task({300}, token.generation, recovery_plan, visual_scene, {}, ports);
    auto service_task = [&] {
        tick();
        recovery_task.Service(.01, frame.sequence);
    };
    recovery_task.Service(0, ++frame.sequence);
    service_task();
    service_task();
    layers[0].time = .71f;
    service_task();
    service_task();
    service_task();
    if (!recovery_task.Status().issues.empty())
        throw std::runtime_error("recovery sequence: " + recovery_task.Status().issues.front());
    if (layers[0].state != 10 || recovery_task.Drained()) {
        std::string detail = "explicit return boundary body=" + std::to_string(layers[0].state) +
                             " drained=" + std::to_string(recovery_task.Drained());
        for (const auto& waiting : recovery_task.Status().waiting)
            detail += " wait=" + waiting;
        for (const auto& row : recovery_task.Requests())
            detail += " request=" + row.name + " accepted=" + std::to_string(row.state.request_complete) +
                      " drained=" + std::to_string(row.state.drained);
        throw std::runtime_error(detail);
    }
    // The body record remains until its explicit successor can be accepted.
    service_task();
    Check(layers[0].state == 10 && !recovery_task.Drained());
    layers[0].time = 1.1f;
    service_task();
    service_task();
    service_task();
    service_task();
    Check(layers[0].state == 11 && recovery_task.Drained() && recovery_task.Status().issues.empty());
    visual_scene->End();
    visual_scene->Service();
    player.CancelPending();
    tick();
    // Exercise the manual catalog's real self-hit -> performance -> idle chain.
    // Delivery occurs before the receiving player's animation Service, so it
    // must not authorize Idle on that same admission boundary.
    player.ConfigureHitResolver({});
    player.SetPresentationDomain(true);
    available = true;
    controls = {};
    frame.delta_time = .05f;
    std::shared_ptr<const HitDeliveryStatus> self_delivery;
    visual_services.hit = [&](auto&, const std::string& target, const HitInput& input) -> std::optional<HitDelivery> {
        Check(target == "Caster");
        auto value = inbox->Post(input);
        self_delivery = value.status;
        return value;
    };
    auto self_scene = std::make_shared<VisualProgramPlayer>(std::make_shared<VisualProgram>(), visual_services);
    TaskSequenceInput self_plan_input;
    self_plan_input.nodes = {{"hit", TaskSubmit{"hit", TaskHitCommand{"Caster", {"Hit"}}}},
                        {"performance", TaskWaitRequest{"hit", TaskWaitSignal::PerformanceComplete}},
                        {"idle", TaskSubmit{"idle", TaskAnimationCommand{{"body"}, "Idle"}}},
                        {"admitted", TaskWaitRequest{"idle", TaskWaitSignal::RequestComplete}},
                        {"entry", TaskSerial{{0, 1, 2, 3}}}};
    self_plan_input.entry = 4;
    auto self_plan = std::make_shared<TaskSequence>(self_plan_input);
    const auto before_idle = body_plays;
    const auto add_baseline = layers[1].weight, override_baseline = layers[2].weight;
    TaskPresentation self_task({301}, token.generation, self_plan, self_scene, {}, ports);
    auto service_self = [&] {
        tick();
        self_task.Service(.01, frame.sequence);
    };
    self_task.Service(0, ++frame.sequence);
    service_self();
    service_self();
    service_self();
    Check(self_delivery && self_delivery->delivered && self_delivery->playback && self_delivery->playback->entered);
    Check(!self_task.Drained() && body_plays == before_idle && layers[1].state == 20 && layers[1].weight == .6f &&
          layers[3].state == 30);
    layers[1].time = layers[2].time = 1.1f;
    service_self();
    service_self();
    Check(self_delivery->playback->clip_complete && !self_delivery->playback->expression_released &&
          layers[3].state == 30 && body_plays == before_idle && !self_task.Drained());
    frame.delta_time = .25f;
    service_self();
    service_self();
    service_self();
    service_self();
    Check(self_task.Drained() && self_task.Status().issues.empty() && layers[0].state == 11 &&
          body_plays == before_idle + 1);
    Check(self_delivery->playback->clip_complete && self_delivery->playback->released &&
          !self_delivery->playback->cancelled);
    Check(layers[1].weight == add_baseline && layers[2].weight == override_baseline && layers[3].state == 40);
    frame.delta_time = 0;
    self_scene->End();
    self_scene->Service();
    player.CancelPending();
    tick();
    tick();
    tick();
    defaults->fly_hit = {true, "Hit", {}};
    motion_present = true;
    motion_height = .4f;
    voice_fault = true;
    const auto before_air_voice = voice_cues.size();
    auto airborne = inbox->Post({"DamageHit"});
    tick();
    tick();
    tick();
    Check(airborne.status->playback && airborne.status->playback->accepted &&
          voice_cues.size() == before_air_voice + 1);
    Check(!player.Status().voice_error.empty()); // voice failure leaves accepted Fly_Hit body alone
    voice_fault = false;
    motion_present = false;
    motion_height = 0;
    player.CancelPending();
    tick();
    hit = player.QueueHit(Additive());
    tick();
    Check(player.Close(frame));
    Check(hit->released && layers[1].weight == 0.25f && roots.empty());
    bool closed = false;
    try {
        inbox->Post({"DamageHit"});
    } catch (const std::logic_error&) {
        closed = true;
    }
    Check(closed);
}
