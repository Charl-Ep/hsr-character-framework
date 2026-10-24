#include <hksr/framework/tasks/TaskCatalog.hpp>
#include <hksr/framework/tasks/BattleControl.hpp>
#include <stdexcept>
#include <set>

namespace hksr::framework {
const TaskPlan& TaskCatalog::Resolve(const TaskKey& key) const {
    if(key.catalog!=id)throw std::invalid_argument("task belongs to another catalog: "+key.catalog);
    const auto found=tasks.find(key.name);
    if(found==tasks.end())throw std::invalid_argument("unknown task: "+key.name);
    return found->second;
}
void ValidateTaskCatalog(const TaskCatalog& catalog) {
    ValidateSkillSlots(catalog.control_slots);
    if(catalog.id.empty() || !catalog.visual)throw std::invalid_argument("task catalog requires identity and visual resources");
    for(const auto& [key,sequence]:catalog.sequences) {
        if(key.empty() || !sequence)throw std::invalid_argument("empty task sequence");
        ValidateTaskSequence(*sequence);
    }
    for(const auto& [key,task]:catalog.tasks) {
        ValidateTaskDefinition(task.definition);
        if(task.definition.key.catalog!=catalog.id || key!=task.definition.key.name)
            throw std::invalid_argument("task catalog identity mismatch: "+key);
        if(!task.definition.entry.empty() && !catalog.sequences.count(task.definition.entry))
            throw std::invalid_argument("unknown task sequence: "+task.definition.entry);
        if(!task.definition.entry.empty() && !task.definition.control)
            for(const auto& node:catalog.sequences.at(task.definition.entry)->nodes)
                if(const auto* submit=std::get_if<TaskSubmit>(&node.operation))
                    if(std::holds_alternative<TaskControlCommand>(submit->operation))
                        throw std::invalid_argument("sequence control changes require an accepted control relation: "+key);
        if(task.definition.control && !task.definition.control->presentation &&
            (!task.definition.entry.empty() || task.next))throw std::invalid_argument("control-only operation cannot own a sequence or successor");
        if(task.next) {
            const auto& successor=catalog.Resolve(task.next->task).definition;
            if(successor.control && successor.control->trigger!=ControlTrigger::Automatic)
                throw std::invalid_argument("task successor requires an automatic control relation: "+task.next->task.name);
            // Query also reports dynamic prerequisites. Only static argument
            // errors are rejected here; missing values may come from context.
            TaskContext context;const auto query=context.Query(successor,{task.next->inputs});
            for(const auto& reason:query.reasons)if(reason.kind==TaskReasonKind::InputType)
                throw std::invalid_argument("invalid successor input: "+reason.source);
        }
    }
    for(const auto& [name,context]:catalog.contexts) {
        if(!catalog.visual->contexts.count(name))throw std::invalid_argument("unknown visual context contribution: "+name);
        for(const auto& [slot,key]:context.slots){(void)slot;catalog.Resolve(key);}
        for(const auto& disable:context.disables)
            if(const auto* key=std::get_if<TaskKey>(&disable.selector))catalog.Resolve(*key);
        TaskContext checked;auto token=checked.Enter(name);
        for(const auto& [key,value]:context.values)checked.SetValue(token,key,value);
    }
    std::set<std::string> automatic;
    for(const auto& name:catalog.automatic) {
        const auto& definition=catalog.Resolve({catalog.id,name}).definition;
        if(!automatic.insert(name).second || !definition.control || definition.control->trigger!=ControlTrigger::Automatic)
            throw std::invalid_argument("invalid automatic control entry: "+name);
    }
}
}
