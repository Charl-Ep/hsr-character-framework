#include <hksr/framework/application/CharacterTasks.hpp>
#include <algorithm>
#include <stdexcept>

namespace hksr::framework {
std::vector<CharacterTaskEntry> CharacterTasks(const ApplicationSnapshot& snapshot,std::uint64_t id) {
    const SceneCharacterStatus* actor=nullptr;
    for(const auto& row:snapshot.characters)if(row.id==id){actor=&row;break;}
    if(!actor)throw std::out_of_range("character not found");
    std::vector<CharacterTaskEntry> result;
    const auto* catalog=snapshot.catalog?snapshot.catalog->Actions(id):nullptr;
    auto add=[&](CharacterTaskKind kind,ActionDomain domain,std::string label,CharacterCommand command)->CharacterTaskEntry& {
        CharacterTaskEntry entry;entry.kind=kind;entry.domain=domain;entry.label=std::move(label);entry.command=std::move(command);
        if(!actor->active || !actor->enabled || actor->deleting) {
            entry.query.availability=TaskAvailability::Unavailable;
            entry.query.reasons.push_back({TaskReasonKind::Participant,"body","character is not active"});
        }
        result.push_back(std::move(entry));return result.back();
    };
    auto reading=[&](CharacterTaskEntry& entry) {
        if(catalog && !catalog->actions.Ready(entry.domain)) {
            entry.query.availability=TaskAvailability::Unavailable;
            entry.query.reasons.push_back({TaskReasonKind::Unresolved,"catalog","action data is still being read"});
        }
    };
    if(const auto found=snapshot.npc_actions.find(id);found!=snapshot.npc_actions.end() && found->second->creation==actor->creations)
        for(const auto& motion:found->second->source.motions)
            add(CharacterTaskKind::Npc,ActionDomain::Exploration,motion.name.available?motion.name.value:motion.indexed_name,
                SelectCharacterAction{id,{ActionDomain::Exploration,NpcMotion{motion.id}}});
    if(!actor->battle_idle_state.empty())
        add(CharacterTaskKind::Idle,ActionDomain::Alternate,actor->battle_idle_state,IdleCharacter{id});
    if(catalog) {
        for(const auto& set:catalog->actions.ready_actions)for(std::size_t i=0;i<set->poses.size();++i) {
            const auto& pose=set->poses[i];
            auto& entry=add(CharacterTaskKind::Ready,ActionDomain::Alternate,pose.label,
                SelectCatalogAction{id,{catalog->revision,ReadyActionKey{set->source,i}}});
            entry.detail=pose.state;entry.sources.push_back({set->source,pose.skill_name});reading(entry);
        }
        for(const auto& proposal:catalog->actions.proposals) {
            auto& entry=add(CharacterTaskKind::Configured,proposal.domain,proposal.state,
                SelectCatalogAction{id,{catalog->revision,ConfiguredActionKey{proposal.domain,proposal.state}}});
            for(const auto& reference:proposal.references)entry.sources.push_back({reference.source.path,reference.field});
            reading(entry);
        }
        for(const auto& visual:catalog->actions.visual_states)if(visual.value.flag.available)for(bool recover:{false,true}) {
            const auto& value=visual.value;
            auto label=value.occur.resolved_state.value.empty()?value.loop.resolved_state.value:value.occur.resolved_state.value;
            if(label.empty())label="flag="+std::to_string(value.flag.value);
            if(visual.expression.available && !visual.expression.value.empty())label+=" · "+visual.expression.value;
            if(value.priority.available && !value.priority.value.empty())label+=" · "+value.priority.value;
            auto& entry=add(CharacterTaskKind::Reaction,ActionDomain::Alternate,std::move(label),
                SelectCatalogAction{id,{catalog->revision,VisualActionKey{visual.character_config,value.flag.value,recover}}});
            entry.recovery=recover;
            entry.detail=value.occur.resolved_state.value+" → "+value.loop.resolved_state.value+" → "+value.recover.resolved_state.value;
            entry.sources.push_back({visual.source,"flag="+std::to_string(value.flag.value)});
            if(!visual.expression_source.empty())entry.sources.push_back({visual.expression_source,"expression"});
            const auto query=QueryVisualAction(visual,recover);
            if(!query.available)entry.query.availability=TaskAvailability::Unavailable;
            for(const auto& issue:query.issues)entry.query.reasons.push_back({TaskReasonKind::Unimplemented,visual.source,issue});
        }
    }
    for(const auto& [key,state]:actor->program_actions) {
        auto& entry=add(CharacterTaskKind::Program,ActionDomain::Alternate,key,
            SelectCharacterAction{id,{ActionDomain::Alternate,ProgramAction{actor->visual_program,key}}});
        entry.detail=state;
    }
    if(actor->tasks)for(const auto& [name,definition]:actor->tasks->definitions) {
        TaskSelection selection{{actor->task_catalog,name}};
        auto& entry=add(CharacterTaskKind::Sequence,actor->tasks->domain,name,
            ControlCharacterTask{id,actor->creations,{TaskControlOperation::Select,std::move(selection)}});
        entry.sources=definition.sources;entry.inputs=definition.inputs;
        const auto found=actor->tasks->control.catalog.find(name);
        if(found==actor->tasks->control.catalog.end()) {
            entry.query.availability=TaskAvailability::Unknown;
            entry.query.reasons.push_back({TaskReasonKind::Unresolved,name,"task session is not prepared"});
        } else {
            const auto prior=entry.query;
            entry.query=found->second;
            if(prior.availability!=TaskAvailability::Available)entry.query.availability=prior.availability;
            entry.query.reasons.insert(entry.query.reasons.end(),prior.reasons.begin(),prior.reasons.end());
        }
    }
    auto order=[](CharacterTaskKind kind) {
        switch(kind) {
        case CharacterTaskKind::Sequence:case CharacterTaskKind::Npc:return 0;
        case CharacterTaskKind::Program:return 1;
        case CharacterTaskKind::Idle:return 2;
        case CharacterTaskKind::Ready:return 3;
        case CharacterTaskKind::Reaction:return 4;
        default:return 5;
        }
    };
    std::stable_sort(result.begin(),result.end(),[&](const auto& a,const auto& b){return order(a.kind)<order(b.kind);});
    return result;
}
}
