#pragma once
#include "Localization.hpp"
#include <hksr/framework/application/ApplicationService.hpp>
#include <type_traits>

namespace hksr::framework::ui {
struct CharacterStatusLine {std::string text,error;};
// Copied host state only. Rendering the menu must not enumerate controllers
// or call the game to obtain a status label.
inline CharacterStatusLine CharacterStatus(const ApplicationSnapshot& snapshot,const SceneCharacterStatus& actor,Language language) {
    CharacterStatusLine out;
    out.error=!actor.error.empty()?actor.error:actor.creation_error;
    if(out.error.empty() && actor.handoff)out.error=actor.handoff->error;
    if(out.error.empty() && actor.action && actor.action->phase==ActionIntentPhase::Failed)out.error=actor.action->detail;
    const auto domain=actor.handoff?actor.handoff->ready_domain:std::optional<ActionDomain>{};
    std::string mode=domain?Translate(*domain==ActionDomain::Alternate?UiText::Battle:UiText::Exploration,language):std::string{};
    UiText phase=UiText::CharacterReady;
    bool show_action=false;
    if(actor.deleting || (actor.session && actor.session->phase==SessionPhase::Closing))phase=UiText::CharacterRemoving;
    else if(!out.error.empty() || (actor.session && actor.session->phase==SessionPhase::Failed))phase=UiText::CharacterFailed;
    else if(!actor.enabled)phase=UiText::Removed;
    else if(!snapshot.scene || snapshot.scene->key!=actor.scene)phase=UiText::CharacterOtherMap;
    else if(!actor.active || !actor.creation_ready)phase=UiText::CharacterLoading;
    else if(actor.handoff && actor.handoff->phase!=HandoffPhase::Ready)phase=UiText::CharacterSwitching;
    else if((actor.action && (actor.action->phase==ActionIntentPhase::Waiting || actor.action->phase==ActionIntentPhase::Dispatching)) ||
        (actor.playback && actor.playback->pending))phase=UiText::CharacterPreparingAction;
    else show_action=true;
    std::string action;
    if(show_action && actor.playback && actor.playback->last) {
        const auto& playback=*actor.playback;const auto& last=*playback.last;
        if(last.outcome==ActionOutcome::Returned) {
            if(playback.successor && ((playback.successor->returned && playback.successor->serial==last.serial) ||
                (playback.successor->observed && playback.successor->predecessor==last.serial)))action=playback.successor->state;
            else action=std::visit([&](const auto& request)->std::string {
                using T=std::decay_t<decltype(request)>;
                if constexpr(std::is_same_v<T,AnimatorState> || std::is_same_v<T,FreeStyleState>)return request.name;
                else if constexpr(std::is_same_v<T,ProgramAction>) {
                    const auto body=actor.program_actions.find(request.action);
                    return body==actor.program_actions.end()?request.action:request.action+" / "+body->second;
                }
                else if constexpr(std::is_same_v<T,BattleIdle>)return actor.battle_idle_state.empty()?Translate(UiText::IdleState,language):actor.battle_idle_state;
                else if constexpr(std::is_same_v<T,ReadyState>) {
                    if(request.actions && request.pose<request.actions->poses.size())return request.actions->poses[request.pose].state;
                    return {};
                }else if constexpr(std::is_same_v<T,NpcMotion>) {
                    const auto found=snapshot.npc_actions.find(actor.id);
                    if(found!=snapshot.npc_actions.end() && found->second->creation==actor.creations)
                        for(const auto& motion:found->second->source.motions)if(motion.id==request.id)
                            return motion.name.available?motion.name.value:motion.indexed_name;
                    return {};
                }else return Translate(UiText::BattleStates,language);
            },last.request);
        }else if(last.outcome==ActionOutcome::StateAbsent || last.outcome==ActionOutcome::Unavailable || last.outcome==ActionOutcome::CallFailed) {
            phase=UiText::CharacterFailed;
            if(out.error.empty())out.error=last.outcome==ActionOutcome::StateAbsent?Translate(UiText::StateMissing,language):last.detail;
        }
    }
    out.text=std::move(mode);
    auto append=[&](const std::string& value){if(value.empty())return;if(!out.text.empty())out.text+=" · ";out.text+=value;};
    append(action);append(Translate(phase,language));return out;
}
}
