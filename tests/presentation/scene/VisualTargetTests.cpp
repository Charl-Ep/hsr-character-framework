#include <hksr/framework/presentation/scene/VisualTargets.hpp>
#include <hksr/framework/tasks/TaskCatalog.hpp>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
using namespace hksr::framework;
void Check(bool value) {
    if (!value)
        throw std::runtime_error("visual target test failed");
}
bool Near(float x, float y) {
    return std::abs(x - y) < .0001f;
}
template <class F> void Reject(F call) {
    bool failed = false;
    try {
        call();
    } catch (const std::exception&) {
        failed = true;
    }
    Check(failed);
}
}
void TestVisualTargets() {
    VisualTargetDefaults defaults{{"Target", hksr::api::Position3{0, 0, 3}}, {"SkillPoint", std::string("Target")}};
    VisualProgramParameters inputs;
    auto bindings = ResolveVisualTargetBindings(defaults, inputs, {"Caster"});
    Check(std::holds_alternative<VisualEntityTarget>(bindings.at("Caster")));
    int reads = 0;
    const auto captured = CaptureVisualPositionTargets(bindings, [&] {
        ++reads;
        return VisualSpatialAnchor{{10, -15, 20}, {0, .70710678f, 0, .70710678f}};
    });
    Check(reads == 1 && captured.size() == 2);
    Check(Near(captured.at("Target").position.x, 13) && Near(captured.at("Target").position.y, -15) &&
          Near(captured.at("Target").position.z, 20));
    inputs.position_targets = captured;
    auto restored = CaptureVisualPositionTargets(
        ResolveVisualTargetBindings(defaults, inputs, {"Caster"}),
        [&]() -> VisualSpatialAnchor { throw std::runtime_error("must not recapture after movement"); });
    Check(Near(restored.at("SkillPoint").position.x, 13));
    inputs = {};
    inputs.targets["Target"] = 42;
    bindings = ResolveVisualTargetBindings(defaults, inputs, {"Caster", "Target"});
    Check(std::get<VisualEntityTarget>(bindings.at("SkillPoint")).binding == "Target");
    bindings = ResolveVisualTargetBindings(defaults, inputs, {"Caster"});
    Check(!bindings.count("Target") && !bindings.count("SkillPoint"));
    inputs.targets["Target"] = 0;
    Check(std::holds_alternative<VisualPositionTarget>(
        ResolveVisualTargetBindings(defaults, inputs, {"Caster"}).at("SkillPoint")));
    bindings = ResolveVisualTargetBindings(defaults, inputs, {"Caster"}, {"Target"});
    Check(!bindings.count("Target") && !bindings.count("SkillPoint")); // ended native-only binding
    inputs.targets["Target"] = 42;
    inputs.targets["Unrelated"] = 999;
    const auto required = VisualTargetInputs(defaults, inputs, {"SkillPoint"});
    Check(required.targets.size() == 1 && required.targets.at("Target") == 42);
    defaults["Target"] = std::string("SkillPoint");
    Reject([&] { ValidateVisualTargetDefaults(defaults); });
    const auto rotation = RotateVisualTarget({0, 0, 0, 1}, {0, 90, 0});
    const auto moved = TransformVisualTarget({{}, rotation}, {0, 0, 2});
    Check(Near(moved.position.x, 2) && Near(moved.position.z, 0));
    TaskContext context;
    TaskDefinition task;
    task.key = {"test", "point"};
    task.entry = "entry";
    task.requirements = {TaskNeedsTarget{"Target", true}};
    TaskQuery query;
    query.position_targets = {"Target"};
    Check(context.Query(task, query).availability == TaskAvailability::Available);
    task.requirements = {TaskNeedsTarget{"Target", false}};
    Check(context.Query(task, query).availability == TaskAvailability::Unavailable);
    inputs = {};
    inputs.targets["A"] = 42;
    inputs.position_targets["B"] = {{4, -1, 3}, {}};
    inputs.target_collections["Neighbours"] = {"Param", "B"};
    const auto rebound = BindVisualParameters(inputs, {{"Param", "A"}, {"Point", "B"}, {"Owner", "Caster"}});
    Check(rebound.targets.at("Param") == 42 && rebound.position_targets.at("Point").position.x == 4);
    Check(rebound.target_aliases.at("Owner") == "Caster" && !inputs.targets.count("Param"));
    Check(rebound.target_collections.at("Neighbours") == std::vector<std::string>({"A", "B"}));
    const auto scoped = ResolveVisualTargetBindings({}, rebound, {"Caster", "A", "Param"});
    Check(std::get<VisualEntityTarget>(scoped.at("Owner")).binding == "Caster");
    Check(std::holds_alternative<VisualPositionTarget>(scoped.at("Point")));
    auto overridden = rebound;
    overridden.target_aliases["Owner"] = "B";
    const auto overlay = ResolveVisualTargetBindings({}, overridden, {"Caster", "Owner"}, {"Owner"});
    Check(std::holds_alternative<VisualPositionTarget>(overlay.at("Owner")));
    VisualProgramSnapshot snapshot{"scope", VisualContextSnapshot{"", rebound, {}}, {}};
    ValidateVisualSnapshot(snapshot);
    auto copy = snapshot;
    copy.scene->inputs.target_aliases["Owner"] = "A";
    Check(!SameVisualSnapshot(snapshot, copy));
    copy = snapshot;
    copy.scene->inputs.target_collections["Neighbours"] = {};
    Check(!SameVisualSnapshot(snapshot, copy));
    copy = snapshot;
    RemapVisualTargets(copy, {{42, 123}});
    Check(copy.scene->inputs.targets.at("Param") == 123);

    ValidateVisualParameters(rebound);
    Check(SameVisualParameters(rebound, rebound));
    auto parameter_copy = rebound;
    parameter_copy.targets["A"] = 43;
    Check(!SameVisualParameters(rebound, parameter_copy));
    parameter_copy = rebound;
    parameter_copy.choices["variant"] = "other";
    Check(!SameVisualParameters(rebound, parameter_copy));
    parameter_copy = rebound;
    parameter_copy.anchors["origin"] = {{1, 2, 3}, {0, 0, 0, 1}};
    Check(!SameVisualParameters(rebound, parameter_copy));
    parameter_copy = rebound;
    parameter_copy.position_targets.at("B").position.x += 1;
    Check(!SameVisualParameters(rebound, parameter_copy));
    parameter_copy = rebound;
    parameter_copy.target_aliases["Owner"] = "A";
    Check(!SameVisualParameters(rebound, parameter_copy));
    parameter_copy = rebound;
    parameter_copy.target_collections["Neighbours"] = {};
    Check(!SameVisualParameters(rebound, parameter_copy));
    parameter_copy = rebound;
    parameter_copy.target_aliases = {{"A", "B"}, {"B", "A"}};
    Reject([&] { ValidateVisualParameters(parameter_copy); });
    parameter_copy = rebound;
    parameter_copy.position_targets.at("B").position.y = std::numeric_limits<float>::infinity();
    Reject([&] { ValidateVisualParameters(parameter_copy); });
    parameter_copy = rebound;
    parameter_copy.choices["variant"] = "";
    Reject([&] { ValidateVisualParameters(parameter_copy); });
    parameter_copy = rebound;
    parameter_copy.targets["cleared"] = 0;
    RemapVisualParameters(parameter_copy, {{42, 123}});
    Check(parameter_copy.targets.at("A") == 123 && parameter_copy.targets.at("Param") == 123 &&
          !parameter_copy.targets.at("cleared"));
    Reject([&] { RemapVisualParameters(parameter_copy, {}); });
}
