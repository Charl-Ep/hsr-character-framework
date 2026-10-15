#pragma once
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include "Localization.hpp"

namespace hksr::framework::ui {
inline bool BeginChoices(const char* label,const char* preview) {
    return ImGui::BeginCombo(label,preview,ImGuiComboFlags_HeightLarge);
}
inline void BeginHelp() {
    // An auto-sized tooltip has no useful content width on its first frame.
    // TextWrapped alone feeds that width back into its own auto-sizing.
    ImGui::BeginTooltip();
    const float width=std::min(680.0f,std::max(160.0f,ImGui::GetIO().DisplaySize.x-40.0f));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+width);
}
inline void EndHelp() {ImGui::PopTextWrapPos();ImGui::EndTooltip();}
// Emit only one axis per button. The official-frame consumer combines rapid
// steps against its pending pose, rather than repeatedly reading a stale pose.
template<class Submit> void DrawPlacementSteps(float distance,float degrees,Submit submit,Language language=DefaultLocale) {
    ImGui::PushButtonRepeat(true);
    const float w=52,h=30,gap=ImGui::GetStyle().ItemSpacing.x;
    auto key=[&](const char* label,int axis,float direction) {
        const float step=std::abs(distance);
        ImGui::BeginDisabled(!std::isfinite(step) || step==0);
        if(ImGui::Button(label,{w,h}))submit(axis,direction*step);
        ImGui::EndDisabled();
    };
    ImGui::BeginGroup();
    ImGui::Dummy({w,h});ImGui::SameLine();key("Z+",2,1);
    key("X-",0,-1);ImGui::SameLine();ImGui::Dummy({w,h});ImGui::SameLine();key("X+",0,1);
    ImGui::Dummy({w,h});ImGui::SameLine();key("Z-",2,-1);
    ImGui::EndGroup();ImGui::SameLine(0,24);
    ImGui::BeginGroup();key("Y+",1,1);
    ImGui::PushButtonRepeat(false);
    if(ImGui::Button(Translate(UiText::SnapGround,language),{w,h}))submit(4,0);
    if(ImGui::IsItemHovered()){BeginHelp();ImGui::TextUnformatted(Translate(UiText::SnapGroundHelp,language));EndHelp();}
    ImGui::PopButtonRepeat();key("Y-",1,-1);ImGui::EndGroup();
    auto turn=[&](bool right) {
        const float step=std::abs(degrees);ImGui::BeginDisabled(!std::isfinite(step) || step==0);
        if(ImGui::Button(right?"##rotate-right":"##rotate-left",{(3*w+gap)/2,h}))submit(3,right?step:-step);
        const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();const ImVec2 center{(a.x+b.x)/2,(a.y+b.y)/2};
        auto* draw=ImGui::GetWindowDrawList();const auto color=ImGui::GetColorU32(ImGuiCol_Text);
        draw->PathArcTo(center,8,right?0.7f:2.4f,right?5.7f:-2.6f,20);draw->PathStroke(color,0,1.7f);
        const float x=center.x+(right?6.5f:-6.5f),y=center.y-4.5f;
        draw->AddTriangleFilled({x,y},{x+(right?-6:6),y-2},{x,y+5},color);
        if(ImGui::IsItemHovered()){BeginHelp();ImGui::TextUnformatted(Translate(right?UiText::TurnRight:UiText::TurnLeft,language));EndHelp();}
        ImGui::EndDisabled();
    };
    turn(false);ImGui::SameLine();turn(true);
    ImGui::PopButtonRepeat();
}
}
