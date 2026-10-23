#include "Menu.hpp"
#include "MenuLabels.hpp"
#include "Widgets.hpp"
#include <hksr/framework/application/CharacterTasks.hpp>

namespace hksr::framework::ui {
void Menu::Actions(ApplicationService& host,const ApplicationSnapshot& snapshot,const SceneCharacterStatus& selected) {
    const bool npc=PureNpc(selected.package);
    const bool controlled=selected.tasks && std::any_of(selected.tasks->definitions.begin(),selected.tasks->definitions.end(),
        [](const auto& row){return bool(row.second.control);});
    bool diagnostic=false;
#if HKSR_DEVELOPMENT_UI
    diagnostic=controlled && action_kind_==1;
#endif
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
        const UiText kinds[]={controlled?UiText::BattleControls:UiText::ConfiguredActions,
            controlled?UiText::ActionPreviews:UiText::BattleStates};
        ImGui::SetNextItemWidth(230);
        if(BeginChoices(L(UiText::ActionCategory),T(kinds[action_kind_]))) {
            for(int i=0;i<2;++i)if(ImGui::Selectable(L(kinds[i]),i==action_kind_))action_kind_=i;
            ImGui::EndCombo();
        }
        diagnostic=controlled && action_kind_==1;
        if(action_kind_==0)
#endif
        {
            ImGui::RadioButton(L(UiText::Exploration),&action_domain_,0);ImGui::SameLine();
            ImGui::RadioButton(L(UiText::Battle),&action_domain_,1);
        }
    }
#if HKSR_DEVELOPMENT_UI
    ImGui::Checkbox(L(UiText::ShowUnavailableTasks),&show_unavailable_tasks_);
#endif
    if(selected.tasks) {
        const auto& control=selected.tasks->control;
        for(const auto& context:control.contexts) {
            auto line=context.phase.value_or(std::string{});
            for(const auto& [name,value]:context.values) {
                const auto text=std::visit([](const auto& input)->std::string {
                    using T=std::decay_t<decltype(input)>;
                    if constexpr(std::is_same_v<T,std::string>)return input;
                    else if constexpr(std::is_same_v<T,bool>)return input?"true":"false";
                    else return std::to_string(input);
                },value);
                if(!line.empty())line+=" · ";line+=name+": "+text;
            }
            if(!line.empty())Text(line);
            if(ImGui::IsItemHovered()){BeginHelp();Text(context.name);EndHelp();}
        }
        if(!control.status.error.empty())Text(control.status.error);
        if(!control.preparation_error.empty())Text(control.preparation_error);
        if(!controlled || diagnostic) {
            ImGui::BeginDisabled(!selected.active);
            if(ImGui::Button(L(UiText::FinishTask)))host.Post(ControlCharacterTask{actor_,selected.creations,{TaskControlOperation::Finish,{}}});
            ImGui::SameLine();
            if(ImGui::Button(L(UiText::CancelTask)))host.Post(ControlCharacterTask{actor_,selected.creations,{TaskControlOperation::Cancel,{}}});
            ImGui::EndDisabled();
        }
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##action-filter",T(UiText::SearchActions),action_filter_,sizeof(action_filter_));
    const auto* current=snapshot.catalog?snapshot.catalog->Actions(actor_):nullptr;
    const auto domain=action_domain_==0?ActionDomain::Exploration:ActionDomain::Alternate;
    ImGui::BeginChild("action-list",{0,std::max(100.0f,ImGui::GetContentRegionAvail().y-30)});
    int shown=0;
    for(const auto& entry:CharacterTasks(snapshot,actor_)) {
        if(npc && entry.kind!=CharacterTaskKind::Npc)continue;
#if HKSR_DEVELOPMENT_UI
        if(!npc && action_kind_==1) {
            if(entry.domain!=ActionDomain::Alternate)continue;
        } else
#endif
        if(!npc && entry.domain!=domain)continue;
        if(controlled && entry.domain==ActionDomain::Alternate && entry.diagnostic!=diagnostic)continue;
        const bool available=entry.query.availability==TaskAvailability::Available;
        const bool configurable=CanConfigureTask(entry);
        if(!available && !configurable && !show_unavailable_tasks_)continue;
        auto label=entry.recovery?std::string(T(UiText::RecoverReaction))+" · "+entry.label:entry.label;
        if(entry.kind==CharacterTaskKind::Idle)label=std::string(T(UiText::IdleState))+" · "+label;
        auto detail=entry.detail;
        if(entry.kind==CharacterTaskKind::Reaction)detail+="\n"+std::string(T(UiText::ReactionScope));
        for(const auto& source:entry.sources)detail+="\n"+source.document+" · "+source.field;
        for(const auto& reason:entry.query.reasons)detail+="\n"+reason.source+": "+reason.detail;
        if(!SourceMatches(label+" "+detail,action_filter_))continue;
        ImGui::PushID(shown++);
        const float width=std::max(1.0f,ImGui::GetContentRegionAvail().x);
        ImGui::BeginDisabled(!available && !configurable);
        if(ImGui::Selectable(label.c_str(),false,0,{width,ImGui::GetTextLineHeight()+12})) {
            if((entry.kind==CharacterTaskKind::Sequence || entry.kind==CharacterTaskKind::Control) && HasTaskInputs(entry))OpenTaskInputs(entry);
            else host.Post(entry.command);
        }
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && (!detail.empty() || ImGui::CalcTextSize(label.c_str()).x>width)) {
            BeginHelp();Text(label);Text(detail);EndHelp();
        }
        ImGui::EndDisabled();ImGui::PopID();
    }
    if(!shown)ImGui::TextWrapped("%s",T(UiText::NoActionMatches));
    ImGui::EndChild();
    if(current && !current->actions.finished)ImGui::TextDisabled("%s",T(UiText::ReadingActions));
}
}
