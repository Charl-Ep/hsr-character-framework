#pragma once
#include <hksr/framework/application/CharacterTasks.hpp>
#include <tuple>
#include <algorithm>

namespace hksr::framework::ui {
// UI-thread value state. Replies are delivered through Menu::Draw, never a
// callback retaining the menu. Query acceptance does not start a task.
struct TaskInputDraft {
    CharacterTaskEntry entry;
    ControlCharacterTask command;
    std::uint64_t revision=1,pending=0,sent_revision=0;
    std::optional<TaskSelectionResult> result;
    std::string error;
    bool dirty=true;
    std::vector<std::tuple<std::uint64_t,std::uint64_t,bool,bool,bool,std::string>> targets;
    std::vector<std::uint64_t> contexts;
    explicit TaskInputDraft(CharacterTaskEntry value):entry(std::move(value)),command(std::get<ControlCharacterTask>(entry.command)){}
    TaskSelection& Selection(){return *command.input.selection;}
    void Changed(){++revision;dirty=true;result.reset();error.clear();}
    void Observe(const ApplicationSnapshot& snapshot,const SceneCharacterStatus& actor) {
        decltype(contexts) next_contexts;
        if(actor.tasks)for(const auto& context:actor.tasks->control.contexts)next_contexts.push_back(context.token.value);
        decltype(targets) next_targets;
        // Query again when a selected target is rebuilt/removed or leaves the map.
        for(const auto& [alias,id]:Selection().visual.targets) {
            (void)alias;
            const auto row=std::find_if(snapshot.characters.begin(),snapshot.characters.end(),[&](const auto& item){return item.id==id;});
            if(row!=snapshot.characters.end())next_targets.emplace_back(id,row->creations,row->active,row->enabled,row->deleting,row->scene);
        }
        next_targets.emplace_back(actor.id,actor.creations,actor.active,actor.enabled,actor.deleting,actor.scene);
        if(contexts!=next_contexts || targets!=next_targets){contexts=std::move(next_contexts);targets=std::move(next_targets);Changed();}
    }
    void Query(ApplicationService& host) {
        if(!dirty || pending)return;
        auto query=command;query.input.operation=TaskControlOperation::Query;
        pending=host.Post(std::move(query));sent_revision=revision;dirty=false;
    }
    bool Reply(const CommandReply& reply) {
        if(!pending || pending!=reply.serial)return false;
        pending=0;
        if(sent_revision!=revision)return true;
        if(!reply.accepted)error=reply.error;
        else if(reply.task && reply.task->selection){result=reply.task->selection;entry.query=result->query;}
        else error="task query returned no selection";
        return true;
    }
    bool Ready() const {return !dirty && !pending && result && result->query.availability==TaskAvailability::Available;}
};
}
