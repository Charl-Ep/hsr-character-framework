#include <hksr/framework/presentation/VisualTargets.hpp>
#include <hksr/framework/tasks/TaskCatalog.hpp>
#include <cmath>
#include <stdexcept>

namespace {
using namespace hksr::framework;
void Check(bool value){if(!value)throw std::runtime_error("visual target test failed");}
bool Near(float x,float y){return std::abs(x-y)<.0001f;}
template<class F>void Reject(F call){bool failed=false;try{call();}catch(const std::exception&){failed=true;}Check(failed);}
}
void TestVisualTargets() {
    VisualTargetDefaults defaults{{"Target",hksr::api::Position3{0,0,3}},{"SkillPoint",std::string("Target")}};
    VisualProgramParameters inputs;
    auto bindings=ResolveVisualTargetBindings(defaults,inputs,{"Caster"});
    Check(std::holds_alternative<VisualEntityTarget>(bindings.at("Caster")));
    int reads=0;
    const auto captured=CaptureVisualPositionTargets(bindings,[&]{++reads;return VisualSpatialAnchor{{10,-15,20},{0,.70710678f,0,.70710678f}};});
    Check(reads==1 && captured.size()==2);
    Check(Near(captured.at("Target").position.x,13) && Near(captured.at("Target").position.y,-15) && Near(captured.at("Target").position.z,20));
    inputs.position_targets=captured;
    auto restored=CaptureVisualPositionTargets(ResolveVisualTargetBindings(defaults,inputs,{"Caster"}),[&]()->VisualSpatialAnchor {throw std::runtime_error("must not recapture after movement");});
    Check(Near(restored.at("SkillPoint").position.x,13));
    inputs={};inputs.targets["Target"]=42;
    bindings=ResolveVisualTargetBindings(defaults,inputs,{"Caster","Target"});
    Check(std::get<VisualEntityTarget>(bindings.at("SkillPoint")).binding=="Target");
    bindings=ResolveVisualTargetBindings(defaults,inputs,{"Caster"});
    Check(!bindings.count("Target") && !bindings.count("SkillPoint"));
    inputs.targets["Target"]=0;
    Check(std::holds_alternative<VisualPositionTarget>(ResolveVisualTargetBindings(defaults,inputs,{"Caster"}).at("SkillPoint")));
    bindings=ResolveVisualTargetBindings(defaults,inputs,{"Caster"},{"Target"});
    Check(!bindings.count("Target") && !bindings.count("SkillPoint")); // ended native-only binding
    inputs.targets["Target"]=42;inputs.targets["Unrelated"]=999;
    const auto required=VisualTargetInputs(defaults,inputs,{"SkillPoint"});
    Check(required.targets.size()==1 && required.targets.at("Target")==42);
    defaults["Target"]=std::string("SkillPoint");Reject([&]{ValidateVisualTargetDefaults(defaults);});
    const auto rotation=RotateVisualTarget({0,0,0,1},{0,90,0});
    const auto moved=TransformVisualTarget({{},rotation},{0,0,2});Check(Near(moved.position.x,2) && Near(moved.position.z,0));
    TaskContext context;TaskDefinition task;task.key={"test","point"};task.entry="entry";task.requirements={TaskNeedsTarget{"Target",true}};
    TaskQuery query;query.position_targets={"Target"};
    Check(context.Query(task,query).availability==TaskAvailability::Available);
    task.requirements={TaskNeedsTarget{"Target",false}};
    Check(context.Query(task,query).availability==TaskAvailability::Unavailable);
}
