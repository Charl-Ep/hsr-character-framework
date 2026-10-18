#pragma once
#include "Localization.hpp"
#include <hksr/framework/application/ApplicationService.hpp>
#include <sstream>
#include <locale>

namespace hksr::framework::ui {
struct CharacterStatusLine {std::string text,error;};
// Values used by presentation, not an animation/loading receipt or a guessed
// copy of gameplay variables. Rendering performs no game calls.
inline CharacterStatusLine CharacterStatus(const ApplicationSnapshot& snapshot,const SceneCharacterStatus& actor,Language language) {
    CharacterStatusLine out;
    out.error=!actor.error.empty()?actor.error:actor.creation_error;
    if(out.error.empty() && actor.handoff)out.error=actor.handoff->error;
    if(out.error.empty() && actor.action && actor.action->phase==ActionIntentPhase::Failed)out.error=actor.action->detail;
    if(out.error.empty() && actor.playback && actor.playback->last) {
        const auto& last=*actor.playback->last;
        if(last.outcome==ActionOutcome::StateAbsent)out.error=Translate(UiText::StateMissing,language);
        else if(last.outcome==ActionOutcome::Unavailable || last.outcome==ActionOutcome::CallFailed)out.error=last.detail;
    }
    if(!actor.active || !actor.enabled || !snapshot.scene || snapshot.scene->key!=actor.scene)return out;
    auto append=[&](const std::string& value){if(!out.text.empty())out.text+='\n';out.text+=value;};
    if(!actor.branch_state.phase.empty())append(std::string(Translate(UiText::BranchPhase,language))+": "+actor.branch_state.phase);
    for(const auto& item:actor.branch_state.values) {
        std::string value;
        if(const auto* text=std::get_if<std::string>(&item.value))value=*text;
        else {std::ostringstream number;number.imbue(std::locale::classic());number<<std::get<float>(item.value);value=number.str();}
        append((item.scope.empty()?std::string{}:item.scope+".")+item.name+" = "+value);
    }
    return out;
}
}
