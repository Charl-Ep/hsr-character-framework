#include "Menu.hpp"
#include "MenuLabels.hpp"
#include "Widgets.hpp"
#include <set>

namespace hksr::framework::ui {
void Menu::Actions(ApplicationService& host,const ApplicationSnapshot& snapshot,const SceneCharacterStatus& selected) {
    const bool npc=PureNpc(selected.package);
    if(action_actor_!=actor_ || action_generation_!=selected.creations) {
        action_actor_=actor_;action_generation_=selected.creations;action_kind_=0;
        action_domain_=!npc && selected.handoff && selected.handoff->ready_domain==ActionDomain::Alternate?1:0;
    }
    if(selected.handoff && !selected.handoff->error.empty())Text(selected.handoff->error);
    if(selected.playback && selected.playback->last) {
        const auto& last=*selected.playback->last;
        if(last.outcome==ActionOutcome::StateAbsent)Text(T(UiText::StateMissing));
        else if(last.outcome==ActionOutcome::CallFailed)Text(last.detail);
    }
    if(!npc) {
#if HKSR_DEVELOPMENT_UI
        const UiText kinds[]={UiText::ConfiguredActions,UiText::BattleStates};
        ImGui::SetNextItemWidth(230);
        if(BeginChoices(L(UiText::ActionCategory),T(kinds[action_kind_]))) {
            for(int i=0;i<2;++i)if(ImGui::Selectable(L(kinds[i]),i==action_kind_))action_kind_=i;
            ImGui::EndCombo();
        }
        if(action_kind_==0)
#endif
        {
            ImGui::RadioButton(L(UiText::Exploration),&action_domain_,0);ImGui::SameLine();
            ImGui::RadioButton(L(UiText::Battle),&action_domain_,1);
        }
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##action-filter",T(UiText::SearchActions),action_filter_,sizeof(action_filter_));
    const auto* current=snapshot.catalog?snapshot.catalog->Actions(actor_):nullptr;
    const auto domain=action_domain_==0?ActionDomain::Exploration:ActionDomain::Alternate;
#if HKSR_DEVELOPMENT_UI
    const bool show_ready=!npc && action_kind_==1,show_configured=!show_ready;
#else
    const bool show_ready=!npc && domain==ActionDomain::Alternate,show_configured=true;
#endif
    ImGui::BeginDisabled(!selected.active);
    ImGui::BeginChild("action-list",{0,std::max(100.0f,ImGui::GetContentRegionAvail().y-30)});
    int shown=0;
    std::set<std::string> ready_names;
    auto row=[&](const std::string& label,const std::string& detail,CharacterCommand command,bool available=true) {
        if(!SourceMatches(label+" "+detail,action_filter_))return;
        ImGui::PushID(shown++);
        const float width=std::max(1.0f,ImGui::GetContentRegionAvail().x);
        bool pending=!available;
        if(const auto* selection=std::get_if<SelectCatalogAction>(&command)) {
            if(!std::holds_alternative<VisualActionKey>(selection->action.key))
                pending=pending || (current && !current->actions.Ready(std::holds_alternative<ReadyActionKey>(selection->action.key)?
                    ActionDomain::Alternate:std::get<ConfiguredActionKey>(selection->action.key).domain));
        }
        ImGui::BeginDisabled(pending);
        if(ImGui::Selectable(label.c_str(),false,0,{width,ImGui::GetTextLineHeight()+12}))host.Post(std::move(command));
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && (!detail.empty() || ImGui::CalcTextSize(label.c_str()).x>width)){BeginHelp();Text(label);Text(detail);EndHelp();}
        ImGui::EndDisabled();
        ImGui::PopID();
    };
    if(!npc && current && (show_ready || domain==ActionDomain::Alternate) && !current->actions.visual_states.empty()) {
        if(ImGui::CollapsingHeader(L(UiText::Reactions),ImGuiTreeNodeFlags_DefaultOpen)) {
            for(const auto& visual:current->actions.visual_states) {
                const auto& value=visual.value;
                if(!value.flag.available)continue;
                const auto& occur=value.occur.resolved_state;
                const auto& loop=value.loop.resolved_state;
                std::string label=occur.available && !occur.value.empty()?occur.value:loop.value;
                if(label.empty())label=T(UiText::VisualEffect);
                if(visual.expression.available && !visual.expression.value.empty())label+=" · "+visual.expression.value;
                if(value.priority.available && !value.priority.value.empty())label+=" · "+value.priority.value;
                const auto detail=occur.value+" → "+loop.value+" → "+value.recover.resolved_state.value+
                    "\n"+T(UiText::ReactionScope)+"\n"+visual.source+"\nflag="+std::to_string(value.flag.value);
                for(bool recover:{false,true}) {
                    const auto query=QueryVisualAction(visual,recover);
                    auto explanation=detail;for(const auto& issue:query.issues)explanation+="\n"+issue;
                    row(recover?std::string("    ")+T(UiText::RecoverReaction)+" · "+label:label,explanation,
                        SelectCatalogAction{actor_,{current->revision,VisualActionKey{visual.character_config,value.flag.value,recover}}},query.available);
                }
            }
        }
        ImGui::Separator();
    }
    if(show_ready && current) {
        if(!selected.battle_idle_state.empty()) {
            ready_names.insert(selected.battle_idle_state);
            row(std::string(T(UiText::IdleState))+" · "+selected.battle_idle_state,{},IdleCharacter{actor_});
        }
        for(const auto& set:current->actions.ready_actions)for(std::size_t i=0;i<set->poses.size();++i) {
            const auto& pose=set->poses[i];ready_names.insert(pose.state);
            row(pose.label+(pose.variant_index>=0?T(UiText::ConditionalVariant):""),{},
                SelectCatalogAction{actor_,{current->revision,ReadyActionKey{set->source,i}}});
#if HKSR_DEVELOPMENT_UI
            for(const auto* attack:ReadyAttacks(current->actions,*set,pose))
                row("    "+attack->state,{},SelectCatalogAction{actor_,{current->revision,ConfiguredActionKey{attack->domain,attack->state}}});
#endif
        }
    }
    if(show_configured) {
        if(!npc && domain==ActionDomain::Alternate)for(const auto& [key,state]:selected.program_actions)
            row(key,state,SelectCharacterAction{actor_,{ActionDomain::Alternate,ProgramAction{selected.visual_program,key}}});
        if(npc) {
            const auto found=snapshot.npc_actions.find(actor_);
            if(found!=snapshot.npc_actions.end() && found->second->creation==selected.creations)
                for(const auto& motion:found->second->source.motions)
                    row(motion.name.available?motion.name.value:motion.indexed_name,{},
                        SelectCharacterAction{actor_,{ActionDomain::Exploration,NpcMotion{motion.id}}});
        }else if(current)for(const auto& proposal:current->actions.proposals) {
            if(proposal.domain!=domain || ready_names.count(proposal.state))continue;
            row(proposal.state,{},SelectCatalogAction{actor_,{current->revision,ConfiguredActionKey{domain,proposal.state}}});
        }
    }
    if(!shown)ImGui::TextWrapped("%s",T(UiText::NoActionMatches));
    ImGui::EndChild();ImGui::EndDisabled();
    if(current && !current->actions.finished)ImGui::TextDisabled("%s",T(UiText::ReadingActions));
}
}
