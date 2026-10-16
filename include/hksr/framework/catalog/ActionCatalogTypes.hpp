#pragma once
#include "../presentation/ActionTypes.hpp"
#include <map>
#include <set>
#include <tuple>

namespace hksr::framework {
struct ActionSourceKey {
    api::ActionConfigKind kind=api::ActionConfigKind::Adventure;
    std::string path;
    bool operator<(const ActionSourceKey& other) const { return std::tie(kind,path)<std::tie(other.kind,other.path); }
};
struct ActionSourceRoot { ActionDomain domain; ActionSourceKey source; };
struct ActionReference {
    ActionSourceKey source;
    std::string field;
    std::optional<api::AnimationTaskSource> task;
    std::string root_ability; // official config reference, including discovered TriggerAbility edges
};
struct ActionProposal {
    ActionDomain domain;
    std::string state;
    std::vector<ActionReference> references;
    std::vector<ActionRetirement> retirements;
    // A request candidate, not proof that HasState succeeds or that the state
    // is safe to enter in every phase. Existing ActionPlayback checks current
    // model state; this catalog doesn't execute skills or infer transitions.
    ActionRequest Request() const {
        if(domain==ActionDomain::Exploration)return AnimatorState{state,0,0};
        return FreeStyleState{state,0,0,retirements};
    }
};
struct ActionDocumentStatus { ActionSourceRoot input; bool attempted=false,loaded=false; std::vector<api::CatalogIssue> issues; };
struct AbilityDocumentStatus {
    ActionDomain domain;
    std::string ability;
    bool attempted=false,reader_available=false;
    std::vector<api::AbilitySourceIssue> issues;
    std::size_t removal_sources=0,linked_retirements=0;
};
struct ActionCatalogStatus {
    bool finished=false;
    std::set<ActionDomain> pending_domains;
    bool Ready(ActionDomain domain) const {return !pending_domains.count(domain);}
    std::vector<ActionDocumentStatus> documents;
    std::vector<ActionProposal> proposals;
    std::vector<AbilityDocumentStatus> abilities;
    std::vector<VisualState> visual_states;
    std::vector<api::VisualConfigSources> visual_sources;
    std::vector<std::shared_ptr<const ReadyActions>> ready_actions;
};
inline std::vector<const ActionProposal*> ReadyAttacks(const ActionCatalogStatus& catalog,const ReadyActions& source,const ReadyPose& pose) {
    std::vector<const ActionProposal*> result;
    std::set<std::string> entries,visited;
    auto add_entry=[&](const api::TextInput& entry) {
        if(entry.available && !entry.value.empty())entries.insert(entry.value);
    };
    add_entry(pose.entry_ability);
    std::vector<std::string> pending;
    if(pose.child_skills.available)pending=pose.child_skills.values;
    for(std::size_t i=0;i<pending.size();++i) {
        const auto name=pending[i];
        if(name.empty() || !visited.insert(name).second)continue;
        const api::SkillInput* child=nullptr;unsigned matches=0;
        for(const auto& skill:source.skills)if(skill.name.available && skill.name.value==name){child=&skill;++matches;}
        // A missing/duplicate name does not establish an edge. Other explicit
        // branches remain usable; never guess a replacement from SkillNN.
        if(matches!=1)continue;
        add_entry(child->entry_ability);
        if(child->child_skills.available)pending.insert(pending.end(),child->child_skills.values.begin(),child->child_skills.values.end());
    }
    for(const auto& proposal:catalog.proposals) {
        if(proposal.domain!=ActionDomain::Alternate || proposal.state==source.initial_state)continue;
        for(const auto& ref:proposal.references)if(ref.source.path==source.source && entries.count(ref.root_ability) && ref.task &&
            ref.task->enabled.available && ref.task->enabled.value && ref.task->target_alias.available && ref.task->target_alias.value=="Caster") {
            result.push_back(&proposal);break;
        }
    }
    return result;
}
}
