#include <hksr/framework/tasks/TaskControlTypes.hpp>
#include <cmath>
#include <stdexcept>

namespace hksr::framework {
namespace {
VisualProgramSnapshot Inputs(const TaskSelection& value) {
    return {"task-inputs",VisualContextSnapshot{"",value.visual,{}},{}};
}
}
void ValidateTaskSelection(const TaskSelection& value) {
    auto name=[](const auto& text){return !text.empty() && text.find('\0')==std::string::npos;};
    if(!name(value.task.catalog) || !name(value.task.name))throw std::invalid_argument("invalid task identity");
    for(const auto& [key,input]:value.query.inputs) {
        if(!name(key))throw std::invalid_argument("invalid task input name");
        if(const auto* number=std::get_if<double>(&input);number && !std::isfinite(*number))
            throw std::invalid_argument("nonfinite task input");
    }
    ValidateVisualSnapshot(Inputs(value));
}
bool SameTaskSelection(const TaskSelection& a,const TaskSelection& b) {
    return a.task==b.task && a.inputs_captured==b.inputs_captured && a.query.inputs==b.query.inputs &&
        bool(a.query.slot)==bool(b.query.slot) && (!a.query.slot || a.query.slot->value==b.query.slot->value) &&
        SameVisualSnapshot(Inputs(a),Inputs(b));
}
void RemapTaskTargets(TaskSelection& value,const std::map<std::uint64_t,std::uint64_t>& keys) {
    for(auto& [name,id]:value.visual.targets)if(id)id=keys.at(id);
}
}
