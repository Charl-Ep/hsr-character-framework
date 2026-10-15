#include "Menu.hpp"
#include "MenuLabels.hpp"
#include "Widgets.hpp"
#include "ControlMessages.hpp"
#include <cmath>

namespace hksr::framework::ui {
void Menu::Controls(ApplicationService& host,const ApplicationSnapshot& snapshot,const SceneCharacterStatus& selected) {
    const auto same_look=[](const LookIntent& a,const LookIntent& b){return a.mode==b.mode && a.target==b.target && a.point.x==b.point.x && a.point.y==b.point.y && a.point.z==b.point.z && a.max_distance==b.max_distance;};
    if(look_pending_ && same_look(*look_pending_,selected.look))look_pending_.reset();
    const bool changed_actor=edited_actor_!=actor_;
    if(edited_actor_!=actor_) {
        edited_actor_=actor_;pose_dirty_=false;look_pending_.reset();
    }
    if(changed_actor || (!look_pending_ && !ImGui::IsAnyItemActive())) {
        const auto& look=selected.look;
        look_mode_=look.mode==LookMode::Automatic?0:look.mode==LookMode::Disabled?1:look.mode==LookMode::Point?3:2;
        look_target_=look.mode==LookMode::Player?0:look.target;
        look_point_[0]=look.point.x;look_point_[1]=look.point.y;look_point_[2]=look.point.z;
        look_limit_=look.max_distance>0;if(look_limit_)look_distance_=look.max_distance;
    }
    const auto pose=selected.controls && selected.controls->live.ready?
        CharacterPlacement{selected.controls->live.position,selected.controls->live.rotation}:selected.placement;
    if(!pose_dirty_ && (!selected.controls || !selected.controls->placement_pending)) {
        edit_position_[0]=pose.position.x;edit_position_[1]=pose.position.y;edit_position_[2]=pose.position.z;
        const auto q=pose.rotation;edit_yaw_=std::atan2(2*(q.w*q.y+q.x*q.z),1-2*(q.y*q.y+q.x*q.x))*57.295779513f;
    }
    auto rotation=[](float degrees){const auto half=degrees*0.00872664626f;return api::Rotation4{0,std::sin(half),0,std::cos(half)};};
    if(ImGui::CollapsingHeader(L(UiText::PositionHeading),ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BeginDisabled(!selected.active || !selected.controls);
        pose_dirty_|=ImGui::InputFloat3(L(UiText::WorldPosition),edit_position_);
        pose_dirty_|=ImGui::InputFloat(L(UiText::YawDegrees),&edit_yaw_);
        if(ImGui::Button(L(UiText::ApplyPlacement))) {
            host.Post(PlaceCharacter{actor_,{{edit_position_[0],edit_position_[1],edit_position_[2]},rotation(edit_yaw_)},false});pose_dirty_=false;
        }
        ImGui::SameLine();if(ImGui::Button(L(UiText::ReadPosition)))pose_dirty_=false;
        ImGui::InputFloat(L(UiText::MoveStep),&move_step_,0.01f,0.1f,"%.3f");
        ImGui::InputFloat(L(UiText::TurnStep),&turn_step_,1.0f,15.0f,"%.1f");
        DrawPlacementSteps(move_step_,turn_step_,[&](int axis,float amount) {
            CharacterPlacement delta;delta.rotation={0,0,0,1};
            if(axis==0)delta.position.x=amount;
            else if(axis==1){delta.position.y=amount;delta.snap_to_ground=false;}
            else if(axis==2)delta.position.z=amount;
            else if(axis==4)delta.snap_to_ground=true;
            else delta.rotation=rotation(amount);
            host.Post(PlaceCharacter{actor_,delta,true});pose_dirty_=false;
        },language_);
        ImGui::EndDisabled();
        if(PureNpc(selected.package))Text(T(UiText::NpcPlacementHint));
        if(selected.controls)Text(selected.controls->live.error);
    }
    if(ImGui::CollapsingHeader(L(UiText::GazeHeading),ImGuiTreeNodeFlags_DefaultOpen)) {
        bool changed=false;
        const UiText modes[]={UiText::Automatic,UiText::Disabled,UiText::LookEntity,UiText::LookPoint};
        if(BeginChoices(L(UiText::LookMode),T(modes[look_mode_]))) {
            for(int i=0;i<4;++i)if(ImGui::Selectable(L(modes[i]),look_mode_==i)){look_mode_=i;changed=true;}
            ImGui::EndCombo();
        }
        const auto* rows=snapshot.catalog && snapshot.catalog->rows?snapshot.catalog->rows.get():nullptr;
        std::string target_label=look_target_?T(UiText::SelectInstance):T(UiText::PlayerTarget);
        for(const auto& row:snapshot.characters)if(row.id==look_target_ && !row.deleting)target_label=ActorName(row.package,rows,language_);
        if(look_mode_==2 && BeginChoices(L(UiText::LookTarget),target_label.c_str())) {
            if(ImGui::Selectable(L(UiText::PlayerTarget),look_target_==0)){look_target_=0;changed=true;}
            for(const auto& row:snapshot.characters)if(row.id!=actor_ && !row.deleting && row.scene==selected.scene) {
                const auto label=ActorName(row.package,rows,language_)+"###look-"+std::to_string(row.id);
                if(ImGui::Selectable(label.c_str(),row.id==look_target_)){look_target_=row.id;changed=true;}
            }
            ImGui::EndCombo();
        }
        if(look_mode_==3)changed|=ImGui::InputFloat3(L(UiText::GazePosition),look_point_);
        if(look_mode_>=2) {
            changed|=ImGui::Checkbox(L(UiText::GazeDistanceLimit),&look_limit_);
            if(look_limit_ && ImGui::InputFloat(L(UiText::GazeMaxDistance),&look_distance_,0.5f,5,"%.1f")) {
                if(std::isfinite(look_distance_))look_distance_=std::max(0.1f,look_distance_);changed=true;
            }
        }
        if(changed && std::isfinite(look_distance_) && std::isfinite(look_point_[0]) && std::isfinite(look_point_[1]) && std::isfinite(look_point_[2])) {
            LookIntent intent;intent.mode=look_mode_==0?LookMode::Automatic:look_mode_==1?LookMode::Disabled:look_mode_==3?LookMode::Point:look_target_?LookMode::Instance:LookMode::Player;
            intent.target=look_target_;intent.point={look_point_[0],look_point_[1],look_point_[2]};intent.max_distance=look_limit_?look_distance_:0;
            look_pending_=intent;look_serial_=host.Post(LookCharacter{actor_,intent});
        }
        if(selected.controls)Text(GazeStatusText(*selected.controls,language_));
    }
}
}
