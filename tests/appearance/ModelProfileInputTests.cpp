#include "../support/LegacyScriptFixture.hpp"
#include "../support/ModelServicesFixture.hpp"
#include "../support/ModelInputDeclarations.hpp"
#include <hksr/model/ModelAccess.hpp>
#include <hksr/framework/presentation/scene/VisualProgramPlayer.hpp>
#include <hksr/presentation/PresentationBindings.hpp>
#include <hksr/model/ModelProfileInputs.hpp>
#include <hksr/model/ModelProfileRequest.hpp>
#include <hksr/framework/presentation/model/ModelActionHandoff.hpp>
#include <hksr/framework/catalog/SourceCatalog.hpp>
#include <hksr/framework/character/CharacterSession.hpp>
#include <hksr/framework/presentation/scene/VisualProgramBindings.hpp>
#include <hksr/framework/character/SceneCharacters.hpp>
#include <hksr/framework/layout/SceneLayout.hpp>
#include <hksr/framework/host/FrameworkHost.hpp>
#include <Windows.h>
#include <algorithm>
#include <cstddef>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <cstring>
#include <thread>
#include <type_traits>
#include "../../src/character/SkinEventPreparation.hpp"

static_assert(!std::is_copy_constructible_v<hksr::api::ModelProfileInputs>);
static_assert(!std::is_copy_constructible_v<hksr::api::ModelProfileRequest>);

namespace {
template <class F> __declspec(noinline) void TestSection(F&& run) {
    run();
}
using namespace hksr;
struct Value {
    std::string text;
    std::vector<void*> items;
    void* child = nullptr;
} baseline_path, baseline_entity_config, baseline_events, baseline_layers, string_type, model, operation;
struct Consumer {
    void* unused[3]{};
    void* paths = &baseline_events;
    void* layers = &baseline_layers;
} consumer;
std::vector<std::unique_ptr<Value>> values;
std::map<int, void*> roots;
int info, next = 0, initializes = 0;
bool constructor_fault = false, array_fault = false, null_config = false;
int value_applies = 0, value_restores = 0, value_active = 0, value_ended = 0;
bool value_fail = false, require_value_on_die = false;
std::string value_path;
struct ValueLease : api::EventValueLease {
    bool active = false;
    std::string error = "fixture value apply failure";
    bool Apply(void* component, const std::string& path) override {
        CheckInput(component, path);
        active = true;
        ++value_active;
        ++value_applies;
        value_path = path;
        return !value_fail;
    }
    static void CheckInput(void* component, const std::string& path) {
        if (!component || path.empty())
            throw std::runtime_error("value fixture missing source");
    }
    bool Restore() override {
        if (active) {
            active = false;
            --value_active;
            ++value_restores;
        }
        return true;
    }
    bool Close(bool ended) override {
        if (ended && active) {
            active = false;
            --value_active;
            ++value_ended;
        } else
            Restore();
        return true;
    }
    const std::string& Error() const override {
        return error;
    }
};
bool keep_fail = false;
int model_type, style_type, event_type, data_type, style, npc_manager, npc_definition, npc_row;
int gaze_controller_type, gaze_driver_type, gaze_type, gaze_component, gaze_head;
Value gaze_components;
bool gaze_enabled = false, gaze_active = false, gaze_automatic = true, gaze_fading = false;
int gaze_follow_calls = 0;
void* current_event_component = nullptr;
bool controller_installed = false;
void* current_path = &baseline_path;
void* current_config = &baseline_entity_config;
void CheckAt(bool value, int line) {
    if (!value) {
        std::cerr << "model profile input test line " << line << '\n';
        throw std::runtime_error("model profile input transaction failed");
    }
}
#define Check(...) CheckAt((__VA_ARGS__), __LINE__)
template <class T> api::Method M(T f) {
    return {reinterpret_cast<void*>(f), &info};
}
bool Rooted(void* object) {
    for (const auto& item : roots)
        if (item.second == object)
            return true;
    return false;
}
int __fastcall Keep(void* object, int zero, int kind, void*) {
    Check(!zero && kind == 2);
    if (keep_fail) {
        keep_fail = false;
        return 0;
    }
    roots[++next] = object;
    return next;
}
void* __fastcall Target(int id, void*) {
    return roots.at(id);
}
void __fastcall Free(int id, void*) {
    roots.erase(id);
}
void* __fastcall Path(void* object, void*) {
    Check(object == &model && Rooted(object));
    return current_path;
}
void* __fastcall Config(void* object, void*) {
    Check(object == &model);
    return null_config ? nullptr : current_config;
}
void __fastcall Initialize(void* object, void*) {
    Check(object == &operation && Rooted(object));
    ++initializes;
    if (constructor_fault)
        RaiseException(0xE06C0001, 0, 0, nullptr);
}
bool __fastcall Initialized(void* object, void*) {
    Check(object == &model);
    return true;
}
void* __fastcall Array(void* type, int size, void*) {
    Check(type == &string_type && Rooted(type));
    values.push_back(std::make_unique<Value>());
    auto* value = values.back().get();
    value->items.resize(size);
    return value;
}
void __fastcall Set(Value* array, Value* element, int index, void*) {
    Check(Rooted(array) && Rooted(element));
    array->items.at(index) = element;
    if (array_fault)
        RaiseException(0xE06C0002, 0, 0, nullptr);
}
struct Objects : api::ManagedObjects {
    TestModelInputDeclarations declarations;
    bool allow_gaze_driver = false;
    std::optional<TestManagedClass> gaze_driver;
    api::ObjectResult NewStringUtf8(const char* text) override {
        values.push_back(std::make_unique<Value>());
        values.back()->text = text;
        return {{true, 0}, values.back().get()};
    }
    api::ObjectResult ManagedType(const char* ns, const char* name) override {
        const TestManagedClass identity{ns, name};
        if (identity == declarations.data)
            return {{true, 0}, &data_type};
        if (identity == declarations.events)
            return {{true, 0}, &event_type};
        if (std::string(ns) == "RPG.GameCore") {
            if (std::string(name) == "CharacterModelComponent")
                return {{true, 0}, &model_type};
            if (std::string(name) == "FreeStyleComponent")
                return {{true, 0}, &style_type};
            if (std::string(name) == "AdventureCharacterController")
                return {{true, 0}, &gaze_controller_type};
        }
        if (std::string(ns) == "RPG.Client" && std::string(name) == "LookAtIK")
            return {{true, 0}, &gaze_type};
        if (identity == TestManagedClass{"System", "String"})
            return {{true, 0}, &string_type};
        if (allow_gaze_driver) {
            if (!gaze_driver)
                gaze_driver = identity;
            Check(gaze_driver == identity);
            return {{true, 0}, &gaze_driver_type};
        }
        throw std::runtime_error("undeclared fixture component type");
    }
    api::ObjectResult AllocateObject(const char* ns, const char* name) override {
        Check(TestManagedClass(ns, name) == declarations.operation);
        return {{true, 0}, &operation};
    }
};
int entity, world, animator, art;
auto* entity_ptr = reinterpret_cast<api::GameEntity*>(&entity);
auto* world_ptr = reinterpret_cast<api::GameWorld*>(&world);
auto* model_ptr = reinterpret_cast<api::CharacterModelComponent*>(&model);
int load_state = 5, begins = 0, cancels = 0, restores = 0, event_sets = 0;
bool changing = false, animator_present = true, animator_ready = true, can_change = true;
bool begin_fault = false, cancel_fault = false, event_noop = false;
bool resolved_alias = false, submit_wrong = false;
bool dispose_on_begin = false;
bool async_requested = false, async_pending = false, pending_fault = false;
void* async_path = nullptr;
Value alias_path;
void __fastcall Begin(void* op, void* owner, api::ModelInputs inputs, bool asynchronous, void* callback, void*) {
    Check(!controller_installed);
    Check(op == &operation && owner == entity_ptr && Rooted(op) && Rooted(owner) && asynchronous == async_requested &&
          !callback);
    if (asynchronous) {
        ++begins;
        async_pending = true;
        async_path = inputs.model_path;
        if (begin_fault)
            RaiseException(0xE06C0010, 0, 0, nullptr);
        return;
    }
    ++begins;
    current_path =
        submit_wrong ? static_cast<void*>(&baseline_path) : (resolved_alias ? &alias_path : inputs.model_path);
    current_config = nullptr;
    if (dispose_on_begin)
        api::TrackedEntities().Disposed(api::TrackedEntities().Current(entity_ptr));
    // Deliberately leaves events untouched: same-path / empty-array behavior
    // means successful Begin cannot stand in for post-load input adoption.
    if (begin_fault)
        RaiseException(0xE06C0010, 0, 0, nullptr);
}
void __fastcall Cancel(void* op, void*) {
    Check(op == &operation);
    ++cancels;
    if (cancel_fault)
        RaiseException(0xE06C0011, 0, 0, nullptr);
    async_pending = false;
    async_path = nullptr;
}
bool __fastcall Pending(void* op, void*) {
    Check(op == &operation && Rooted(op));
    if (pending_fault)
        RaiseException(0xE06C0012, 0, 0, nullptr);
    return async_pending;
}
bool __fastcall CanChange(void* m, void* path, void** resolved, void*) {
    Check(m == &model);
    *resolved = resolved_alias ? &alias_path : path;
    return can_change;
}
void __fastcall Restore(void* m, void* path, void* config, void* prepared_root, void*) {
    Check(!controller_installed);
    Check(m == &model && !prepared_root);
    ++restores;
    current_path = path;
    current_config = config;
}
bool __fastcall Equals(void* a, void* b, void*) {
    return static_cast<Value*>(a)->text == static_cast<Value*>(b)->text;
}
void* __fastcall Art(void*, void*) {
    return &art;
}
void* __fastcall Animator(void*, void*) {
    return animator_present ? &animator : nullptr;
}
int __fastcall LoadState(void*, void*) {
    return load_state;
}
bool __fastcall Changing(void*, void*) {
    return changing;
}
bool __fastcall Alive(void*, void*) {
    return true;
}
bool __fastcall AnimatorReady(void*, void*) {
    return animator_ready;
}
api::GameEntity* __fastcall Create(void*, void*, void*, void*, bool, void*) {
    return entity_ptr;
}
unsigned __fastcall EntityId(void*, void*) {
    return 55;
}
api::GameWorld* __fastcall EntityWorld(void*, void*) {
    return world_ptr;
}
void* __fastcall Component(void* e, void* type, void*) {
    Check(e == entity_ptr);
    if (type == &model_type)
        return &model;
    if (type == &event_type)
        return current_event_component;
    if (type == &gaze_controller_type || type == &gaze_driver_type)
        return e;
    Check(type == &style_type);
    return &style;
}
void __fastcall Kill(void* e, bool immediate, void* metadata) {
    Check(e == entity_ptr && immediate && metadata == &info);
}
void __fastcall Die(void* w, void* e, void* killer, void* metadata) {
    if (require_value_on_die)
        Check(value_active == 1);
    Check(w == world_ptr && e == entity_ptr && !killer && metadata == &info && !controller_installed);
    api::TrackedEntities().Disposed(api::TrackedEntities().Current(entity_ptr));
}
int npc_plays = 0, battle_plays = 0, animator_plays = 0, controller_sets = 0, controller_clears = 0;
bool state_exists = true, requeue_battle = false;
bool requeue_on_controller = false;
bool default_absent = false;
int observed_hash = 0, state_reads = 0;
float observed_time = 0;
framework::ModelActionHandoff* active_handoff = nullptr;
int __fastcall Hash(Value* name, void*) {
    Check(name->text == "SomeState" || name->text == "OtherState");
    return name->text == "SomeState" ? 37 : 41;
}
bool __fastcall Has(void* a, int layer, int hash, void*) {
    Check(a == &animator && layer == 0 && (hash == 37 || hash == 41));
    return state_exists && !(default_absent && hash == 41);
}
void __fastcall ObserveState(void* a, int layer, int index, unsigned char* bytes, void* metadata) {
    Check(a == &animator && layer == 0 && index == 0 && metadata == &info);
    ++state_reads;
    std::memset(bytes, 0, 36);
    std::memcpy(bytes, &observed_hash, 4);
    std::memcpy(bytes + 12, &observed_time, 4);
}
void __fastcall NpcPlay(void* object, unsigned id, void*) {
    Check(object == &style && id == 18);
    ++npc_plays;
}
void __fastcall AnimatorPlay(void* a, int, int, float, void*) {
    Check(a == &animator);
    ++animator_plays;
}
void __fastcall BattlePlay(void* object, int hash, float first, float second, void*) {
    Check(object == &style && (hash == 37 || hash == 41) && first == 0 && second == 0);
    ++battle_plays;
    if (requeue_battle) {
        requeue_battle = false;
        active_handoff->Queue(framework::ActionDomain::Exploration, framework::NpcMotion{18});
    }
}
void __fastcall ControllerPath(void* m, Value* path, void* key, void*) {
    Check(m == &model && !key);
    if (path) {
        Check(path->text == "Controller/exploration");
        ++controller_sets;
        controller_installed = true;
        if (requeue_on_controller) {
            requeue_on_controller = false;
            active_handoff->Queue(framework::ActionDomain::Exploration, framework::NpcMotion{18});
        }
    } else {
        ++controller_clears;
        controller_installed = false;
    }
}
int map_group, map_definition, map_phase;
unsigned map_floor = 2;
void* __fastcall Group(void*, void*) {
    return &map_group;
}
void* __fastcall Map(void*, void*) {
    return &map_definition;
}
unsigned __fastcall Key(void*, void*) {
    return map_floor;
}
}
__declspec(noinline) void TestGazeHandoff(const api::WorldFrame& frame, const api::NpcCreateInput& create,
                                          const framework::CharacterSessionServices& session_services,
                                          const api::ReferenceMethods& references,
                                          const api::EntityAccessMethods& entity_methods,
                                          const framework::CharacterPackage& package,
                                          framework::AppearanceServices appearance, api::AnimationInputTypes types) {
    using Domain = framework::ActionDomain;
    using Handoff = framework::HandoffPhase;
    auto& assets = appearance.assets;
    auto& managed = static_cast<Objects&>(assets.objects);
    managed.allow_gaze_driver = true;
    managed.gaze_driver.reset();
    struct ResetDriverSlot {
        Objects& objects;
        ~ResetDriverSlot() { objects.allow_gaze_driver = false; }
    } reset_driver{managed};
    const auto& visuals = appearance.visuals;
    const auto& config = appearance.config;
    const auto& components = appearance.components;
    // Use the real SDK controls and real registry, including its public
    // action selection path. A pending official fade must not reject a
    // domain switch or re-submit the old look target on the next frame.
    current_path = &baseline_path;
    current_config = &baseline_entity_config;
    consumer.paths = &baseline_events;
    consumer.layers = &baseline_layers;
    model.child = &string_type;
    load_state = 5;
    changing = false;
    animator_present = true;
    animator_ready = true;
    observed_hash = 41;
    observed_time = 0;
    auto current = frame;
    current.sequence = 900;
    current.manager = {create.manager, {true, 0}};
    current.battle = {false, {true, 0}};
    current.adventure_phase = {&map_phase, {true, 0}};
    api::PresentationBindings controls;
    controls.references = references;
    controls.entity = entity_methods;
    controls.lifetime.alive = M(Alive);
    controls.lifetime.destroy = M(+[](void*, void*) {});
    controls.placement.player = M(+[](void*, void*) -> void* { return entity_ptr; });
    controls.placement.owner_world = M(EntityWorld);
    controls.placement.game_object = M(+[](void*, void*) -> void* { return &art; });
    controls.placement.transform = M(+[](void* value, void*) -> void* { return value; });
    controls.placement.position = M(+[](void*, api::Position3* out, void*) { *out = {}; });
    controls.placement.rotation = M(+[](void*, api::Rotation4* out, void*) { *out = {0, 0, 0, 1}; });
    controls.actor_control.mono = M(+[](void*, void*) -> void* { return nullptr; });
    controls.actor_control.model_attach_point = M(+[](void*, void*, bool, void*) -> void* { return &gaze_head; });
    controls.actor_control.ik_head = M(+[](void*, void*) -> void* { return &gaze_head; });
    controls.actor_control.ik_follow = M(+[](void*, void*, bool, bool, void*, void*) {
        gaze_active = true;
        ++gaze_follow_calls;
    });
    controls.actor_control.ik_stop = M(+[](void*, bool, void*) { gaze_active = gaze_fading; });
    controls.actor_control.ik_active = M(+[](void*, void*) { return gaze_active; });
    controls.actor_control.behaviour_enabled = M(+[](void*, void*) { return gaze_enabled; });
    controls.actor_control.set_behaviour_enabled = M(+[](void*, bool enabled, void*) { gaze_enabled = enabled; });
    controls.actor_control.look_enabled = M(+[](void*, void*) { return gaze_automatic; });
    controls.actor_control.set_look_enabled = M(+[](void*, bool enabled, void*) { gaze_automatic = enabled; });
    controls.arrays.length = M(+[](Value* value, void*) { return static_cast<int>(value->items.size()); });
    controls.arrays.get = M(+[](Value* value, int index, void*) -> void* { return value->items.at(index); });
    gaze_components.items = {&gaze_component};
    api::SceneObjectMethods gaze_scene_methods;
    gaze_scene_methods.string_equals = M(Equals);
    gaze_scene_methods.components = M(+[](void*, void*, bool, void*) -> void* { return &gaze_components; });
    api::SceneObjects gaze_scene(gaze_scene_methods);
    framework::AppearanceServices with_gaze{assets, gaze_scene, visuals, config, components};
    auto task_services = session_services;
    task_services.visual_program = [](api::EntityOrigin, const framework::VisualProgramInput& value, const auto&,
                                      const auto&) {
        framework::VisualProgramServices ports;
        ports.effects = std::make_shared<api::PresentationEffects>(
            api::EffectRequestServices{{}, api::EffectAccess({}), {}, {}, {}});
        ports.create = [](auto&, const auto&, const auto&) -> framework::VisualEffectId {
            throw std::logic_error("unexpected effect");
        };
        return std::make_shared<framework::VisualProgramPlayer>(value.program, std::move(ports));
    };
    framework::SceneCharacters registry(task_services,
                                        [&](auto& session, const auto&, const auto&) { session.Create(create); });
    api::PresentationContext control_context(assets.objects, controls);
    registry.ConfigureControls(control_context.Controls(), control_context.Player());
    framework::CharacterPlan plan;
    plan.package = package;
    plan.alternate = framework::BattlePresentation{{"battle.prefab", {}, {}}, {true, "OtherState", {}}};
    auto task_input = std::make_shared<framework::VisualProgramInput>();
    task_input->tasks = std::make_shared<const framework::TaskCatalog>(framework::test::ParseLegacyTaskCatalog(R"({
      "format":"hksr.task-catalog","id":"gaze","domain":"battle",
      "visual":{"id":"gaze-visual","source":"fixture","scope":"owned","effects":{}},
      "sequences":{"hold":{"entry":{"op":"serial","children":[
        {"op":"animation","request":"body","participant":"body","state":"SomeState"},
        {"op":"repeat","until_finish":true,"child":{"op":"wait_frame"}}]}}},
      "tasks":{"hold":{"entry":"hold"}}})"));
    task_input->program = task_input->tasks->visual;
    plan.visual_program = task_input;
    const framework::SceneAddress address{"gaze-map", 1};
    const auto id = registry.Add(address.key, plan,
                                 framework::PlacedAction{Domain::Battle, framework::FreeStyleState{"SomeState"}});
    registry.Service(current, address, with_gaze, types);
    ++current.sequence;
    registry.Service(current, address, with_gaze, types);
    if (registry.Status(id).handoff->phase != Handoff::Ready)
        std::cerr << "gaze fixture handoff: " << registry.Status(id).handoff->error
                  << " phase=" << static_cast<int>(registry.Status(id).handoff->phase)
                  << " action=" << registry.Status(id).action->detail << '\n';
    Check(registry.Status(id).handoff->phase == Handoff::Ready);
    registry.Look(id, {framework::LookMode::Player});
    ++current.sequence;
    registry.Service(current, address, with_gaze, types);
    Check(registry.Status(id).controls.has_value());
    if (!gaze_enabled || !gaze_active || gaze_automatic)
        std::cerr << "gaze fixture controls: " << registry.Status(id).controls->gaze_error
                  << " phase=" << static_cast<int>(registry.Status(id).controls->gaze_phase) << '\n';
    Check(gaze_enabled && gaze_active && !gaze_automatic);
    gaze_fading = true;
    const auto calls_before = gaze_follow_calls;
    const auto plays_before = npc_plays;
    const auto first = registry.Select(id, {Domain::Exploration, framework::NpcMotion{18}});
    Check(registry.Status(id).action->id == first &&
          registry.Status(id).action->phase == framework::ActionIntentPhase::Waiting);
    ++current.sequence;
    registry.Service(current, address, with_gaze, types);
    Check(npc_plays == plays_before && gaze_follow_calls == calls_before && gaze_enabled);
    // Replacing a pending request must not dispatch its predecessor later.
    const auto replacement = registry.Select(id, {Domain::Exploration, framework::AnimatorState{"SomeState"}});
    const auto animator_before = animator_plays;
    gaze_active = false;
    gaze_fading = false;
    ++current.sequence;
    registry.Service(current, address, with_gaze, types);
    ++current.sequence;
    registry.Service(current, address, with_gaze, types);
    Check(registry.Status(id).action->id == replacement &&
          registry.Status(id).action->phase == framework::ActionIntentPhase::Returned);
    Check(npc_plays == plays_before && animator_plays == animator_before + 1);
    Check(registry.Status(id).handoff->ready_domain == Domain::Exploration);
    const auto creation = registry.Status(id).creations;
    const framework::TaskControlInput task_select{framework::TaskControlOperation::Select,
                                                  framework::TaskSelection{{"gaze", "hold"}}};
    const auto before_task = battle_plays;
    const auto old_queue = animator_plays;
    registry.Select(id, {Domain::Exploration, framework::AnimatorState{"OtherState"}}); // not dispatched yet
    gaze_fading = true;
    auto deferred = registry.ControlTask(id, creation, task_select, current);
    Check(deferred.preparing && battle_plays == before_task);
    Check(!registry.Status(id).action); // previous operation is superseded, not left waiting
    registry.ControlTask(id, creation, {framework::TaskControlOperation::Cancel, {}}, current);
    gaze_active = false;
    gaze_fading = false;
    ++current.sequence;
    registry.Service(current, address, with_gaze, types);
    Check(!registry.ControlTask(id, creation, {}, current).preparing && battle_plays == before_task);
    Check(animator_plays == old_queue); // cancelling the task never revives the superseded command
    gaze_fading = true;
    deferred = registry.ControlTask(id, creation, task_select, current);
    Check(deferred.preparing.has_value());
    registry.ControlTask(id, creation, {framework::TaskControlOperation::Finish, {}}, current);
    ++current.sequence;
    registry.Service(current, address, with_gaze, types);
    Check(battle_plays == before_task);
    gaze_active = false;
    gaze_fading = false;
    observed_hash = 41;
    observed_time = 0;
    for (unsigned i = 0; i < 4; ++i) {
        ++current.sequence;
        registry.Service(current, address, with_gaze, types);
    }
    const auto task_state = registry.ControlTask(id, creation, {}, current);
    Check(!task_state.preparing && task_state.preparation_error.empty());
    Check(task_state.status.execution && task_state.status.execution->finish_requested &&
          task_state.status.execution->phase == framework::TaskExecutionPhase::Finished);
    Check(registry.Status(id).handoff->ready_domain == Domain::Battle && battle_plays == before_task + 2);
    registry.Shutdown();
    registry.Service(current, address, with_gaze, types);
    Check(registry.Closed() && roots.empty() && !gaze_enabled && gaze_automatic);
    Check(managed.gaze_driver.has_value());
    bool second_driver_rejected = false;
    try {
        managed.ManagedType("Fixture", "AnotherDriver");
    } catch (const std::runtime_error&) {
        second_driver_rejected = true;
    }
    Check(second_driver_rejected);
}
void TestModelProfileInputs() {
    TestSection([] {
        std::unique_ptr<api::ActorVisualConfiguration> binding;
        int live_consumer = 0, creates = 0, applies = 0, closes = 0;
        bool can_close = true, replace_during_apply = false, unsupported = false, return_null = false,
             lose_during_create = false;
        bool consumer_lookup_failed = false;
        const std::vector<std::string> files{"Skin/Designer", "Skin/Audio"};
        auto create = [&]() -> std::unique_ptr<api::ActorVisualConfiguration> {
            ++creates;
            if (return_null)
                return {};
            if (lose_during_create) {
                lose_during_create = false;
                live_consumer = 0;
            }
            const int captured = live_consumer;
            api::ActorVisualConfigurationServices services;
            // Like the real host, a null consumer can be a current binding
            // before loading ends. It must never be created/applied that early.
            services.current = [] { return true; };
            services.current_component = [&, captured](api::VisualConfigurationComponent use) {
                Check(use != api::VisualConfigurationComponent::Emotion); // skin inputs do not use emotion
                if (use == api::VisualConfigurationComponent::Data)
                    return true;
                if (consumer_lookup_failed)
                    throw std::runtime_error("consumer lookup failed");
                return live_consumer == captured;
            };
            services.apply = [&](const api::ActorVisualConfigurationInput& input) {
                ++applies;
                Check(live_consumer && input.event_files && *input.event_files == files);
                if (replace_during_apply) {
                    replace_during_apply = false;
                    ++live_consumer;
                }
                api::ActorVisualConfigurationResult result;
                if (unsupported)
                    result.unsupported_fields = {"event_files"};
                return result;
            };
            services.emotion = [](bool, float, float) { return true; };
            services.close = [&] {
                ++closes;
                return can_close;
            };
            return std::make_unique<api::ActorVisualConfiguration>(std::move(services));
        };
        auto prepare = [&](bool model_ready) {
            return framework::detail::PrepareSkinEventInputs(
                binding, [&] { return model_ready && live_consumer != 0; }, files, create);
        };
        Check(!prepare(false) && !binding && creates == 0 && applies == 0);
        live_consumer = 1;
        Check(prepare(true) && creates == 1 && applies == 1);
        // A replaced consumer must drain the old binding before a new one is
        // acquired, even when its model is still loading.
        live_consumer = 2;
        can_close = false;
        Check(!prepare(false) && binding && creates == 1 && applies == 1);
        Check(!prepare(true) && creates == 1 && applies == 1);
        can_close = true;
        Check(prepare(true) && creates == 2 && applies == 2);
        replace_during_apply = true;
        Check(!prepare(true) && !binding && creates == 2 && applies == 3);
        Check(prepare(true) && creates == 3 && applies == 4);
        unsupported = true;
        bool rejected = false;
        try {
            prepare(true);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        Check(rejected && creates == 3 && applies == 5); // genuine unsupported is not loading
        unsupported = false;
        Check(binding->Close());
        binding.reset();
        return_null = true;
        Check(!prepare(true) && !binding && applies == 5);
        return_null = false;
        lose_during_create = true;
        Check(!prepare(true) && !binding && live_consumer == 0 && applies == 5);
        live_consumer = 4;
        Check(prepare(true) && applies == 6);
        const auto before_lookup_creates = creates;
        consumer_lookup_failed = true;
        bool lookup_rejected = false;
        try {
            prepare(true);
        } catch (const std::runtime_error&) {
            lookup_rejected = true;
        }
        Check(lookup_rejected && binding && creates == before_lookup_creates && applies == 6);
        consumer_lookup_failed = false;
        Check(binding->Close() && closes >= 5);
        binding.reset();
    });
    api::ProfileInputMethods declared_inputs{M(Path), M(Config), M(Initialize), M(Initialized), {}};
    api::ProfileInputs access(declared_inputs);
    Check(access.AnimatorInitialized(&model).value);
    const api::ProfileEventFields fields{offsetof(Consumer, paths), offsetof(Consumer, layers), true, true, {}};
    api::ReferenceMethods references{M(Keep), M(Target), M(Free), {}, {}};
    api::ManagedArrays arrays({M(Array), {}, {}, M(Set), {}});
    Objects objects;
    const api::AvatarCatalogRow source{777, "角色/战斗.json", "角色/战斗.prefab"};
    const api::PresentationConfigInput source_config{
        source.character_config, {true, "StandBy", {}}, {true, {"表情.json", "附件.json"}, {}}, {true, {"Scene"}, {}}};
    const auto description = framework::SourceCatalog::Presentation(source, source_config).model;
    {
        api::ModelProfileInputs inputs(references);
        inputs.Prepare(objects, arrays, access, fields, &model, &consumer, description);
        const auto& s = inputs.Status();
        Check(s.prepared && initializes == 1 && s.baseline.model_path == &baseline_path &&
              s.baseline_entity_config == &baseline_entity_config);
        Check(s.baseline.event_paths == &baseline_events && s.baseline.excluded_layers == &baseline_layers);
        Check(!s.target.adventure_config && !s.baseline.adventure_config);
        Check(static_cast<Value*>(s.target.model_path)->text == description.model_path);
        auto* events = static_cast<Value*>(s.target.event_paths);
        Check(events->items.size() == 2 && static_cast<Value*>(events->items[1])->text == "附件.json");
        Check(inputs.Close().complete && roots.empty());
    }
    {
        consumer.layers = nullptr;
        null_config = true;
        api::ModelProfileInputs inputs(references);
        inputs.Prepare(objects, arrays, access, fields, &model, &consumer, {"battle.prefab", {}, {}});
        const auto& s = inputs.Status();
        Check(s.prepared && !s.baseline_entity_config && s.baseline.excluded_layers &&
              static_cast<Value*>(s.baseline.excluded_layers)->items.empty() &&
              static_cast<Value*>(s.target.event_paths)->items.empty());
        Check(inputs.Close().complete && roots.empty());
        consumer.layers = &baseline_layers;
        null_config = false;
    }
    {
        constructor_fault = true;
        api::ModelProfileInputs inputs(references);
        inputs.Prepare(objects, arrays, access, fields, &model, &consumer, description);
        Check(!inputs.Status().prepared && inputs.Status().operation.object == &operation &&
              inputs.Status().initialize.exception == 0xE06C0001);
        Check(Rooted(&operation) && inputs.Close().complete && roots.empty());
        constructor_fault = false;
    }
    {
        array_fault = true;
        const auto before = initializes;
        api::ModelProfileInputs inputs(references);
        inputs.Prepare(objects, arrays, access, fields, &model, &consumer, description);
        Check(!inputs.Status().prepared && initializes == before && inputs.Close().complete && roots.empty());
        array_fault = false;
    }
    std::cout << "Model profile input transaction passed: official getters/default constructor, retained baseline "
                 "arrays, event-only target and partial-failure roots; local substitutes only.\n";

    // Same typed inputs, now consumed by the real request implementation.
    baseline_path.text = "npc.prefab";
    baseline_entity_config.text = "npc.entity";
    alias_path.text = "resolved/battle.prefab";
    api::ModelAccessMethods model_methods{
        M(Art), M(Animator), M(LoadState), M(Changing), M(Alive), M(Alive), offsetof(Value, child), true, {}};
    auto input_methods = declared_inputs;
    input_methods.animator_initialized = M(AnimatorReady);
    struct EventComponent {
        void* consumer;
    } component_view{&consumer};
    api::AnimationEventMethods event_methods;
    event_methods.has_consumer = true;
    event_methods.consumer_offset = offsetof(EventComponent, consumer);
    // Use actual memory identity for the event consumer getter, as in SDK.
    auto* event_object = &component_view;
    api::ModelMethods profile_methods{M(Begin), M(Cancel), {}, {}, M(CanChange), M(Restore)};
    // Event setter below is redirected to the same official signature but
    // accepts this synthetic outer component rather than its old placeholder.
    profile_methods.events = M(+[](void* c, void* paths, void* layers, void*) {
        auto* object = static_cast<EventComponent*>(c);
        Check(object->consumer == &consumer);
        ++event_sets;
        if (!event_noop) {
            consumer.paths = paths;
            consumer.layers = layers;
        }
    });
    api::ModelRequestServices services{
        api::ModelProfile(profile_methods),  api::ModelAccess(model_methods),
        api::ProfileInputs(input_methods),   api::SceneObjects({{}, {}, {}, M(Equals), {}}),
        api::AnimationEvents(event_methods), api::ModelLoadCompleted};
    api::WorldFrame frame;
    frame.world = world_ptr;
    frame.disposing = {false, {true, 0}};
    auto foreign = frame;
    foreign.world = reinterpret_cast<api::GameWorld*>(1);
    std::string value_source;
    auto make = [&]() {
        current_path = &baseline_path;
        current_config = &baseline_entity_config;
        consumer.paths = &baseline_events;
        consumer.layers = &baseline_layers;
        model.child = &string_type;
        load_state = 5;
        changing = false;
        animator_present = true;
        animator_ready = true;
        auto inputs = std::make_unique<api::ModelProfileInputs>(references);
        inputs->Prepare(objects, arrays, access, fields, &model, &consumer, {"battle.prefab", {}, {}, value_source});
        Check(inputs->Status().prepared);
        return std::make_unique<api::ModelProfileRequest>(api::TrackedEntities().Current(entity_ptr), world_ptr,
                                                          std::move(inputs), references, services);
    };
    using Phase = api::ModelRequestPhase;
    using Destination = api::ModelDestination;
    const auto token = api::TrackedEntities().Track(entity_ptr);
    {
        value_source = "Config/Test.json";
        services.event_values = [] { return std::make_unique<ValueLease>(); };
        auto request = make();
        request->Submit(frame, model_ptr, Destination::Alternate);
        request->Observe(frame, model_ptr, true);
        request->BindEvents(frame, model_ptr, event_object, fields);
        Check(request->Status().phase == Phase::EventsReady && value_active == 1 && value_applies == 1 &&
              value_path == value_source);
        request->BindEvents(frame, model_ptr, event_object, fields);
        Check(value_applies == 1);
        request->Submit(frame, model_ptr, Destination::Baseline);
        Check(value_active == 1);
        request->Observe(frame, model_ptr, true);
        request->BindEvents(frame, model_ptr, event_object, fields);
        Check(request->Status().phase == Phase::EventsReady && value_active == 0 && value_restores == 1);
        Check(request->Close(frame).complete && roots.empty());
        value_fail = true;
        request = make();
        request->Submit(frame, model_ptr, Destination::Alternate);
        request->Observe(frame, model_ptr, true);
        request->BindEvents(frame, model_ptr, event_object, fields);
        Check(request->Status().phase == Phase::Failed);
        const auto attempts = value_applies;
        value_fail = false;
        request->Submit(frame, model_ptr, Destination::Alternate);
        request->Observe(frame, model_ptr, true);
        request->BindEvents(frame, model_ptr, event_object, fields);
        Check(request->Status().phase == Phase::Failed &&
              value_applies == attempts); // retained failed lease cannot silently bypass preparation
        Check(request->Close(frame).complete && value_active == 0 && roots.empty());
        value_source.clear();
    }
    {
        auto request = make();
        const int before = begins, event_before = event_sets, restore_before = restores;
        request->Submit(foreign, model_ptr, Destination::Alternate);
        Check(begins == before);
        resolved_alias = true;
        request->Submit(frame, model_ptr, Destination::Alternate);
        Check(request->Status().phase == Phase::AwaitingModel && begins == before + 1);
        // Both root/Art liveness getters are true while load state is pending.
        load_state = 4;
        request->Observe(frame, model_ptr, true);
        Check(request->Status().phase == Phase::AwaitingModel);
        load_state = 5;
        changing = true;
        request->Observe(frame, model_ptr, true);
        Check(request->Status().phase == Phase::AwaitingModel);
        changing = false;
        animator_ready = false;
        request->Observe(frame, model_ptr, true);
        Check(request->Status().phase == Phase::AwaitingModel);
        animator_ready = true;
        request->Observe(frame, model_ptr, true);
        Check(request->Status().phase == Phase::ModelReady);
        request->BindEvents(frame, model_ptr, event_object, fields);
        Check(request->Status().phase == Phase::EventsReady && event_sets == event_before + 1);
        request->BindEvents(frame, model_ptr, event_object, fields);
        Check(event_sets == event_before + 1);
        const int cancel_before = cancels;
        Check(request->Cancel(frame) && cancels == cancel_before + 1 &&
              current_path == &alias_path); // cancel is NOT restore
        resolved_alias = false;
        request->Submit(frame, model_ptr, Destination::Baseline);
        Check(restores == restore_before + 1 && current_path == &baseline_path &&
              current_config == &baseline_entity_config);
        animator_present = false;
        request->Observe(frame, model_ptr, false);
        Check(request->Status().phase == Phase::ModelReady);
        request->BindEvents(frame, model_ptr, event_object, fields);
        Check(request->Status().phase == Phase::EventsReady && consumer.paths == &baseline_events);
        Check(request->Close(frame).complete && roots.empty());
    }
    {
        auto request = make();
        can_change = false;
        request->Submit(frame, model_ptr, Destination::Alternate);
        Check(request->Status().phase == Phase::Failed);
        const int before = event_sets;
        request->Submit(frame, model_ptr, Destination::Baseline);
        request->Observe(frame, model_ptr, false);
        request->BindEvents(frame, model_ptr, event_object, fields);
        Check(request->Status().phase == Phase::EventsReady && event_sets == before); // already installed
        can_change = true;
        Check(request->Close(frame).complete && roots.empty());
    }
    {
        auto request = make();
        begin_fault = true;
        request->Submit(frame, model_ptr, Destination::Alternate);
        Check(request->Status().phase == Phase::Failed && request->Status().operation_in_use &&
              request->Status().submit.exception == 0xE06C0010);
        begin_fault = false;
        Check(request->Close(frame).complete && roots.empty());
    }
    {
        auto request = make();
        request->Submit(frame, model_ptr, Destination::Alternate);
        cancel_fault = true;
        const int before = cancels;
        Check(!request->Close(frame).complete);
        Check(!request->Close(frame).complete && cancels == before + 1);
        request->Submit(frame, model_ptr, Destination::Baseline);
        Check(cancels == before + 1);
        cancel_fault = false;
        api::TrackedEntities().Disposed(token);
        Check(request->Close(foreign).complete && roots.empty() && cancels == before + 1);
    }
    api::TrackedEntities().Forget(token);
    const auto next_token = api::TrackedEntities().Track(entity_ptr);
    {
        auto request = make();
        request->Submit(frame, model_ptr, Destination::Alternate);
        request->Observe(frame, model_ptr, true);
        event_noop = true;
        request->BindEvents(frame, model_ptr, event_object, fields);
        Check(request->Status().phase == Phase::Failed && request->Status().event_replace.returned);
        const int before = event_sets;
        request->BindEvents(frame, model_ptr, event_object, fields);
        Check(event_sets == before);
        event_noop = false;
        Check(request->Close(frame).complete && roots.empty());
    }
    {
        auto request = make();
        submit_wrong = true;
        request->Submit(frame, model_ptr, Destination::Alternate);
        Check(request->Status().phase == Phase::Failed);
        submit_wrong = false;
        Check(request->Close(frame).complete && roots.empty());
    }
    {
        auto request = make();
        request->Submit(frame, model_ptr, Destination::Alternate);
        keep_fail = true;
        request->Submit(frame, model_ptr, Destination::Baseline);
        Check(request->Status().phase == Phase::Failed);
        request->Submit(frame, model_ptr, Destination::Baseline);
        Check(request->Status().phase == Phase::AwaitingModel && request->Close(frame).complete && roots.empty());
    }
    {
        auto request = make();
        dispose_on_begin = true;
        request->Submit(frame, model_ptr, Destination::Alternate);
        Check(request->Status().phase == Phase::OwnerEnded);
        dispose_on_begin = false;
        const int before = cancels;
        Check(request->Close(foreign).complete && roots.empty() && cancels == before);
    }
    api::TrackedEntities().Forget(next_token);
    {
        const auto async_owner = api::TrackedEntities().Track(entity_ptr);
        const auto original_profile = services.profile;
        services.profile = api::ModelProfile(profile_methods, M(Pending));
        async_requested = true;
        {
            auto request = make();
            const auto before_events = event_sets;
            request->Submit(frame, model_ptr, Destination::Alternate);
            Check(request->Status().phase == Phase::AwaitingModel && current_path == &baseline_path && async_pending);
            request->Observe(frame, model_ptr, true);
            Check(request->Status().phase == Phase::AwaitingModel && event_sets == before_events);
            current_path = async_path;
            current_config = nullptr;
            async_pending = false;
            load_state = 4;
            request->Observe(frame, model_ptr, true);
            Check(request->Status().phase == Phase::AwaitingModel);
            load_state = 5;
            request->Observe(frame, model_ptr, true);
            Check(request->Status().phase == Phase::ModelReady);
            request->BindEvents(frame, model_ptr, event_object, fields);
            Check(request->Status().phase == Phase::EventsReady);
            Check(request->Close(frame).complete && roots.empty());
        }
        {
            auto request = make();
            request->Submit(frame, model_ptr, Destination::Alternate);
            const auto before = cancels;
            request->Submit(frame, model_ptr, Destination::Baseline);
            Check(cancels == before + 1 && !async_pending && !async_path && current_path == &baseline_path);
            request->Observe(frame, model_ptr, false);
            Check(request->Status().phase == Phase::ModelReady);
            Check(request->Close(frame).complete && roots.empty());
        }
        {
            auto request = make();
            request->Submit(frame, model_ptr, Destination::Alternate);
            async_pending = false; // Official failure completed without replacing the old model.
            request->Observe(frame, model_ptr, true);
            Check(request->Status().phase == Phase::Failed);
            Check(request->Close(frame).complete && roots.empty());
        }
        {
            auto request = make();
            begin_fault = true;
            request->Submit(frame, model_ptr, Destination::Alternate);
            begin_fault = false;
            Check(request->Status().phase == Phase::Failed && async_pending);
            Check(request->Close(frame).complete && !async_pending && roots.empty());
        }
        {
            auto request = make();
            request->Submit(frame, model_ptr, Destination::Alternate);
            pending_fault = true;
            request->Observe(frame, model_ptr, true);
            Check(request->Status().phase == Phase::Failed);
            pending_fault = false;
            Check(request->Close(frame).complete && !async_pending && roots.empty());
        }
        {
            auto request = make();
            request->Submit(frame, model_ptr, Destination::Alternate);
            api::TrackedEntities().Disposed(async_owner);
            const auto before = cancels;
            Check(request->Close(foreign).complete && cancels == before + 1 && !async_pending && roots.empty());
        }
        api::TrackedEntities().Forget(async_owner);
        async_requested = false;
        services.profile = original_profile;
    }
    std::cout << "Official model request tests passed: actual DLL calls, pending loading, cancel vs restoration, "
                 "empty/same-path event adoption, faults and owner-end closure; no game acceptance.\n";

    // Full coordinator uses the actual CharacterInstance, request, controller
    // binding and ActionPlayback implementations, not a mocked stage port.
    api::EntityAccessMethods entity_methods{M(EntityId), M(EntityWorld), M(Alive), M(Component),
                                            {},          M(Kill),        M(Die),   {}};
    api::EntityAccess entities(entity_methods);
    api::AnimationControlMethods action_methods;
    action_methods.hash = M(Hash);
    action_methods.has_state = M(Has);
    action_methods.play = M(AnimatorPlay);
    action_methods.npc_motion = M(NpcPlay);
    action_methods.free_style_state = M(BattlePlay);
    action_methods.replace_path = M(ControllerPath);
    action_methods.state_info = M(ObserveState);
    action_methods.in_transition = M(+[](void*, int, void*) { return false; });
    api::AnimationControl animation(action_methods);
    api::SceneObjects scene({{}, {}, {}, M(Equals), {}});
    api::ControllerBindingServices controller_services{animation, entities, scene, arrays, {}, {}, {}};
    auto transition = [=](api::EntityToken owner, api::GameWorld* owner_world,
                          std::unique_ptr<api::ModelProfileRequest> request) {
        return std::make_unique<api::ModelTransition>(
            owner, owner_world, std::move(request),
            api::ModelTransitionServices{references, entities, api::ModelAccess(model_methods), animation,
                                         controller_services, fields});
    };
    auto animations = [=](api::EntityToken owner) {
        return std::make_unique<api::ActorAnimationSession>(
            owner, references, entities, api::ModelAccess(model_methods), animation, api::ProfileInputs(input_methods));
    };
    framework::HandoffServices handoff_services{animations, transition};
    bool motion_drained = true;
    unsigned model_quiesces = 0;
    handoff_services.prepare_model_change = [&](api::EntityOrigin origin) {
        Check(origin.token.entity == entity_ptr && origin.world == world_ptr);
        ++model_quiesces;
        return motion_drained;
    };
    auto create_npc = [=] {
        return std::make_unique<api::EntityCreationSession>(api::EntityOwner{}, M(Create), entity_methods, references);
    };
    framework::CharacterMethods character_methods{
        create_npc, TestModelServices(references, {}, {}, entity_methods, api::ModelAccess(model_methods))};
    framework::CharacterPackage package;
    package.id = "test.npc";
    api::NpcCreateInput create{world_ptr, reinterpret_cast<api::EntityManager*>(&npc_manager),
                               reinterpret_cast<api::MapNpcDefinition*>(&npc_definition),
                               reinterpret_cast<api::NpcDataRow*>(&npc_row), true};
    // Geometry providers intentionally unbound: pure official NPC must not
    // require unrelated external Mesh/texture capabilities to change domains.
    api::MeshAccess mesh({});
    api::MeshSkinning skin({});
    api::MeshTemplateAccess templates({});
    api::TextureAccess texture({});
    api::MaterialAccess material({});
    api::ShaderAccess shader({});
    api::HierarchyAccess hierarchy({});
    api::VisualObjects visuals({});
    api::AdventureConfig config({});
    api::ComponentConfig components({});
    framework::ModelAssetServices assets{objects, mesh, skin, templates, texture, material, shader, arrays, hierarchy};
    framework::AppearanceServices appearance{assets, scene, visuals, config, components};
    api::AnimationInputTypes types;
    types.events = {{true, 0}, &event_type};
    current_event_component = event_object;
    framework::CharacterInstance character(world_ptr, package, nullptr, "", character_methods, {});
    character.Create(create);
    Check(character.Creation().create_returned);
    auto request = make();
    framework::DomainControllers controllers;
    controllers.exploration = api::ControllerSource{api::ControllerSourceKind::ResourcePath, "Controller/exploration"};
    framework::ModelActionHandoff handoff(character, api::TrackedEntities().Current(entity_ptr), world_ptr,
                                          std::move(request), handoff_services, controllers);
    active_handoff = &handoff;
    using Domain = framework::ActionDomain;
    using Handoff = framework::HandoffPhase;
    const auto original_begins = begins, original_restores = restores;
    requeue_on_controller = true;
    handoff.Queue(Domain::Exploration, framework::NpcMotion{18});
    handoff.Service(frame, appearance, types);
    Check(npc_plays == 0 && controller_sets == 1 && handoff.Status().phase == Handoff::Adopting);
    handoff.Service(frame, appearance, types);
    Check(npc_plays == 1 && controller_sets == 1 && controller_installed && begins == original_begins &&
          restores == original_restores);
    Check(handoff.Status().output && handoff.Status().output->outcome == framework::ActionOutcome::Returned);
    load_state = 4;
    const auto loading_request = handoff.Queue(Domain::Battle, framework::FreeStyleState{"SomeState"});
    motion_drained = false;
    const auto before_quiesce = model_quiesces;
    handoff.Service(frame, appearance, types);
    Check(model_quiesces == before_quiesce + 1 && begins == original_begins && controller_clears == 0 &&
          controller_installed);
    motion_drained = true;
    handoff.Service(frame, appearance, types);
    Check(handoff.Status().phase == Handoff::Loading && begins == original_begins + 1 && controller_clears == 1 &&
          !controller_installed && battle_plays == 0);
    Check(!handoff.Status().output && !handoff.Status().playback_serial); // no prior NPC receipt for the new command
    Check(handoff.ActionStatus(loading_request).phase == framework::ActionIntentPhase::Dispatching &&
          !handoff.ActionStatus(loading_request).outcome &&
          !handoff.ActionStatus(loading_request).settled_successor);
    handoff.Service(frame, appearance, types);
    Check(begins == original_begins + 1 && battle_plays == 0);
    // Replace an in-flight request. Cancellation must not restore/kill the NPC;
    // only the newest state is played when the official model really loads.
    const auto replacement_request = handoff.Queue(Domain::Battle, framework::FreeStyleState{"OtherState"});
    Check(handoff.ActionStatus(loading_request).phase == framework::ActionIntentPhase::Cancelled &&
          !handoff.ActionStatus(loading_request).settled_successor);
    load_state = 5;
    handoff.Service(frame, appearance, types);
    Check(battle_plays == 1 && handoff.Status().phase == Handoff::Ready && !character.Creation().removal_requested);
    Check(handoff.Status().servicing == replacement_request &&
          handoff.Status().output->serial == handoff.Status().playback_serial &&
          std::get<framework::FreeStyleState>(handoff.Status().output->request).name == "OtherState");
    Check(handoff.ActionStatus(replacement_request).phase == framework::ActionIntentPhase::Returned &&
          handoff.ActionStatus(replacement_request).outcome == framework::ActionOutcome::Returned);
    const auto loaded_begins = begins;
    handoff.Queue(Domain::Battle, framework::FreeStyleState{"SomeState"});
    handoff.Service(frame, appearance, types);
    Check(battle_plays == 2 && begins == loaded_begins); // same domain: no model reload
    state_exists = false;
    const auto absent_action_request = handoff.Queue(Domain::Battle, framework::FreeStyleState{"OtherState"});
    handoff.Service(frame, appearance, types);
    Check(battle_plays == 2 && handoff.Playback().last->outcome == framework::ActionOutcome::StateAbsent);
    Check(handoff.ActionStatus(absent_action_request).phase == framework::ActionIntentPhase::Failed &&
          handoff.ActionStatus(absent_action_request).outcome == framework::ActionOutcome::StateAbsent &&
          !handoff.ActionStatus(absent_action_request).settled_successor);
    state_exists = true;
    requeue_battle = true;
    handoff.Queue(Domain::Battle, framework::FreeStyleState{"SomeState"});
    handoff.Service(frame, appearance, types);
    Check(battle_plays == 3 && npc_plays == 1); // callback queues but cannot recursively switch
    handoff.Service(frame, appearance, types);
    Check(npc_plays == 2 && restores == original_restores + 1 && current_config == &baseline_entity_config &&
          consumer.paths == &baseline_events);
    Check(controller_installed && controller_sets == 2 && handoff.Status().ready_domain == Domain::Exploration);
    // Event adoption failure does not delete the character or silently play.
    event_noop = true;
    const auto failed_request = handoff.Queue(Domain::Battle, framework::FreeStyleState{"OtherState"});
    handoff.Service(frame, appearance, types);
    Check(handoff.Status().phase == Handoff::Failed && battle_plays == 3 && !character.Creation().removal_requested);
    Check(handoff.ActionStatus(failed_request).phase == framework::ActionIntentPhase::Failed &&
          !handoff.ActionStatus(failed_request).detail.empty() &&
          !handoff.ActionStatus(failed_request).settled_successor);
    event_noop = false;
    handoff.Queue(Domain::Exploration, framework::AnimatorState{"SomeState"});
    handoff.Service(frame, appearance, types);
    Check(animator_plays == 1 && handoff.Status().phase == Handoff::Ready && controller_installed);
    {
        const auto saved_battle = frame.battle;
        const auto saved_state_reads = state_reads;
        frame.battle = {false, {true, 0}};
        auto reaction_catalog = std::make_shared<const framework::TaskCatalog>(framework::test::ParseLegacyTaskCatalog(R"({
            "format":"hksr.task-catalog","id":"reaction-fixture","domain":"battle",
            "visual":{"id":"reaction-scene","source":"fixture","scope":"owned","effects":{}},
            "sequences":{"enter":{"entry":{"op":"reaction","request":"pose","config":"fixture","flag":133}},
              "recover":{"entry":{"op":"reaction","request":"pose","config":"fixture","flag":133,"recover":true}}},
            "tasks":{"enter":{"entry":"enter"},"recover":{"entry":"recover"}}
        })"));
        framework::VisualProgramServices visual_services;
        visual_services.effects = std::make_shared<api::PresentationEffects>(
            api::EffectRequestServices{{}, api::EffectAccess({}), {}, {}, {}});
        visual_services.create = [](auto&, const auto&, const auto&) -> framework::VisualEffectId {
            throw std::logic_error("no reaction effects expected");
        };
        auto visual_player =
            std::make_shared<framework::VisualProgramPlayer>(reaction_catalog->visual, std::move(visual_services));
        handoff.ConfigureTasks(reaction_catalog, visual_player,
                              [](const framework::PoseSelection& selection) -> std::optional<framework::PoseCommand> {
                                  const auto& key = std::get<framework::ReactionSelection>(selection);
                                  framework::VisualState pose;
                                  pose.character_config = key.source;
                                  pose.recover = key.recover;
                                  pose.value.flag = {true, key.flag, {}};
                                  pose.value.occur.resolved_state = pose.value.recover.resolved_state =
                                      {true, "SomeState", {}};
                                  pose.value.loop.resolved_state = {true, "", {}};
                                  pose.value.occur.transition = pose.value.recover.transition = {true, 0, {}};
                                  pose.expression = {true, "", {}};
                                  return framework::PoseCommand{pose};
                              });
        handoff.Queue(Domain::Battle, framework::FreeStyleState{"SomeState"});
        for (unsigned attempt = 0; attempt < 5 && (handoff.Status().ready_domain != Domain::Battle ||
                                                   handoff.Status().phase != Handoff::Ready);
             ++attempt) {
            ++frame.sequence;
            handoff.Service(frame, appearance, types);
        }
        Check(handoff.Status().phase == Handoff::Ready && handoff.Status().ready_domain == Domain::Battle);
        handoff.RestoreManualReaction({"fixture", 133});
        ++frame.sequence;
        handoff.Service(frame, appearance, types);
        Check(handoff.ManualVisualReaction() && handoff.ManualVisualReaction()->flag == 133);
        framework::VisualState recover;
        recover.character_config = "fixture";
        recover.recover = true;
        recover.value.flag = {true, 115, {}};
        recover.value.recover.resolved_state = {true, "SomeState", {}};
        recover.value.recover.transition = {true, 0, {}};
        handoff.Queue(Domain::Battle, recover);
        ++frame.sequence;
        handoff.Service(frame, appearance, types);
        if (!handoff.Status().output || handoff.Status().output->outcome != framework::ActionOutcome::Returned ||
            !handoff.ManualVisualReaction())
            std::cerr << "recovery fixture phase=" << static_cast<int>(handoff.Status().phase)
                      << " error=" << handoff.Status().error
                      << " output=" << (handoff.Status().output ? handoff.Status().output->detail : "none") << '\n';
        Check(handoff.Status().output && handoff.Status().output->outcome == framework::ActionOutcome::Returned &&
              handoff.ManualVisualReaction() && handoff.ManualVisualReaction()->flag == 133);
        recover.value.flag.value = 133;
        handoff.Queue(Domain::Battle, recover);
        ++frame.sequence;
        handoff.Service(frame, appearance, types);
        Check(handoff.Status().output && handoff.Status().output->outcome == framework::ActionOutcome::Returned &&
              !handoff.ManualVisualReaction());
        const auto cleared = handoff.QueryTasks();
        Check(std::none_of(cleared.contexts.begin(), cleared.contexts.end(),
                           [](const auto& context) { return bool(context.visual_reaction); }));
        handoff.RestoreManualReaction({"fixture", 133});
        ++frame.sequence;
        handoff.Service(frame, appearance, types);
        Check(handoff.ManualVisualReaction().has_value());
        state_exists = false;
        handoff.Queue(Domain::Battle, framework::FreeStyleState{"OtherState"});
        ++frame.sequence;
        handoff.Service(frame, appearance, types);
        Check(handoff.Status().output && handoff.Status().output->outcome == framework::ActionOutcome::StateAbsent &&
              handoff.ManualVisualReaction());
        state_exists = true;
        handoff.Queue(Domain::Battle, framework::FreeStyleState{"SomeState"});
        ++frame.sequence;
        handoff.Service(frame, appearance, types);
        Check(handoff.Status().output && handoff.Status().output->outcome == framework::ActionOutcome::Returned &&
              !handoff.ManualVisualReaction());
        auto task_pose = [&](const char* name) {
            const auto result = handoff.ControlTask(
                {framework::TaskControlOperation::Select, framework::TaskSelection{{"reaction-fixture", name}}});
            Check(result.selection && result.selection->intent);
            for (int step = 0; step < 4; ++step) {
                ++frame.sequence;
                handoff.Service(frame, appearance, types);
            }
            Check(handoff.QueryTasks().status.error.empty());
        };
        task_pose("enter");
        Check(handoff.ManualVisualReaction() && handoff.QueryTasks().manual_reaction->flag == 133);
        task_pose("recover");
        Check(!handoff.ManualVisualReaction() && !handoff.QueryTasks().manual_reaction);
        frame.battle = saved_battle;
        state_reads = saved_state_reads;
    }
    handoff.Remove();
    handoff.Service(frame, appearance, types);
    Check(handoff.Status().closed && character.Status().closed && !controller_installed && roots.empty());
    {
        framework::CharacterInstance initialized_character(world_ptr, package, nullptr, "", character_methods, {});
        initialized_character.Create(create);
        value_source = "Config/Test.json";
        auto initial_request = make();
        framework::ModelActionHandoff initial(initialized_character, api::TrackedEntities().Current(entity_ptr),
                                              world_ptr, std::move(initial_request), handoff_services, {},
                                              "OtherState");
        const auto plays = battle_plays;
        const auto begin_count = begins;
        frame.sequence = 100;
        initial.Queue(Domain::Battle, framework::FreeStyleState{"SomeState"});
        initial.Service(frame, appearance, types);
        Check(initial.Status().phase == Handoff::Initializing && battle_plays == plays + 1 && state_reads == 0);
        Check(!initial.Status().output &&
              !initial.Status().playback_serial); // initial-state playback is not the requested action
        initial.Service(frame, appearance, types);
        Check(initial.Status().phase == Handoff::Initializing && state_reads == 0 && battle_plays == plays + 1);
        ++frame.sequence;
        initial.Service(frame, appearance, types);
        Check(initial.Status().phase == Handoff::Initializing && state_reads == 1 && battle_plays == plays + 1);
        // Same-domain selection while initializing neither cancels/reloads the
        // model nor repeats its initial request.
        initial.Queue(Domain::Battle, framework::FreeStyleState{"SomeState"});
        observed_hash = 41;
        ++frame.sequence;
        initial.Service(frame, appearance, types);
        Check(initial.Status().phase == Handoff::Ready && battle_plays == plays + 2 && begins == begin_count + 1 &&
              state_reads == 2);
        Check(initial.Status().output &&
              std::get<framework::FreeStyleState>(initial.Status().output->request).name == "SomeState");
        const auto requested_output = initial.Status().output->serial;
        observed_hash = 37;
        observed_time = 1.1f;
        ++frame.sequence;
        initial.Service(frame, appearance, types);
        Check(battle_plays == plays + 3 &&
              std::get<framework::FreeStyleState>(initial.Playback().last->request).name == "OtherState");
        Check(initial.Status().output->serial == requested_output &&
              std::get<framework::FreeStyleState>(initial.Status().output->request).name == "SomeState");
        observed_time = 0;
        const auto before_ended = value_ended;
        Check(value_active == 1);
        require_value_on_die = true;
        initial.Remove();
        initial.Service(frame, appearance, types);
        Check(initial.Status().closed && roots.empty());
        require_value_on_die = false;
        value_source.clear();
        Check(value_active == 0 && value_ended == before_ended + 1);
    }
    {
        framework::CharacterInstance absent_character(world_ptr, package, nullptr, "", character_methods, {});
        absent_character.Create(create);
        auto absent_request = make();
        framework::ModelActionHandoff absent(absent_character, api::TrackedEntities().Current(entity_ptr), world_ptr,
                                             std::move(absent_request), handoff_services, {}, "OtherState");
        default_absent = true;
        const auto plays = battle_plays;
        absent.Queue(Domain::Battle, framework::FreeStyleState{"SomeState"});
        absent.Service(frame, appearance, types);
        Check(absent.Status().phase == Handoff::Ready && battle_plays == plays + 1 &&
              !absent.Status().initialization_note.empty());
        default_absent = false;
        absent.Remove();
        absent.Service(frame, appearance, types);
        Check(absent.Status().closed && roots.empty());
    }
    std::cout << "Model/action handoff passed: same NPC, controller detach before load, actual model readiness, event "
                 "adoption, playback, return, replacement, failure and removal; pure-official local fixture only.\n";
    // Source assembly consumes real CharacterInstance/Handoff implementations.
    // An absent/broken battle input must not disable independently usable NPCs.
    auto baseline = [=](api::EntityToken owner, api::GameWorld* owner_world, const api::ProfileDescription& profile) {
        return std::make_unique<api::ModelBaselineCapture>(owner, owner_world, references, entities, services, fields,
                                                           profile);
    };
    framework::CharacterSessionServices session_services{character_methods, {}, handoff_services, baseline};
    auto reset_baseline = [&] {
        current_path = &baseline_path;
        current_config = &baseline_entity_config;
        consumer.paths = &baseline_events;
        consumer.layers = &baseline_layers;
        model.child = &string_type;
        load_state = 5;
        changing = false;
        animator_present = true;
        animator_ready = true;
    };
    TestSection([&] {
        reset_baseline();
        framework::CharacterInstance restored_actor(world_ptr, package, nullptr, "", character_methods, {});
        restored_actor.Create(create);
        framework::ModelActionHandoff restored(restored_actor, api::TrackedEntities().Current(entity_ptr), world_ptr,
                                               nullptr, handoff_services);
        auto catalog = std::make_shared<const framework::TaskCatalog>(framework::test::ParseLegacyTaskCatalog(R"({
          "format":"hksr.task-catalog","id":"restoring-state","domain":"exploration",
          "visual":{"id":"restoring-state","source":"test","scope":"owned","effects":{},
            "contexts":{"actor":{"restore":[]}}},
          "contexts":{"actor":{"state_only":true,"values":{"marker":"initial"}}},
          "initial_contexts":["actor"],"sequences":{},"tasks":{}
        })"));
        framework::VisualProgramServices callbacks;
        callbacks.effects = std::make_shared<api::PresentationEffects>(
            api::EffectRequestServices{{}, api::EffectAccess({}), {}, {}, {}});
        callbacks.create = [](auto&, const auto&, const auto&) -> framework::VisualEffectId {
            throw std::logic_error("pure state requested effect");
        };
        auto state_player = std::make_shared<framework::VisualProgramPlayer>(catalog->visual, std::move(callbacks));
        restored.ConfigureTasks(catalog, state_player, {});
        framework::VisualProgramSnapshot snapshot;
        snapshot.program = "restoring-state";
        framework::VisualContextSnapshot actor;
        actor.name = "actor";
        actor.values = {{"marker", std::string("saved")}};
        snapshot.contexts.push_back(std::move(actor));
        restored.RestoreVisualProgram(snapshot);
        restored.Queue(Domain::Exploration, framework::FreeStyleState{"SomeState"});
        auto foreign = frame;
        int other_world = 0;
        foreign.world = reinterpret_cast<api::GameWorld*>(&other_world);
        ++foreign.sequence;
        restored.Service(foreign, appearance, types);
        Check(state_player->ActiveContexts().empty() && !state_player->Restoring());
        // The pending saved scene belongs to the owned world; neither foreign
        // frame service nor the release-old-task pass may initialize defaults.
        const auto pending = restored.CaptureVisualProgram();
        Check(pending && framework::SameVisualSnapshot(*pending, snapshot));
        auto owned = frame;
        owned.sequence = foreign.sequence + 1;
        const auto before = battle_plays;
        restored.Service(owned, appearance, types);
        Check(restored.Status().phase == Handoff::Ready && restored.Status().error.empty() &&
              battle_plays == before + 1);
        const auto token = state_player->ContextToken("actor");
        Check(token);
        Check(std::get<std::string>(state_player->TaskContextValues(token).at("marker")) == "saved");
        restored.Remove();
        ++owned.sequence;
        restored.Service(owned, appearance, types);
        Check(restored.Status().closed && roots.empty());
    });
    TestSection([&] {
        for (bool alternate : {false, true}) {
            reset_baseline();
            framework::CharacterPlan plan;
            plan.package = package;
            if (alternate)
                plan.alternate = framework::BattlePresentation{{"battle.prefab", {}, {}}, {}};
            auto input = std::make_shared<framework::VisualProgramInput>();
            framework::TaskCatalogInput task_catalog_input(framework::test::ParseLegacyTaskCatalog(R"({
          "format":"hksr.task-catalog","id":"owned","domain":"exploration",
          "visual":{"id":"owned-visual","source":"test","scope":"task","effects":{},
            "contexts":{"enhanced":{"restore":[]},"actor":{"restore":[]}}},
          "contexts":{"enhanced":{"phase":"enhanced","values":{"boosted":true},
            "numbers":{"charge":{"initial":0,"min":0,"max":8}},"callbacks":{},
            "visual_reaction":{"source":"test","flag":133}},
            "actor":{"state_only":true,"values":{"mode":"initial"}}},
          "initial_contexts":["actor"],
          "sequences":{"play":{"entry":{"op":"serial","children":[
            {"op":"animation","request":"body","participant":"body","state":"SomeState"},
            {"op":"wait_request","request":"body"}]}},
            "charge":{"entry":{"op":"serial","children":[
              {"op":"context_enter","request":"context","name":"enhanced"},
              {"op":"context_value","request":"value","name":"enhanced","variable":"charge","operation":"set","value":4},
              {"op":"wait_request","request":"context","signal":"performance"}]}},
            "tail":{"entry":{"op":"repeat","until_finish":true,"child":{"op":"wait_frame"}},
              "interruption":{"op":"serial","children":[{"op":"wait_seconds","seconds":0.2},
                {"op":"animation","request":"cleanup","participant":"body","state":"SomeState"}]}},
            "auto":{"entry":{"op":"animation","request":"auto","participant":"body","state":"SomeState"}}},
          "tasks":{"play":{"entry":"play","requires":[{"op":"one_of","context":"actor","name":"mode","values":["initial","saved"]}]},"charge":{"entry":"charge"},"tail":{"entry":"tail"},
            "set-charge":{"entry":"charge","mode":"input"},
            "auto":{"entry":"auto","trigger":"internal",
              "requires":[{"op":"number","name":"charge","compare":"eq","value":4}]},
            "in_phase":{"entry":"play","trigger":"internal","requires":[{"op":"phase","is":"enhanced"},
              {"op":"target","name":"Target"},{"op":"value","context":"actor","name":"mode","is":"saved"}]}}
        })"));
            task_catalog_input.domain = alternate ? Domain::Battle : Domain::Exploration;
            auto task_catalog = std::make_shared<framework::TaskCatalog>(task_catalog_input);
            input->tasks = task_catalog;
            input->program = input->tasks->visual;
            plan.visual_program = input;
            auto configured = session_services;
            std::shared_ptr<framework::VisualProgramPlayer> player;
            bool restored_target_ready = false;
            bool restored_target_failed = false;
            std::shared_ptr<framework::TaskContextRegistry> receiver_registry;
            configured.visual_program = [&](api::EntityOrigin origin, const framework::VisualProgramInput& value,
                                            const framework::VisualProgramBindings& bindings, const auto&) {
                Check(origin.token.entity == entity_ptr && origin.world == world_ptr);
                Check(bindings.character && bindings.task_contexts);
                if (receiver_registry)
                    Check(receiver_registry == bindings.task_contexts);
                receiver_registry = bindings.task_contexts;
                framework::VisualProgramServices callbacks;
                callbacks.character = bindings.character;
                callbacks.task_contexts = bindings.task_contexts;
                callbacks.effects = std::make_shared<api::PresentationEffects>(
                    api::EffectRequestServices{{}, api::EffectAccess({}), {}, {}, {}});
                callbacks.create = [](auto&, const auto&, const auto&) -> framework::VisualEffectId {
                    throw std::logic_error("unexpected effect");
                };
                callbacks.restore_inputs_ready = [&](const auto& inputs) {
                    if (!inputs.targets.empty() && restored_target_failed)
                        return framework::VisualStepResult{framework::VisualStepState::Failed,
                                                           "target creation failed"};
                    return framework::VisualStepResult{inputs.targets.empty() || restored_target_ready
                                                           ? framework::VisualStepState::Done
                                                           : framework::VisualStepState::Pending,
                                                       "target is still being constructed"};
                };
                callbacks.available_targets = [&](const auto&) {
                    return restored_target_ready ? std::vector<std::string>{"Target"} : std::vector<std::string>{};
                };
                player = std::make_shared<framework::VisualProgramPlayer>(value.program, std::move(callbacks));
                return player;
            };
            framework::SceneCharacters registry(configured,
                                                [&](auto& value, const auto&, const auto&) { value.Create(create); });
            auto current = frame;
            current.manager = {create.manager, {true, 0}};
            current.battle = {false, {true, 0}};
            current.sequence = 1000;
            observed_hash = 37;
            observed_time = 0;
            const framework::SceneAddress address{"task-map", 1};
            const auto id = registry.Add(address.key, plan); // no preparatory idle/action command
            auto live_address = address;
            auto tick = [&] {
                ++current.sequence;
                registry.Service(current, live_address, appearance, types);
            };
            tick();
            auto epoch = registry.Status(id).creations;
            framework::TaskControlInput select{framework::TaskControlOperation::Select,
                                               framework::TaskSelection{{"owned", "play"}}};
            const auto before = battle_plays;
            framework::ApplicationService application;
            framework::ApplicationCoordinator coordinator(application, registry, nullptr, {}, {});
            application.Post(framework::ControlCharacterTask{id, epoch, select});
            auto batch = application.TakeCommands(false);
            Check(batch.size() == 1 && battle_plays == before);
            coordinator.Dispatch(batch.front(), address, current, false);
            const auto replies = application.TakeReplies();
            Check(replies.size() == 1 && replies[0].accepted && replies[0].task->selection->intent != 0 &&
                  battle_plays == before);
            tick();
            tick();
            Check(battle_plays == before + 1);
            Check(registry.ControlTask(id, epoch, {}, current).status.execution->phase ==
                  framework::TaskExecutionPhase::Running);
            const auto task_requests = registry.ControlTask(id, epoch, {}, current).requests;
            Check(task_requests.size() == 1 && task_requests[0].name == "body" && task_requests[0].state.submitted &&
                  task_requests[0].receipt.model != 0);
            // Tasks retain their own identity, not a synthetic normal action.
            Check(!registry.DesiredAction(id));
            Check(registry.CaptureTask(id)->task.name == "play");
            observed_time = 1.1f;
            tick();
            tick();
            Check(registry.ControlTask(id, epoch, {}, current).status.execution->phase ==
                  framework::TaskExecutionPhase::Finished);
            Check(battle_plays == before + 1); // no implicit idle selection
            observed_time = 0;
            registry.ControlTask(id, epoch, select, current);
            tick();
            tick();
            registry.Select(id, {Domain::Exploration, framework::NpcMotion{18}});
            tick();
            const auto stopped = battle_plays;
            tick();
            Check(battle_plays == stopped);
            registry.ControlTask(id, epoch, select, current);
            tick();
            tick();
            Check(battle_plays == stopped + 1);
            bool stale = false;
            try {
                registry.ControlTask(id, epoch + 1, select, current);
            } catch (const std::logic_error&) {
                stale = true;
            }
            Check(stale);
            auto battle = current;
            battle.battle = {true, {true, 0}};
            bool rejected_battle = false;
            try {
                registry.ControlTask(id, epoch, select, battle);
            } catch (const std::logic_error&) {
                rejected_battle = true;
            }
            Check(rejected_battle);
            // A previously accepted task also loses permission when the frame changes,
            // even if the game reuses the same world pointer.
            registry.ControlTask(id, epoch, select, current);
            const auto before_battle = battle_plays;
            current.battle = {true, {true, 0}};
            tick();
            tick();
            current.battle = {false, {true, 0}};
            tick();
            tick();
            Check(battle_plays == before_battle && !registry.ControlTask(id, epoch, {}, current).status.pending);
            Check(registry.ControlTask(id, epoch, {}, current).status.execution->reason ==
                  framework::TaskExitReason::WorldLeft);
            registry.ControlTask(id, epoch, select, current);
            tick();
            tick();
            Check(battle_plays == before_battle + 1);
            // One owned model can play legacy actions without erasing explicit inputs.
            current.delta_time = .1f;
            auto task = [&](const char* name) {
                registry.ControlTask(
                    id, epoch, {framework::TaskControlOperation::Select, framework::TaskSelection{{"owned", name}}},
                    current);
            };
            task("charge");
            tick();
            tick();
            const auto charge_token = player->ContextToken("enhanced");
            Check(charge_token);
            Check(!registry.CaptureVisualReaction(id)); // script ownership must not be promoted to manual state
            task("tail");
            tick();
            const auto tail_plays = battle_plays;
            registry.Select(id, {task_catalog->domain, framework::FreeStyleState{"SomeState"}});
            tick();
            Check(battle_plays == tail_plays); // authored interruption has not reached its final body request
            registry.Select(id, {task_catalog->domain, framework::FreeStyleState{"OtherState"}});
            for (int i = 0; i < 5; ++i)
                tick();
            Check(battle_plays == tail_plays + 2); // old cleanup, then only the latest ordinary selection
            if (registry.CaptureTask(id) || player->ContextToken("enhanced") != charge_token) {
                throw std::runtime_error("ordinary handoff: alternate=" + std::to_string(alternate) + " task=" +
                                         (registry.CaptureTask(id) ? registry.CaptureTask(id)->task.name : "none") +
                                         " context=" + std::to_string(player->ContextToken("enhanced")) +
                                         " expected=" + std::to_string(charge_token));
            }
            Check(std::get<std::int64_t>(player->TaskContextNumbers(charge_token).at("charge")) == 4);
            const auto ordinary_plays = battle_plays;
            for (int i = 0; i < 5; ++i)
                tick();
            Check(battle_plays == ordinary_plays); // automatic task does not retake ordinary playback
            const auto saved = registry.CaptureVisualProgram(id);
            Check(saved && saved->contexts.size() == 2);
            const auto enhanced = std::find_if(saved->contexts.begin(), saved->contexts.end(),
                                               [](const auto& context) { return context.name == "enhanced"; });
            Check(enhanced != saved->contexts.end() && std::get<std::int64_t>(enhanced->numbers.at("charge")) == 4);
            framework::ValidateVisualSnapshot(*saved);
            // A saved ordinary action and its manual state are restored together,
            // without selecting an automatic task instead of that action.
            registry.Rebuild(id);
            reset_baseline();
            for (int i = 0; i < 6; ++i)
                tick();
            epoch = registry.Status(id).creations;
            if (registry.CaptureTask(id) || !player->ContextToken("enhanced")) {
                const auto failed = registry.Status(id);
                throw std::runtime_error(
                    "ordinary restore state: alternate=" + std::to_string(alternate) + " expected_plays=" +
                    std::to_string(ordinary_plays + 1) + " actual_plays=" + std::to_string(battle_plays) +
                    " current=" + (registry.CaptureTask(id) ? registry.CaptureTask(id)->task.name : "none") +
                    " enhanced=" + std::to_string(player->ContextToken("enhanced")) +
                    " actor=" + std::to_string(player->ContextToken("actor")) + " handoff=" +
                    (failed.handoff ? failed.handoff->error : "no-handoff") + " error=" + failed.error + " task=" +
                    (failed.tasks ? failed.tasks->control.preparation_error + failed.tasks->control.status.error
                                  : "no-tasks"));
            }
            Check(std::get<std::int64_t>(player->TaskContextNumbers(player->ContextToken("enhanced")).at("charge")) ==
                  4);
            Check(!registry.CaptureVisualReaction(id) &&
                  !registry.ControlTask(id, epoch, {}, current).manual_reaction);
            if (battle_plays != ordinary_plays + 1)
                throw std::runtime_error(
                    "ordinary restore playback: alternate=" + std::to_string(alternate) +
                    " expected=" + std::to_string(ordinary_plays + 1) + " actual=" + std::to_string(battle_plays) +
                    " handoff=" + registry.Status(id).handoff->error +
                    " task=" + registry.Status(id).tasks->control.status.error +
                    " current=" + (registry.CaptureTask(id) ? registry.CaptureTask(id)->task.name : "none"));
            // The same application command used by menu and HTTP updates manual
            // state without replacing this ordinary action or its saved intent.
            const auto ordinary_action = registry.DesiredAction(id);
            Check(ordinary_action.has_value());
            const auto ordinary_execution = registry.Active(id, current)->ActionStatus();
            Check(ordinary_execution && ordinary_execution->phase == framework::ActionIntentPhase::Returned);
            const auto input_plays = battle_plays;
            const auto input_revision = registry.Status(id).handoff->requested;
            framework::TaskControlInput update{framework::TaskControlOperation::Select,
                                               framework::TaskSelection{{"owned", "set-charge"}}};
            application.Post(framework::ControlCharacterTask{id, epoch, update});
            auto commands = application.TakeCommands(false);
            Check(commands.size() == 1);
            coordinator.Dispatch(commands.front(), live_address, current, false);
            auto accepted = application.TakeReplies();
            Check(accepted.size() == 1 && accepted[0].accepted && accepted[0].task->selection->intent);
            for (int i = 0; i < 5; ++i)
                tick();
            Check(battle_plays == input_plays && registry.Status(id).handoff->requested == input_revision &&
                  !registry.CaptureTask(id));
            const auto after_input = registry.Active(id, current)->ActionStatus();
            Check(after_input && after_input->request == ordinary_execution->request &&
                  after_input->phase == ordinary_execution->phase && after_input->outcome == ordinary_execution->outcome);
            Check(registry.DesiredAction(id)->domain == ordinary_action->domain &&
                  std::get<framework::FreeStyleState>(registry.DesiredAction(id)->request).name ==
                      std::get<framework::FreeStyleState>(ordinary_action->request).name);
            const auto input_status = registry.ControlTask(id, epoch, {}, current).status;
            Check(input_status.input &&
                  input_status.input->execution->phase == framework::TaskExecutionPhase::Finished &&
                  input_status.input->error.empty());
            registry.Rebuild(id);
            reset_baseline();
            for (int i = 0; i < 6; ++i)
                tick();
            epoch = registry.Status(id).creations;
            Check(!registry.CaptureTask(id) && registry.DesiredAction(id)->domain == ordinary_action->domain &&
                  std::get<framework::FreeStyleState>(registry.DesiredAction(id)->request).name ==
                      std::get<framework::FreeStyleState>(ordinary_action->request).name);
            Check(std::get<std::int64_t>(player->TaskContextNumbers(player->ContextToken("enhanced")).at("charge")) ==
                  4);
            // Returning to a task re-enables its control; leaving the domain still retires its inputs.
            task("play");
            tick();
            tick();
            Check(battle_plays > ordinary_plays);
            registry.Select(id, {Domain::Exploration, framework::NpcMotion{18}});
            tick();
            tick();
            tick();
            Check(bool(player->ContextToken("enhanced")) == !alternate);
            current.battle = {true, {true, 0}};
            tick();
            tick();
            Check(!player->ContextToken("enhanced"));
            current.battle = {false, {true, 0}};
            tick();
            registry.Delete(id);
            tick();
            tick();
            Check(registry.Ids().empty() && player->Drained() && roots.empty());
            // Restoration uses the same session/model/context path in both domains.
            // The task's precondition is false until the saved context is rebuilt.
            reset_baseline();
            observed_time = 0;
            auto saved_plan = plan;
            framework::CharacterRestoreState saved_restore;
            saved_restore.visual_state = framework::VisualProgramSnapshot{"owned-visual", {}, {{"enhanced", {}, {}}}};
            framework::VisualContextSnapshot actor_state;
            actor_state.name = "actor";
            actor_state.values = {{"mode", std::string("saved")}};
            saved_restore.visual_state->contexts.push_back(std::move(actor_state));
            saved_restore.task_selection = framework::TaskSelection{{"owned", "in_phase"}};
            saved_restore.task_selection->visual.targets["Target"] = 55;
            const auto restored_id = registry.Add(address.key, saved_plan, {}, {}, saved_restore);
            const auto prior_plays = battle_plays;
            tick();
            tick();
            tick();
            Check(battle_plays == prior_plays && registry.Status(restored_id).tasks->control.preparing);
            restored_target_ready = true;
            tick();
            tick();
            Check(battle_plays == prior_plays + 1 && player->ContextToken("enhanced"));
            Check(registry.CaptureTask(restored_id)->task.name == "in_phase");
            Check(std::get<std::string>(player->TaskContextValues(player->ContextToken("actor")).at("mode")) ==
                  "saved");
            const auto restored_epoch = registry.Status(restored_id).creations;
            const auto denied = registry.ControlTask(
                restored_id, restored_epoch,
                {framework::TaskControlOperation::Select, framework::TaskSelection{{"owned", "in_phase"}}}, current);
            Check(denied.selection && denied.selection->query.availability == framework::TaskAvailability::Unavailable);
            ++current.sequence;
            registry.Service(current, framework::SceneAddress{"away", 2}, appearance, types);
            Check(!registry.Status(restored_id).active && registry.CaptureTask(restored_id)->task.name == "in_phase");
            reset_baseline();
            live_address.incarnation = 3;
            tick();
            tick();
            tick();
            Check(battle_plays == prior_plays + 2 && player->ContextToken("enhanced"));
            registry.Rebuild(restored_id);
            restored_target_failed = true;
            reset_baseline();
            tick();
            tick();
            tick();
            Check(registry.Status(restored_id).tasks->control.preparation_error.find("target creation failed") !=
                  std::string::npos);
            Check(registry.CaptureTask(restored_id)->task.name == "in_phase");
            const auto recreated = registry.Status(restored_id).creations;
            registry.ControlTask(restored_id, recreated, {framework::TaskControlOperation::Cancel, {}}, current);
            Check(!registry.CaptureTask(restored_id));
            registry.Delete(restored_id);
            tick();
            tick();
            Check(registry.Ids().empty() && player->Drained() && roots.empty());
            if (alternate) {
                reset_baseline();
                restored_target_failed = false;
                saved_plan.alternate.reset();
                const auto unavailable = registry.Add(address.key, saved_plan, {}, {}, saved_restore);
                tick();
                tick();
                Check(registry.Status(unavailable).active &&
                      !registry.Status(unavailable).tasks->control.preparation_error.empty());
                Check(registry.CaptureTask(unavailable)->task.name == "in_phase");
                const auto previous = npc_plays;
                registry.Select(unavailable, {Domain::Exploration, framework::NpcMotion{18}});
                tick();
                Check(npc_plays == previous + 1 && !registry.CaptureTask(unavailable));
                registry.Delete(unavailable);
                tick();
                tick();
                Check(registry.Ids().empty() && roots.empty());
            }
        }
    });
    {
        reset_baseline();
        framework::PreparedSource prepared;
        prepared.summary.character = package;
        prepared.summary.finished = true;
        auto plan = framework::CharacterPlan::FromPrepared(prepared);
        Check(plan.action_sources.empty() && !plan.alternate);
        framework::CharacterSession session(world_ptr, std::move(plan), session_services);
        const auto first_action = session.Queue(Domain::Exploration, framework::NpcMotion{18});
        Check(session.ActionStatus() && session.ActionStatus()->request == first_action &&
              session.ActionStatus()->phase == framework::ActionIntentPhase::Waiting &&
              !session.ActionStatus()->settled_successor);
        session.Create(create);
        const auto count = npc_plays, begin_count = begins;
        // Pure NPC does not need an event Type, baseline reader or battle config.
        session.Service(frame, appearance, {});
        Check(session.Status().phase == framework::SessionPhase::Active && npc_plays == count + 1 &&
              begins == begin_count);
        Check(!session.Status().baseline_attempted && !session.Handoff()->ModelRequest());
        Check(session.ActionStatus()->request == first_action &&
              session.ActionStatus()->phase == framework::ActionIntentPhase::Returned);
        const auto unsupported = session.Queue(Domain::Battle, framework::FreeStyleState{"SomeState"});
        Check(session.ActionStatus()->request == unsupported &&
              session.ActionStatus()->phase == framework::ActionIntentPhase::Waiting &&
              !session.ActionStatus()->outcome && !session.ActionStatus()->settled_successor);
        session.Service(frame, appearance, {});
        Check(!session.Status().errors.empty() && !session.Character().Creation().removal_requested &&
              begins == begin_count);
        Check(session.Status().submitted_action == unsupported && !session.Status().handoff_request &&
              !session.Status().dispatch_error.empty());
        Check(session.ActionStatus()->request == unsupported &&
              session.ActionStatus()->phase == framework::ActionIntentPhase::Failed &&
              !session.ActionStatus()->detail.empty() && !session.ActionStatus()->settled_successor);
        const auto recovered = session.Queue(Domain::Exploration, framework::AnimatorState{"SomeState"});
        Check(session.ActionStatus()->request == recovered &&
              session.ActionStatus()->phase == framework::ActionIntentPhase::Waiting &&
              session.ActionStatus()->detail != session.Status().dispatch_error && !session.ActionStatus()->outcome);
        session.Service(frame, appearance, {});
        Check(session.Handoff()->Playback().last->outcome == framework::ActionOutcome::Returned);
        Check(session.Status().dispatch_error.empty() &&
              session.Status().handoff_request == session.Handoff()->Status().servicing);
        session.Remove();
        session.Service(frame, appearance, {});
        Check(session.Status().closed && roots.empty());
    }
    {
        reset_baseline();
        framework::CharacterPlan plan;
        plan.package = package;
        plan.alternate = framework::BattlePresentation{{"battle.prefab", {}, {}}, {}};
        auto input = std::make_shared<framework::VisualProgramInput>();
        input->program = std::make_shared<framework::VisualProgram>(framework::test::ParseLegacyVisualProgram(R"({
            "format":"hksr.visual-program","id":"session","source":"battle","scope":"scene","effects":{},
            "restore":[{"op":"timeline_play","source":"restore","key":"scene","path":"scene.playable"}],
            "phases":{"enhanced":{"root":{"kind":"ability","name":"Phase"},"path":[{"owner":"Config","field":"Value"}]}},
            "contexts":{"phase":{"phase":"enhanced","restore":[]}},
            "actions":{"play":{"state":"SomeState","restart":true,"enter":[
              {"op":"context_enter","name":"phase","source":"phase"},
              {"op":"timeline_play","source":"scene","key":"scene","path":"scene.playable"}]},
              "continue":{"state":"SomeState"},
              "other":{"state":"SomeState","restart":true,"enter":[
              {"op":"timeline_play","source":"other","key":"scene","path":"other.playable"}]}}})"));
        plan.visual_program = input;
        auto registry_plan = plan;
        registry_plan.alternate->initial_state = {true, "OtherState", {}};
        auto configured = session_services;
        std::shared_ptr<framework::VisualProgramPlayer> player;
        auto begins_before = begins;
        int preparations = 0;
        bool scene_alive = false;
        std::string scene_path;
        configured.visual_program = [&](api::EntityOrigin owner, const framework::VisualProgramInput& value,
                                        const auto&, const auto&) {
            Check(owner.token.entity == entity_ptr && owner.world == world_ptr && begins == begins_before);
            ++preparations;
            framework::VisualProgramServices callbacks;
            callbacks.effects = std::make_shared<api::PresentationEffects>(
                api::EffectRequestServices{{}, api::EffectAccess({}), {}, {}, {}});
            callbacks.create = [](auto&, const auto&, const auto&) -> framework::VisualEffectId {
                throw std::logic_error("unexpected effect");
            };
            callbacks.timeline = [&](auto, const auto& op) {
                scene_alive = true;
                scene_path = op.path;
                return framework::VisualStepResult{framework::VisualStepState::Done, {}};
            };
            callbacks.cancel_timelines = [&](auto) { scene_alive = false; };
            callbacks.timelines_drained = [&] { return !scene_alive; };
            callbacks.read_phase = [](const auto&) {
                auto phase = std::make_shared<api::PhaseVisualConfigSources>();
                phase->visuals.context_available = true;
                phase->standby = {true, "SomeState", {}};
                return phase;
            };
            player = std::make_shared<framework::VisualProgramPlayer>(value.program, std::move(callbacks));
            return player;
        };
        framework::CharacterSession session(world_ptr, std::move(plan), configured);
        session.Create(create);
        session.Queue(Domain::Battle, framework::ProgramAction{"session", "play"});
        session.Service(frame, appearance, types);
        if (!player || !player->Status().playing || !scene_alive || scene_path != "scene.playable") {
            std::cerr << "program session phase=" << static_cast<int>(session.Status().phase)
                      << " preparations=" << preparations;
            if (session.Handoff())
                std::cerr << " handoff=" << static_cast<int>(session.Handoff()->Status().phase)
                          << " error=" << session.Handoff()->Status().error;
            if (player)
                for (const auto& issue : player->Status().issues)
                    std::cerr << " program_error=" << issue;
            std::cerr << '\n';
        }
        Check(preparations == 1 && player && player->Status().playing && scene_alive && scene_path == "scene.playable");
        Check(std::get<framework::ProgramAction>(session.Handoff()->Status().output->request).action == "play");
        Check(session.Handoff()->CurrentIdle() == "SomeState");
        session.Queue(Domain::Exploration, framework::NpcMotion{18});
        session.Service(frame, appearance, types);
        Check(!scene_alive && preparations == 1);
        session.Queue(Domain::Battle, framework::ProgramAction{"session", "other"});
        session.Service(frame, appearance, types);
        Check(scene_alive && preparations == 1 &&
              scene_path == "other.playable"); // retained player, distinct presentation on the same body
        Check(std::get<framework::ProgramAction>(session.Handoff()->Status().output->request).action == "other");
        session.Remove();
        session.Service(frame, appearance, types);
        Check(session.Status().closed && player->Drained() && roots.empty());
        reset_baseline();
        begins_before = begins;
        framework::SceneCharacters registry(configured,
                                            [&](auto& value, const auto&, const auto&) { value.Create(create); });
        auto current = frame;
        current.manager = {create.manager, {true, 0}};
        current.battle = {false, {true, 0}};
        observed_hash = 41;
        const framework::SceneAddress address{"program-map", 1};
        const auto id =
            registry.Add(address.key, registry_plan,
                         framework::PlacedAction{Domain::Battle, framework::ProgramAction{"session", "play"}});
        Check(registry.Status(id).battle_idle_state == "OtherState");
        registry.Service(current, address, appearance, types);
        ++current.sequence;
        registry.Service(current, address, appearance, types);
        Check(registry.Status(id).battle_idle_state == "SomeState");
        // This is the same selection used by ApplicationCoordinator::IdleCharacter.
        registry.Select(id, {Domain::Battle, framework::BattleIdle{}});
        ++current.sequence;
        registry.Service(current, address, appearance, types);
        Check(registry.Status(id).playback->last->hash.value == 37 && player->ContextToken("phase"));
        const auto snapshot = registry.CaptureVisualProgram(id);
        Check(snapshot && snapshot->contexts.size() == 1 && !snapshot->scene);
        registry.Rebuild(id);
        begins_before = begins;
        ++current.sequence;
        registry.Service(current, address, appearance, types);
        ++current.sequence;
        registry.Service(current, address, appearance, types);
        Check(registry.Status(id).battle_idle_state == "SomeState" && player->ContextToken("phase"));
        registry.Select(id, {Domain::Battle, framework::ProgramAction{"session", "play"}});
        ++current.sequence;
        registry.Service(current, address, appearance, types);
        registry.Select(id, {Domain::Battle, framework::ProgramAction{"session", "continue"}});
        ++current.sequence;
        registry.Service(current, address, appearance, types);
        Check(scene_alive);
        ++current.sequence;
        registry.Service(current, framework::SceneAddress{"away", 2}, appearance, types);
        Check(!registry.Status(id).active && !scene_alive && registry.CaptureRestoreState(id).visual_state->scene);
        begins_before = begins;
        observed_hash = 41;
        const framework::SceneAddress returned{address.key, 3}; // a new map incarnation
        ++current.sequence;
        registry.Service(current, returned, appearance, types);
        Check(!scene_alive); // initialization output has not been observed yet
        ++current.sequence;
        registry.Service(current, returned, appearance, types);
        if (!scene_alive || player->Status().action != "continue" || !player->Status().playing) {
            const auto state = registry.Status(id);
            std::cerr << "restored program: active=" << state.active << " error=" << state.error
                      << " creations=" << state.creations;
            if (state.handoff)
                std::cerr << " handoff=" << static_cast<int>(state.handoff->phase) << " error=" << state.handoff->error;
            if (state.action)
                std::cerr << " action=" << state.action->detail;
            for (const auto& issue : player->Status().issues)
                std::cerr << " program_error=" << issue;
            std::cerr << " restoring=" << player->Restoring() << " prepared=" << player->Status().prepared
                      << " playing=" << player->Status().playing << '\n';
        }
        Check(scene_alive && scene_path == "scene.playable" && player->Status().action == "continue" &&
              player->Status().playing);
        Check(registry.Status(id).battle_idle_state == "SomeState");
        registry.Select(id, {Domain::Exploration, framework::NpcMotion{18}});
        ++current.sequence;
        registry.Service(current, returned, appearance, types);
        Check(!registry.CaptureVisualProgram(id));
        registry.Delete(id);
        ++current.sequence;
        registry.Service(current, returned, appearance, types);
        Check(registry.Ids().empty() && roots.empty());
    }
    {
        reset_baseline();
        framework::CharacterPlan plan;
        plan.package = package;
        plan.alternate = framework::BattlePresentation{{"battle.prefab", {}, {}}, {}};
        plan.visual_program = std::make_shared<framework::VisualProgramInput>();
        auto configured = session_services;
        int attempts = 0;
        configured.visual_program = [&](api::EntityOrigin, const auto&,
                                        const auto&, const auto&) -> std::shared_ptr<framework::VisualProgramPlayer> {
            ++attempts;
            throw std::runtime_error("program assembly failed");
        };
        framework::CharacterSession session(world_ptr, std::move(plan), configured);
        session.Create(create);
        session.Queue(Domain::Battle, framework::FreeStyleState{"SomeState"});
        const auto plays = battle_plays;
        session.Service(frame, appearance, types);
        Check(attempts == 1 && battle_plays == plays && !session.Status().errors.empty());
        session.Service(frame, appearance, types);
        Check(attempts == 1 && session.Status().closed && roots.empty());
    }
    {
        reset_baseline();
        framework::CharacterPlan plan;
        plan.package = package;
        plan.alternate = framework::BattlePresentation{{"battle.prefab", {}, {}}, {}};
        framework::CharacterSession session(world_ptr, std::move(plan), session_services);
        session.Queue(Domain::Battle, framework::FreeStyleState{"SomeState"});
        session.Create(create);
        const auto count = battle_plays, begin_count = begins;
        load_state = 4;
        session.Service(frame, appearance, types);
        Check(session.Status().phase == framework::SessionPhase::PreparingBaseline && begins == begin_count &&
              !session.Status().baseline_attempted);
        load_state = 5;
        session.Service(foreign, appearance, types);
        Check(!session.Status().baseline_attempted);
        session.Service(frame, appearance, types);
        Check(session.Status().alternate_available && battle_plays == count + 1 && begins == begin_count + 1);
        session.Queue(Domain::Exploration, framework::NpcMotion{18});
        session.Service(frame, appearance, types);
        Check(current_path == &baseline_path && current_config == &baseline_entity_config &&
              consumer.paths == &baseline_events);
        session.Remove();
        session.Service(frame, appearance, types);
        Check(session.Status().closed && roots.empty());
    }
    {
        reset_baseline();
        framework::CharacterPlan plan;
        plan.package = package;
        plan.alternate = framework::BattlePresentation{{"battle.prefab", {}, {}}, {}};
        framework::CharacterSession session(world_ptr, std::move(plan), session_services);
        session.Create(create);
        session.Queue(Domain::Exploration, framework::NpcMotion{18});
        constructor_fault = true;
        const auto count = npc_plays, ctor_count = initializes;
        session.Service(frame, appearance, types);
        Check(session.Status().phase == framework::SessionPhase::Active && !session.Status().alternate_available &&
              npc_plays == count + 1);
        Check(!session.Status().errors.empty() && initializes == ctor_count + 1);
        session.Service(frame, appearance, types);
        Check(initializes == ctor_count + 1 && npc_plays == count + 1);
        constructor_fault = false;
        session.Remove();
        session.Service(frame, appearance, types);
        Check(session.Status().closed && roots.empty());
    }
    {
        reset_baseline();
        framework::CharacterPlan plan;
        plan.package = package;
        plan.alternate = framework::BattlePresentation{{"battle.prefab", {}, {}}, {}};
        framework::CharacterSession session(world_ptr, std::move(plan), session_services);
        session.Create(create);
        load_state = 4;
        session.Service(frame, appearance, types);
        api::TrackedEntities().Disposed(api::TrackedEntities().Current(entity_ptr));
        session.Service(foreign, appearance, types);
        Check(session.Status().closed && roots.empty());
        load_state = 5;
    }
    {
        framework::CharacterPlan plan;
        plan.package = package;
        framework::CharacterSession session(world_ptr, std::move(plan), session_services);
        session.Remove();
        session.Service(frame, appearance, {});
        Check(session.Status().closed && roots.empty());
    }
    {
        reset_baseline();
        framework::CharacterPlan plan;
        plan.package = package;
        auto configured = session_services;
        unsigned registered = 0, audio_rate = 0, cleared = 0, timings = 0, scales = 0, removes = 0, closes = 0;
        bool removal_ready = false, audio_fault = false;
        configured.audio = [&](auto, auto*, unsigned rate) {
            ++registered;
            audio_rate = rate;
        };
        configured.audio_rate = [&](auto, auto*, unsigned rate) {
            if (audio_fault)
                throw std::runtime_error("audio rate failure");
            audio_rate = rate;
        };
        configured.clear_audio = [&](auto, auto*) { ++cleared; };
        configured.timing = [&](auto, auto*) {
            ++timings;
            api::ActorTimingServices calls;
            calls.scale = [&](const auto& input, const auto& key) {
                Check(input.factor == 2 && !input.duration && key == "hksr.actor.playback");
                return api::ActorTimeScaleToken{++scales};
            };
            calls.remove = [&](auto token, bool fade) {
                Check(token.value && !fade);
                if (!removal_ready)
                    return false;
                ++removes;
                return true;
            };
            calls.close = [&] {
                ++closes;
                return true;
            };
            return std::make_unique<api::ActorTiming>(std::move(calls));
        };
        framework::CharacterSession session(world_ptr, std::move(plan), configured);
        session.Create(create);
        session.Service(frame, appearance, {});
        Check(registered == 1 && audio_rate == 1 && !scales && !timings);
        audio_fault = true;
        bool audio_failed = false;
        try {
            session.SetPlaybackRate(2);
        } catch (const std::runtime_error&) {
            audio_failed = true;
        }
        Check(audio_failed && !scales && session.PlaybackRate() == 1 && audio_rate == 1);
        audio_fault = false;
        session.SetPlaybackRate(2);
        Check(scales == 1 && timings == 1 && audio_rate == 2 && session.PlaybackRate() == 2);
        session.Queue(Domain::Exploration, framework::NpcMotion{18});
        session.Service(frame, appearance, {});
        Check(scales == 1 && removes == 0 && registered == 1); // action changes do not own the rate
        bool remove_failed = false;
        try {
            session.SetPlaybackRate(1);
        } catch (const std::exception&) {
            remove_failed = true;
        }
        Check(remove_failed && session.PlaybackRate() == 2 && audio_rate == 2 && removes == 0);
        removal_ready = true;
        session.SetPlaybackRate(1);
        Check(removes == 1 && audio_rate == 1);
        session.SetPlaybackRate(2);
        session.Remove();
        session.Service(frame, appearance, {});
        Check(cleared == 1 && closes == 1 && session.Status().closed && roots.empty());
    }
    for (bool audio_unavailable : {true, false}) {
        reset_baseline();
        framework::CharacterPlan plan;
        plan.package = package;
        framework::CharacterRestoreState restore;
        restore.playback_rate = 2;
        auto configured = session_services;
        bool broken = true;
        unsigned rate = 1, removed = 0, cleared = 0;
        configured.audio = [&](auto, auto*, unsigned value) {
            if (audio_unavailable && broken)
                throw std::runtime_error("audio unavailable");
            rate = value;
        };
        configured.audio_rate = [&](auto, auto*, unsigned value) {
            if (audio_unavailable && broken)
                throw std::runtime_error("audio unavailable");
            rate = value;
        };
        configured.clear_audio = [&](auto, auto*) { ++cleared; };
        configured.timing = [&](auto, auto*) {
            api::ActorTimingServices calls;
            calls.scale = [&](const auto&, const auto&) {
                return api::ActorTimeScaleToken{!audio_unavailable && broken ? 0u : 1u};
            };
            calls.remove = [&](auto, bool) {
                ++removed;
                return true;
            };
            calls.close = [] { return true; };
            return std::make_unique<api::ActorTiming>(std::move(calls));
        };
        framework::CharacterSession session(world_ptr, std::move(plan), configured, restore);
        session.Create(create);
        session.Service(frame, appearance, {});
        Check(session.Status().phase == framework::SessionPhase::Active && !session.Status().closed &&
              !session.Status().playback_error.empty() && session.PlaybackRate() == 1 && rate == 1);
        const auto before = npc_plays;
        session.Queue(Domain::Exploration, framework::NpcMotion{18});
        session.Service(frame, appearance, {});
        Check(npc_plays == before + 1); // unavailable speed/audio does not remove ordinary playback
        broken = false;
        session.SetPlaybackRate(2);
        Check(session.PlaybackRate() == 2 && rate == 2 && session.Status().playback_error.empty());
        broken = audio_unavailable;
        session.SetPlaybackRate(1);
        Check(session.PlaybackRate() == 1 && removed == 1);
        if (audio_unavailable)
            Check(!session.Status().playback_error.empty() && cleared == 1);
        session.Remove();
        session.Service(frame, appearance, {});
        Check(session.Status().closed && roots.empty());
    }
    {
        reset_baseline();
        framework::CharacterPlan plan;
        plan.package = package;
        framework::CharacterRestoreState restore;
        restore.playback_rate = 2;
        plan.source_intent = framework::BrowseNpc{77};
        framework::SceneCharacters registry(session_services,
                                            [&](auto& session, const auto&, const auto&) { session.Create(create); });
        const auto id = registry.Add("Map/Speed", std::move(plan), {}, {}, restore);
        auto scene_frame = frame;
        scene_frame.manager = {create.manager, {true, 0}};
        scene_frame.battle = {false, {true, 0}};
        const framework::SceneAddress scene_address{"Map/Speed", 1};
        registry.Service(scene_frame, scene_address, appearance, {});
        Check(registry.Status(id).active && registry.Status(id).playback_rate == 1 &&
              !registry.Status(id).playback_error.empty());
        Check(registry.CaptureRestoreState(id).playback_rate == 1 &&
              framework::CaptureSceneLayout(registry, "Map/Speed").characters[0].restore.playback_rate == 1);
        registry.Rebuild(id);
        registry.SetPlaybackRate(id, 2); // update the next creation, not the superseded session
        Check(registry.CaptureRestoreState(id).playback_rate == 2);
        registry.Service(scene_frame, scene_address, appearance, {});
        Check(registry.Status(id).active && registry.Status(id).creations == 2 &&
              registry.Status(id).playback_rate == 1);
        registry.Delete(id);
        registry.Service(scene_frame, scene_address, appearance, {});
        Check(registry.Ids().empty() && roots.empty());
    }
    std::cout << "CharacterSession passed: prepared sources, no battle prerequisite for NPC, baseline capture before "
                 "handoff, pending load, independent failure, natural disposal and removal; local fixtures only.\n";
    TestSection([&] {
        reset_baseline();
        framework::CharacterPlan plan;
        plan.package = package;
        auto configured = session_services;
        framework::VisualProgramBindings runtime_bindings;
        configured.visual_program = [&](api::EntityOrigin origin, const framework::VisualProgramInput& value,
                                        const framework::VisualProgramBindings& bindings, const auto&) {
            Check(origin.token.entity == entity_ptr && origin.world == world_ptr);
            runtime_bindings = bindings;
            framework::VisualProgramServices callbacks;
            callbacks.effects = std::make_shared<api::PresentationEffects>(
                api::EffectRequestServices{{}, api::EffectAccess({}), {}, {}, {}});
            callbacks.create = [](auto&, const auto&, const auto&) -> framework::VisualEffectId {
                throw std::logic_error("unexpected effect in target binding fixture");
            };
            return std::make_shared<framework::VisualProgramPlayer>(value.program, std::move(callbacks));
        };
        framework::SceneCharacters registry(configured,
                                            [&](auto& session, const auto&, const auto&) { session.Create(create); });
        const framework::SceneAddress target_scene{"Map/Targets", 1};
        auto current = frame;
        current.manager = {create.manager, {true, 0}};
        current.battle = {false, {true, 0}};
        const auto target = registry.Add(target_scene.key, plan);
        registry.SetEnabled(target, false);
        auto input = std::make_shared<framework::VisualProgramInput>();
        input->program = std::make_shared<const framework::VisualProgram>(framework::test::ParseLegacyVisualProgram(
            R"({"format":"hksr.visual-program","id":"target-bindings","source":"fixture","scope":"scene",
              "effects":{},"actions":{"hold":{"state":"SomeState"}}})"));
        input->target_characters.emplace("Target", target);
        plan.visual_program = input;
        const auto visual_source = registry.Add(target_scene.key, plan);
        Check(registry.Plan(visual_source).visual_program == input);
        registry.Service(current, target_scene, appearance, {});
        Check(runtime_bindings.character == visual_source && runtime_bindings.task_contexts &&
              runtime_bindings.placement_intent);
        const auto resolve_target = runtime_bindings.resolve_character;
        const auto source_placement = runtime_bindings.placement_intent;
        const auto source_contexts = runtime_bindings.task_contexts;
        Check(resolve_target && !resolve_target(target).token.entity && !resolve_target(999).token.entity);
        auto transient_plan = plan;
        transient_plan.persistence = framework::CharacterPersistence::Session;
        transient_plan.visual_program.reset();
        const auto transient = registry.Add(target_scene.key, transient_plan);
        registry.SetEnabled(transient, false);
        bool transient_rejected = false;
        try {
            resolve_target(transient);
        } catch (const std::invalid_argument&) {
            transient_rejected = true;
        }
        Check(transient_rejected);
        registry.Delete(transient);
        registry.SetEnabled(visual_source, false);
        registry.Service(current, target_scene, appearance, {});
        const auto other_source = registry.Add(target_scene.key, plan);
        registry.Service(current, target_scene, appearance, {});
        Check(registry.Plan(other_source).visual_program == input && runtime_bindings.character == other_source &&
              runtime_bindings.placement_intent != source_placement && runtime_bindings.task_contexts == source_contexts);
        registry.Delete(other_source);
        registry.Service(current, target_scene, appearance, {});
        registry.SetEnabled(target, true);
        registry.Service(current, target_scene, appearance, {});
        const auto first = resolve_target(target);
        Check(first.token.entity == entity_ptr && first.world == world_ptr);
        registry.Rebuild(target);
        Check(!resolve_target(target).token.entity);
        registry.Service(current, target_scene, appearance, {});
        const auto second = resolve_target(target);
        Check(second.token.entity == entity_ptr && second.token.generation != first.token.generation);
        registry.SetEnabled(target, false);
        Check(!resolve_target(target).token.entity);
        registry.Service(current, target_scene, appearance, {});
        registry.Delete(target);
        registry.Delete(visual_source);
        registry.Service(current, target_scene, appearance, {});
        Check(!resolve_target(target).token.entity && registry.Ids().empty() && roots.empty());
        registry.Shutdown();
        registry.Service(current, target_scene, appearance, {});
        Check(registry.Closed());
    });
    TestSection([&] {
        reset_baseline();
        auto scene_frame = frame;
        scene_frame.manager = {create.manager, {true, 0}};
        scene_frame.battle = {false, {true, 0}};
        auto away = scene_frame;
        away.world = foreign.world;
        framework::CharacterPlan plan;
        plan.package = package;
        framework::SceneCharacters registry(
            session_services, [&](auto& session, const auto& current, const auto& placement) {
                Check(current.world == world_ptr && placement.id == 1 && placement.placement.position.x == 12);
                session.Create(create);
            });
        const auto id =
            registry.Add("Map/A", plan, framework::PlacedAction{Domain::Exploration, framework::NpcMotion{18}},
                         {{12, 3, 4}, {0, 0, 0, 1}});
        std::vector<api::OperationTraceReport> timings;
        registry.ConfigureTiming([&](const auto& report) { timings.push_back(report); });
        registry.EnableTiming(true);
        const framework::SceneAddress a{"Map/A", 1}, b{"Map/B", 2}, returned{"Map/A", 3};
        auto count = npc_plays;
        registry.Service(scene_frame, a, appearance, {});
        Check(registry.Status(id).active && registry.Status(id).creations == 1 && npc_plays == count + 1);
        Check(registry.Status(id).action->id == 1 && registry.Status(id).action->creation == 1 &&
              registry.Status(id).action->phase == framework::ActionIntentPhase::Returned);
        registry.Service(scene_frame, std::nullopt, appearance, {});
        const auto timing_count = timings.size();
        registry.Service(scene_frame, framework::SceneAddress{}, appearance, {});
        Check(timings.size() == timing_count); // Stable frames do not collect native calls.
        Check(registry.Status(id).active &&
              registry.Status(id).creations == 1); // missing identity is not map departure
        const auto superseded = registry.Select(id, {Domain::Exploration, framework::AnimatorState{"OtherState"}});
        const auto action_id = registry.Select(id, {Domain::Exploration, framework::AnimatorState{"SomeState"}});
        Check(action_id > superseded && registry.Status(id).action->id == action_id &&
              registry.Status(id).action->phase == framework::ActionIntentPhase::Waiting);
        registry.Service(scene_frame, a, appearance, {});
        Check(registry.Status(id).action->id == action_id &&
              registry.Status(id).action->phase == framework::ActionIntentPhase::Returned);
        registry.Service(away, b, appearance, {});
        Check(!registry.Status(id).active && registry.Status(id).retiring == 1);
        Check(registry.Status(id).action->id == action_id &&
              registry.Status(id).action->phase == framework::ActionIntentPhase::Waiting &&
              !registry.Status(id).action->outcome);
        api::TrackedEntities().Disposed(api::TrackedEntities().Current(entity_ptr));
        registry.Service(away, b, appearance, {});
        Check(registry.Status(id).retiring == 0 && roots.empty());
        auto animator_count = animator_plays;
        registry.Service(scene_frame, returned, appearance, {});
        Check(registry.Status(id).creations == 2 &&
              animator_plays == animator_count + 1); // replay latest selected action, from its request start
        Check(std::any_of(timings.begin(), timings.end(), [&](const auto& r) {
            return r.subject == id && r.generation == 1 && r.rows[0].stage == "character.retire";
        }));
        Check(std::any_of(timings.begin(), timings.end(), [&](const auto& r) {
            return r.subject == id && r.generation == 2 && r.rows[0].stage == "character.create";
        }));
        registry.EnableTiming(false);
        const auto stopped_timings = timings.size();
        Check(registry.Status(id).action->id == action_id && registry.Status(id).action->creation == 2 &&
              registry.Status(id).action->phase == framework::ActionIntentPhase::Returned);
        registry.SetEnabled(id, false);
        registry.Service(scene_frame, returned, appearance, {});
        Check(!registry.Status(id).active && roots.empty());
        registry.Service(away, b, appearance, {});
        registry.Service(scene_frame, returned, appearance, {});
        Check(registry.Status(id).creations == 2); // manually removed stays absent
        registry.SetEnabled(id, true);
        registry.Service(scene_frame, returned, appearance, {});
        Check(registry.Status(id).creations == 3);
        registry.Rebuild(id);
        registry.Service(scene_frame, returned, appearance, {});
        Check(registry.Status(id).creations == 4 && registry.Status(id).active);
        Check(timings.size() == stopped_timings); // Disabling collection leaves lifecycle behavior unchanged.
        registry.Look(id, {framework::LookMode::Point, 0, {8, 9, 10}});
        registry.Select(id, {Domain::Exploration, framework::NpcMotion{18}});
        auto variant = plan;
        variant.package.id = "another-variant";
        const auto before_npc = npc_plays, before_animator = animator_plays;
        registry.ReplacePlan(id, variant);
        variant.package.id = "latest-variant";
        registry.ReplacePlan(id, variant);
        registry.Service(scene_frame, returned, appearance, {});
        Check(registry.Ids() == std::vector<std::uint64_t>{id} && registry.Status(id).creations == 5);
        Check(registry.Status(id).package.id == "latest-variant" && registry.Status(id).scene == "Map/A");
        Check(registry.Status(id).placement.position.x == 12 && registry.Status(id).look.point.z == 10);
        Check(npc_plays == before_npc && animator_plays == before_animator); // no old-source action replay
        Check(!registry.Status(id).action);
        registry.SetEnabled(id, false);
        registry.Service(scene_frame, returned, appearance, {});
        registry.ReplacePlan(id, plan);
        registry.Service(scene_frame, returned, appearance, {});
        Check(!registry.Status(id).enabled && !registry.Status(id).active && registry.Status(id).creations == 5);
        registry.SetEnabled(id, true);
        registry.Service(scene_frame, returned, appearance, {});
        Check(registry.Status(id).creations == 6 && registry.Status(id).look.point.z == 10);
        registry.Delete(id);
        Check(registry.Status(id).deleting && !registry.Status(id).enabled);
        bool gone = false;
        try {
            registry.Rebuild(id);
        } catch (const std::logic_error&) {
            gone = true;
        }
        Check(gone);
        registry.Service(away, b, appearance, {});
        Check(registry.Contains(id) && registry.Status(id).deleting && registry.Status(id).retiring == 1);
        registry.Service(scene_frame, returned, appearance, {});
        Check(!registry.Contains(id) && registry.Ids().empty() && roots.empty());
        registry.Service(away, b, appearance, {});
        registry.Service(scene_frame, returned, appearance, {});
        Check(registry.Ids().empty()); // deletion cannot return on revisiting a map
        registry.Shutdown();
        registry.Service(scene_frame, returned, appearance, {});
        Check(registry.Closed() && roots.empty());
    });
    TestSection([&] {
        reset_baseline();
        auto scene_frame = frame;
        scene_frame.manager = {create.manager, {true, 0}};
        scene_frame.battle = {false, {true, 0}};
        framework::CharacterPlan plan;
        plan.package = package;
        bool callback_fault = true, disable_in_callback = false;
        std::uint64_t selected_id = 0;
        framework::SceneCharacters* active_registry = nullptr;
        framework::SceneCharacters registry(session_services, [&](auto& session, const auto&, const auto&) {
            session.Create(create);
            if (callback_fault)
                throw std::runtime_error("failure after actual creation");
            if (disable_in_callback)
                active_registry->SetEnabled(selected_id, false);
        });
        active_registry = &registry;
        const framework::SceneAddress scene_address{"Map/A", 1};
        selected_id = registry.Add("Map/A", plan);
        registry.Service(scene_frame, scene_address, appearance, {});
        Check(!registry.Status(selected_id).active && registry.Status(selected_id).retiring == 1 &&
              !registry.Status(selected_id).error.empty());
        Check(registry.Status(selected_id).creation_error == "failure after actual creation");
        registry.Service(scene_frame, scene_address, appearance, {});
        Check(registry.Status(selected_id).creations == 1 && registry.Status(selected_id).retiring == 0 &&
              roots.empty());
        callback_fault = false;
        disable_in_callback = true;
        registry.Rebuild(selected_id);
        Check(registry.Status(selected_id).creation_error.empty());
        registry.Service(scene_frame, scene_address, appearance, {});
        Check(!registry.Status(selected_id).enabled && !registry.Status(selected_id).active &&
              registry.Status(selected_id).retiring == 1);
        registry.Service(scene_frame, scene_address, appearance, {});
        Check(roots.empty());
        registry.Shutdown();
        Check(registry.Closed());
    });
    std::cout << "Scene character scheduling passed: source/session composition, independent scene intent, return "
                 "replay, manual removal, explicit rebuild, partial creation ownership and callback cancellation; "
                 "supplied scene identities, no game bootstrap.\n";
    TestSection([&] {
        reset_baseline();
        auto current = frame;
        current.manager = {create.manager, {true, 0}};
        current.battle = {false, {true, 0}};
        current.adventure_phase = {&map_phase, {true, 0}};
        api::PresentationBindings bindings;
        bindings.factory = M(Create);
        bindings.references = references;
        bindings.entity = entity_methods;
        bindings.model = model_methods;
        bindings.profile_inputs = input_methods;
        bindings.profile = profile_methods;
        bindings.profile_events = fields;
        bindings.events = event_methods;
        bindings.animation = action_methods;
        bindings.scene.string_equals = M(Equals);
        bindings.arrays = {M(Array), {}, {}, M(Set), {}};
        bindings.world_map.group_manager = M(Group);
        bindings.world_map.map = M(Map);
        for (auto& part : bindings.world_map.identity)
            part = M(Key);
        bindings.placement.player = M(+[](void*, void*) -> void* { return entity_ptr; });
        bindings.placement.owner_world = M(EntityWorld);
        bindings.placement.game_object = M(+[](void*, void*) -> void* { return &art; });
        bindings.placement.transform = M(+[](void*, void*) -> void* { return &art; });
        bindings.placement.position = M(+[](void*, api::Position3* out, void*) { *out = {10, 20, 30}; });
        bindings.placement.rotation = M(+[](void*, api::Rotation4* out, void*) { *out = {0, 1, 0, 0}; });
        framework::FrameworkHost host;
        int catalog_reads = 0, motion_reads = 0;
        framework::CatalogReaders catalog;
        catalog.characters = [&] {
            ++catalog_reads;
            api::CharacterCatalog out;
            out.npcs.rows = {{77, "Npc/config", "Npc/model"}};
            out.npcs.enumerated = true;
            return out;
        };
        catalog.document = [](auto kind, const std::string& path) {
            api::ActionConfigInput out;
            out.kind = kind;
            out.path = path;
            out.loaded = true;
            out.states.push_back({path, "state"});
            return out;
        };
        catalog.npc_motions = [&](void* component) {
            Check(component == &style);
            ++motion_reads;
            api::NpcMotionSources out;
            api::NpcMotionRow row;
            row.id = 18;
            out.motions.push_back(row);
            return out;
        };
        host.Prepare(
            objects, bindings, {true, true, false},
            [&](auto& session, const auto&, const auto& placement) {
                Check(placement.placement.position.x == 12 && placement.placement.position.y == 20 &&
                      placement.placement.position.z == 30 && placement.placement.rotation.y == 1);
                session.Create(create);
            },
            catalog);
        framework::CharacterPlan plan;
        plan.package = package;
        const auto plays = npc_plays;
        std::thread ui([&] { host.Application().Post(framework::PrepareSource{framework::BrowseNpc{77}}); });
        ui.join();
        Check(catalog_reads == 0 && !host.Application().Snapshot().catalog);
        host.Frame(current, false);
        auto selection_replies = host.Application().TakeReplies();
        const auto selection = selection_replies[0].source_request;
        Check(selection_replies[0].accepted && selection && catalog_reads == 1 &&
              host.Application().Snapshot().characters.empty());
        host.Frame(current, false);
        Check(host.Application().Snapshot().catalog->Source(selection)->result.finished);
        // The menu's selected table row now flows through real preparation,
        // plan assembly, command queue, registry and session, not a preset plan.
        const auto before_create_cursor = host.Application().ReadUpdates().changes.cursor;
        std::thread create_ui([&] {
            host.Application().Post(
                framework::AddPreparedCharacter{selection,
                                                "",
                                                framework::PlacedAction{Domain::Exploration, framework::NpcMotion{18}},
                                                {},
                                                {},
                                                {},
                                                true});
        });
        create_ui.join();
        Check(npc_plays == plays && host.Application().Snapshot().characters.empty()); // UI is data only
        Check(!host.Frame(current, false));
        auto replies = host.Application().TakeReplies();
        Check(replies.size() == 1 && replies[0].accepted && replies[0].character == 1);
        const auto create_operation = replies[0].operation;
        const auto created_updates = host.Application().ReadUpdates(before_create_cursor);
        Check(!created_updates.changes.reset && !created_updates.changes.events.empty());
        Check(created_updates.changes.events.back().characters == std::vector<std::uint64_t>{1});
        Check(std::find(created_updates.changes.events.back().operations.begin(),
                        created_updates.changes.events.back().operations.end(),
                        create_operation) != created_updates.changes.events.back().operations.end());
        if (!created_updates.snapshot.characters[0].active) {
            const auto& failed = created_updates.snapshot.characters[0];
            std::cerr << "host fixture creation_error=" << failed.creation_error << " error=" << failed.error << '\n';
            if (failed.session)
                for (const auto& error : failed.session->errors)
                    std::cerr << "host fixture session_error=" << error << '\n';
        }
        Check(created_updates.snapshot.characters[0].active);
        Check(create_operation == replies[0].serial &&
              host.Application().Snapshot().operations.entries.at(create_operation).phase ==
                  framework::OperationPhase::Returned);
        Check(host.Application().Snapshot().scene->key == "hsr-map-v1:2:2:2:2:2:2" &&
              host.Application().Snapshot().characters[0].active && npc_plays == plays + 1);
        host.Application().Post(framework::ReadCharacterNpcActions{1});
        host.Frame(current, false);
        Check(host.Application().TakeReplies()[0].accepted && motion_reads == 1 &&
              host.Application().Snapshot().npc_actions.at(1)->source.motions[0].id == 18);
        host.Application().Post(framework::PrepareSource{framework::BrowseNpc{77}});
        host.Application().Post(framework::ReleaseSource{selection});
        host.Application().Post(framework::AddPreparedCharacter{selection, "", {}, {}, {}, {}});
        host.Frame(current, false);
        auto stale_replies = host.Application().TakeReplies();
        Check(stale_replies[0].accepted && stale_replies[1].accepted && !stale_replies[2].accepted &&
              host.Application().Snapshot().characters.size() == 1);
        const auto current_selection = stale_replies[0].source_request;
        host.Application().Post(framework::SelectCharacterAction{999, {Domain::Exploration, framework::NpcMotion{18}}});
        host.Application().Post(
            framework::SelectCharacterAction{1, {Domain::Exploration, framework::AnimatorState{"SomeState"}}});
        const auto anim = animator_plays;
        host.Frame(current, false);
        replies = host.Application().TakeReplies();
        Check(replies.size() == 2 && !replies[0].accepted && replies[1].accepted && animator_plays == anim + 1);
        Check(replies[1].action_intent &&
              host.Application().Snapshot().characters[0].action->id == replies[1].action_intent &&
              host.Application().Snapshot().characters[0].action->phase == framework::ActionIntentPhase::Returned);
        Check(host.Application().Snapshot().operations.entries.at(replies[0].operation).phase ==
              framework::OperationPhase::Failed);
        Check(host.Application().Snapshot().operations.entries.at(replies[1].operation).phase ==
              framework::OperationPhase::Returned);
        const auto first_action = host.Application().Post(
            framework::SelectCharacterAction{1, {Domain::Exploration, framework::AnimatorState{"Old"}}});
        const auto last_action = host.Application().Post(
            framework::SelectCharacterAction{1, {Domain::Exploration, framework::AnimatorState{"SomeState"}}});
        host.Frame(current, false);
        host.Application().TakeReplies();
        Check(host.Application().Snapshot().operations.entries.at(first_action).phase ==
              framework::OperationPhase::Superseded);
        Check(host.Application().Snapshot().operations.entries.at(last_action).phase ==
              framework::OperationPhase::Returned);
        // Non-menu clients receive only their own value replies. A callback
        // can read snapshots/queue later work without holding exchange_.
        std::vector<framework::CommandReply> controlled;
        const auto control_serial =
            host.Application().Post(framework::BrowseCharacterActions{1}, [&](const auto& reply) {
                controlled.push_back(reply);
                Check(host.Application().Snapshot().prepared);
                host.Application().Post(framework::BrowseCharacterActions{999},
                                        [&](const auto& result) { controlled.push_back(result); });
            });
        host.Frame(current, false);
        Check(controlled.size() == 1 && controlled[0].serial == control_serial && controlled[0].accepted &&
              host.Application().TakeReplies().empty());
        host.Frame(current, false);
        Check(controlled.size() == 2 && !controlled.back().accepted && host.Application().TakeReplies().empty());
        host.Application().Post(framework::BrowseCharacterActions{1},
                                [](const auto&) { throw std::runtime_error("test reply fault"); });
        host.Application().Post(framework::BrowseCharacterActions{1});
        host.Frame(current, false);
        Check(host.Application().TakeReplies().size() == 1); // reply receiver failure doesn't stop the other command
        // Map input comes through WorldMap/SceneIdentity, not injected addresses.
        map_floor = 3;
        host.Frame(current, false);
        Check(!host.Application().Snapshot().characters[0].active);
        Check(host.Application().Snapshot().operations.entries.at(create_operation).phase ==
              framework::OperationPhase::Returned);
        map_floor = 2;
        host.Frame(current, false);
        Check(host.Application().Snapshot().characters[0].active &&
              host.Application().Snapshot().characters[0].creations == 2);
        host.Application().Post(framework::EnableCharacter{1, false});
        host.Frame(current, false);
        Check(!host.Application().Snapshot().characters[0].active);
        host.Application().Post(framework::RebuildCharacter{1});
        host.Frame(current, false);
        Check(host.Application().Snapshot().characters[0].creations == 3);
        Check(host.Application().TakeReplies().size() == 2);
        host.Application().Post(framework::ReplacePreparedCharacter{1, selection, {}, {}});
        host.Frame(current, false);
        Check(!host.Application().TakeReplies().back().accepted &&
              host.Application().Snapshot().characters[0].creations == 3);
        const auto previous_npc = npc_plays, previous_animator = animator_plays;
        host.Application().Post(framework::ReplacePreparedCharacter{1, current_selection, {}, {}});
        host.Frame(current, false);
        Check(host.Application().TakeReplies().back().accepted &&
              host.Application().Snapshot().characters.size() == 1 &&
              host.Application().Snapshot().characters[0].creations == 4);
        Check(host.Application().Snapshot().npc_actions.count(1) == 0 &&
              host.Application().Snapshot().catalog->Actions(1));
        Check(npc_plays == previous_npc && animator_plays == previous_animator);
        // Shared service addresses saved actors without changing the source
        // window or the observed model. They are on another map, so accepting
        // an action must not pretend it has already played.
        auto remote_plan = plan;
        remote_plan.action_sources = {{Domain::Exploration, {api::ActionConfigKind::Adventure, "RemoteAction"}}};
        host.Application().Post(framework::AddCharacter{remote_plan, "another-map", {}, {}});
        host.Frame(current, false);
        const auto remote_id = host.Application().TakeReplies().back().character;
        const auto old_gaze =
            host.Application().Post(framework::LookCharacter{remote_id, {framework::LookMode::Point, 0, {1, 2, 3}}});
        const auto new_gaze =
            host.Application().Post(framework::LookCharacter{remote_id, {framework::LookMode::Player}});
        const auto unavailable_place = host.Application().Post(framework::PlaceCharacter{remote_id, {}, false});
        host.Frame(current, false);
        auto controls = host.Application().TakeReplies();
        Check(controls.size() == 3 && controls[0].accepted && controls[1].accepted && !controls[2].accepted);
        Check(controls[0].operation == old_gaze && controls[1].operation == new_gaze &&
              controls[2].operation == unavailable_place);
        Check(host.Application().Snapshot().operations.entries.at(old_gaze).phase ==
              framework::OperationPhase::Superseded);
        Check(host.Application().Snapshot().operations.entries.at(new_gaze).phase ==
              framework::OperationPhase::Waiting);
        Check(host.Application().Snapshot().operations.entries.at(unavailable_place).phase ==
              framework::OperationPhase::Failed);
        host.Application().Post(framework::BrowseCharacterActions{remote_id});
        host.Frame(current, false);
        host.Application().TakeReplies();
        const auto remote_view = host.Application().Snapshot().catalog->Actions(remote_id);
        Check(remote_view && remote_view->actions.finished);
        const framework::CatalogActionSelection remote_action{
            remote_view->revision, framework::ConfiguredActionKey{Domain::Exploration, "RemoteAction"}};
        host.Application().Post(framework::SelectCatalogAction{remote_id, remote_action});
        host.Application().Post(framework::SelectCatalogAction{1, remote_action});
        host.Frame(current, false);
        const auto resolved = host.Application().TakeReplies();
        Check(resolved.size() == 2 && resolved[0].accepted && !resolved[1].accepted);
        Check(resolved[0].action_intent &&
              host.Application().Snapshot().characters.back().action->id == resolved[0].action_intent &&
              host.Application().Snapshot().characters.back().action->phase == framework::ActionIntentPhase::Waiting);
        Check(host.Application().Snapshot().operations.entries.at(resolved[0].operation).phase ==
              framework::OperationPhase::Waiting);
        Check(host.Application().Snapshot().catalog->Source(current_selection)->phase ==
                  framework::SourceRequestPhase::Ready &&
              !host.Application().Snapshot().characters.back().active && animator_plays == previous_animator);
        bool control_cancelled = false;
        host.Application().Post(framework::RebuildCharacter{1}, [&](const auto& reply) {
            control_cancelled = !reply.accepted && !reply.error.empty();
        });
        host.Application().Post(framework::RebuildCharacter{1});
        Check(host.Frame(current, true) && control_cancelled);
        replies = host.Application().TakeReplies();
        Check(replies.size() == 1 && !replies.back().accepted && host.Application().Snapshot().closed && roots.empty());
        Check(host.Application().Snapshot().operations.entries.at(resolved[0].operation).phase ==
              framework::OperationPhase::Cancelled);
        Check(host.Application().Snapshot().operations.entries.at(replies.back().operation).phase ==
              framework::OperationPhase::Cancelled);
        Check(host.Application().Snapshot().operations.entries.at(new_gaze).phase ==
              framework::OperationPhase::Cancelled);
        bool rejected = false;
        try {
            host.Application().Post(framework::RebuildCharacter{1});
        } catch (const std::logic_error&) {
            rejected = true;
        }
        Check(rejected);
        // Missing entity notifications are a concrete ownership dependency,
        // but no optional event/texture capability was needed by the test above.
        framework::FrameworkHost missing;
        missing.Prepare(objects, bindings, {}, [&](auto&, const auto&, const auto&) { Check(false); });
        missing.Application().Post(framework::AddCharacter{plan, "saved", {}, {}});
        missing.Frame(current, false);
        Check(!missing.Application().TakeReplies()[0].accepted && missing.Application().Snapshot().characters.empty());
        Check(missing.Frame(current, true) && roots.empty());
        std::cout
            << "Framework host integration passed: UI value queue, official map getters, session "
               "creation/action/return/rebuild, independent command failure and asynchronous drain; local only.\n";
    });
    TestGazeHandoff(frame, create, session_services, references, entity_methods, package, appearance, types);
    TestSection([&] {
        reset_baseline();
        auto scene_frame = frame;
        scene_frame.manager = {create.manager, {true, 0}};
        scene_frame.battle = {false, {true, 0}};
        framework::SceneCharacters registry(session_services,
                                            [&](auto& session, const auto&, const auto&) { session.Create(create); });
        framework::CharacterPlan invalid;
        invalid.package = package;
        invalid.package.source = framework::SourceKind::External;
        const auto bad = registry.Add("A", invalid); // missing payload: construction throws before any game call
        framework::CharacterPlan valid;
        valid.package = package;
        const auto good =
            registry.Add("A", valid, framework::PlacedAction{Domain::Exploration, framework::NpcMotion{18}});
        const auto plays = npc_plays;
        registry.Service(scene_frame, framework::SceneAddress{"A", 1}, appearance, {});
        Check(!registry.Status(bad).error.empty() && registry.Status(good).active && npc_plays == plays + 1);
        registry.Shutdown();
        registry.Service(scene_frame, framework::SceneAddress{"A", 1}, appearance, {});
        Check(registry.Closed() && roots.empty());
    });
    TestSection([&] {
        reset_baseline();
        auto current = frame;
        current.sequence = 1000;
        current.manager = {create.manager, {true, 0}};
        current.battle = {false, {true, 0}};
        framework::CharacterPlan plan;
        plan.package = package;
        plan.source_intent = framework::BrowseNpc{77};
        plan.alternate = framework::BattlePresentation{{"battle.prefab", {}, {}}, {true, "OtherState", {}}};
        framework::SceneCharacters registry(session_services,
                                            [&](auto& session, const auto&, const auto&) { session.Create(create); });
        const framework::SceneAddress layout_scene{"layout-map", 1};
        const auto id = registry.Add(
            layout_scene.key, plan, framework::PlacedAction{Domain::Battle, framework::FreeStyleState{"SomeState"}});
        observed_hash = 41;
        observed_time = 0;
        registry.Service(current, layout_scene, appearance, types);
        ++current.sequence;
        registry.Service(current, layout_scene, appearance, types);
        Check(registry.Status(id).handoff->phase == Handoff::Ready);
        const auto intent = registry.Status(id).action->id;
        observed_hash = 37;
        observed_time = 1.1f;
        ++current.sequence;
        registry.Service(current, layout_scene, appearance, types);
        Check(std::holds_alternative<framework::BattleIdle>(registry.DesiredAction(id)->request));
        Check(registry.Status(id).action->id == intent); // historical command identity remains unchanged
        const auto layout = framework::CaptureSceneLayout(registry, layout_scene.key);
        Check(std::holds_alternative<framework::BattleIdle>(*layout.characters.front().action));
        registry.Select(id, {Domain::Battle, framework::FreeStyleState{"SomeState"}});
        observed_time = 0;
        ++current.sequence;
        registry.Service(current, layout_scene, appearance, types);
        Check(std::get<framework::FreeStyleState>(registry.DesiredAction(id)->request).name == "SomeState");
        registry.Shutdown();
        registry.Service(current, layout_scene, appearance, types);
        Check(registry.Closed() && roots.empty());
    });
    TestSection([&] {
        reset_baseline();
        auto current = frame;
        current.sequence = 1500;
        current.manager = {create.manager, {true, 0}};
        current.battle = {false, {true, 0}};
        framework::CharacterPlan plan;
        plan.package = package;
        plan.source_intent = framework::BrowseNpc{77};
        plan.alternate = framework::BattlePresentation{{"battle.prefab", {}, {}}, {true, "OtherState", {}}};
        auto input = std::make_shared<framework::VisualProgramInput>();
        input->program = std::make_shared<const framework::VisualProgram>(framework::test::ParseLegacyVisualProgram(R"({
          "format":"hksr.visual-program","id":"settled-result","source":"battle","scope":"scene","effects":{},
          "actions":{"attack":{"state":"SomeState","restart":true,"complete_at":0.5,
            "completion_state":"OtherState","completion_at":0.5,
            "enter":[{"op":"timeline_play","source":"scene","key":"scene","path":"scene.playable"}],
            "after_completion":[{"op":"timeline_stop","source":"tail","key":"scene"}]}}})"));
        plan.visual_program = input;
        auto configured = session_services;
        std::shared_ptr<framework::VisualProgramPlayer> player;
        bool scene_alive = false;
        configured.visual_program = [&](api::EntityOrigin, const auto& value, const auto&, const auto&) {
            framework::VisualProgramServices callbacks;
            callbacks.effects = std::make_shared<api::PresentationEffects>(
                api::EffectRequestServices{{}, api::EffectAccess({}), {}, {}, {}});
            callbacks.create = [](auto&, const auto&, const auto&) -> framework::VisualEffectId {
                throw std::logic_error("unexpected effect in settled-result fixture");
            };
            callbacks.timeline = [&](auto, const auto& operation) {
                scene_alive = operation.play;
                return framework::VisualStepResult{framework::VisualStepState::Done, {}};
            };
            callbacks.cancel_timelines = [&](auto) { scene_alive = false; };
            callbacks.timelines_drained = [&] { return !scene_alive; };
            player = std::make_shared<framework::VisualProgramPlayer>(value.program, std::move(callbacks));
            return player;
        };
        framework::SceneCharacters registry(configured,
                                            [&](auto& session, const auto&, const auto&) { session.Create(create); });
        const framework::SceneAddress address{"settled-result-map", 1};
        const auto id = registry.Add(address.key, plan,
                                     framework::PlacedAction{Domain::Battle,
                                                            framework::ProgramAction{"settled-result", "attack"}});
        auto tick = [&] {
            ++current.sequence;
            registry.Service(current, address, appearance, types);
        };
        observed_hash = 41;
        observed_time = 0;
        tick();
        tick();
        Check(registry.Status(id).action->phase == framework::ActionIntentPhase::Returned && scene_alive);
        const auto intent = registry.Status(id).action->id;
        const auto selection = registry.Active(id, current)->ActionStatus()->request;
        observed_hash = 37;
        tick();
        observed_time = .6f;
        tick();
        const auto return_plays = battle_plays;
        Check(player->Status().completing && !player->Status().completed);
        Check(registry.Active(id, current)->ActionStatus()->phase == framework::ActionIntentPhase::Returned &&
              !registry.Active(id, current)->ActionStatus()->settled_successor);
        Check(std::holds_alternative<framework::ProgramAction>(registry.DesiredAction(id)->request));
        observed_hash = 41;
        observed_time = .1f;
        tick();
        Check(scene_alive && !registry.Active(id, current)->ActionStatus()->settled_successor);
        observed_time = .6f;
        tick();
        const auto settled = registry.Active(id, current)->ActionStatus();
        Check(settled && settled->request == selection && settled->settled_successor &&
              std::holds_alternative<framework::BattleIdle>(settled->settled_successor->request));
        Check(!scene_alive && registry.Status(id).action->id == intent && battle_plays == return_plays);
        Check(std::holds_alternative<framework::BattleIdle>(registry.DesiredAction(id)->request));
        Check(std::holds_alternative<framework::BattleIdle>(
            *framework::CaptureSceneLayout(registry, address.key).characters.front().action));
        tick();
        Check(battle_plays == return_plays); // adopting the checkpoint is not another playback request
        registry.Select(id, {Domain::Battle, framework::FreeStyleState{"SomeState"}});
        const auto replacement = registry.Active(id, current)->ActionStatus();
        Check(replacement && replacement->request != selection && !replacement->settled_successor &&
              replacement->phase == framework::ActionIntentPhase::Waiting);
        registry.Rebuild(id); // the prior creation's settled result must not replace this new intent
        reset_baseline();
        observed_hash = 41;
        observed_time = 0;
        tick();
        tick();
        Check(registry.Status(id).creations == 2 &&
              std::get<framework::FreeStyleState>(registry.DesiredAction(id)->request).name == "SomeState");
        registry.Shutdown();
        tick();
        Check(registry.Closed() && roots.empty());
    });
    TestSection([&] {
        reset_baseline();
        auto current = frame;
        current.sequence = 2000;
        current.manager = {create.manager, {true, 0}};
        current.battle = {false, {true, 0}};
        static Value observed_name;
        observed_name.text = "OtherState";
        auto observed_methods = action_methods;
        observed_methods.resolve_hash = M(+[](void* a, int hash, void*) -> void* {
            Check(a == &animator && hash == 41);
            return &observed_name;
        });
        const api::AnimationControl observed_animation(observed_methods);
        api::ComponentConfigMethods text_methods;
        text_methods.string_length = M(+[](Value* value, void*) { return static_cast<int>(value->text.size()); });
        text_methods.string_character =
            M(+[](Value* value, int index, void*) -> std::uint16_t { return value->text.at(index); });
        const api::ComponentConfig observed_text(text_methods);
        auto observer_services = session_services;
        observer_services.handoff.animations = [=](api::EntityToken owner) {
            return std::make_unique<api::ActorAnimationSession>(owner, references, entities,
                                                                api::ModelAccess(model_methods), observed_animation,
                                                                api::ProfileInputs(input_methods));
        };
        observer_services.handoff.text = &observed_text;
        framework::CharacterPlan plan;
        plan.package = package;
        plan.source_intent = framework::BrowseNpc{77};
        framework::SceneCharacters registry(observer_services,
                                            [&](auto& session, const auto&, const auto&) { session.Create(create); });
        const framework::SceneAddress address{"observed-exploration-map", 1};
        observed_hash = 37;
        observed_time = 0;
        const auto id = registry.Add(
            address.key, plan, framework::PlacedAction{Domain::Exploration, framework::FreeStyleState{"SomeState"}});
        registry.Service(current, address, appearance, types);
        ++current.sequence;
        registry.Service(current, address, appearance, types);
        const auto intent = registry.Status(id).action->id;
        const auto plays = battle_plays;
        observed_time = 1.1f;
        ++current.sequence;
        registry.Service(current, address, appearance, types);
        Check(std::get<framework::FreeStyleState>(registry.DesiredAction(id)->request).name == "SomeState");
        observed_hash = 41;
        ++current.sequence;
        registry.Service(current, address, appearance, types);
        const auto& desired = std::get<framework::FreeStyleState>(registry.DesiredAction(id)->request);
        Check(desired.name == "OtherState" && desired.controller_observed && battle_plays == plays);
        Check(registry.Status(id).action->id == intent);
        const auto saved = framework::DecodeSceneLayout(
            framework::EncodeSceneLayout(framework::CaptureSceneLayout(registry, address.key)));
        const auto restored =
            framework::RestoreLayoutAction(saved.characters.front().action, framework::ActionCatalogStatus{});
        Check(restored && restored->domain == Domain::Exploration &&
              std::get<framework::FreeStyleState>(restored->request).name == "OtherState" &&
              std::get<framework::FreeStyleState>(restored->request).controller_observed);
        registry.Shutdown();
        registry.Service(current, address, appearance, types);
        Check(registry.Closed() && roots.empty());
    });
}
