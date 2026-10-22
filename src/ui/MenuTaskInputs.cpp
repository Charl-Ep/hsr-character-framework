#include "Menu.hpp"
#include "MenuLabels.hpp"
#include "Widgets.hpp"

namespace hksr::framework::ui {
namespace {
bool TextInput(const char* label,std::string& value) {
    return ImGui::InputText(label,value.data(),value.capacity()+1,ImGuiInputTextFlags_CallbackResize,
        [](ImGuiInputTextCallbackData* data) {
            auto& text=*static_cast<std::string*>(data->UserData);
            text.resize(static_cast<std::size_t>(data->BufTextLen));data->Buf=text.data();return 0;
        },&value);
}
TaskValue EmptyValue(TaskValueType type) {
    switch(type) {
    case TaskValueType::Boolean:return false;
    case TaskValueType::Integer:return std::int64_t(0);
    case TaskValueType::Number:return 0.0;
    default:return std::string{};
    }
}
}
void Menu::TaskInputs(ApplicationService& host,const ApplicationSnapshot& snapshot) {
    auto& draft=*task_inputs_;
    const auto actor=std::find_if(snapshot.characters.begin(),snapshot.characters.end(),[&](const auto& row){return row.id==draft.command.id;});
    if(actor==snapshot.characters.end() || actor->deleting || actor->creations!=draft.command.creation ||
        actor->id!=actor_ || !actor->tasks || actor->task_catalog!=draft.Selection().task.catalog) {task_inputs_.reset();return;}
    draft.Observe(snapshot,*actor);
    const auto display=ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize({std::min(580.0f,display.x-32),std::min(560.0f,display.y-32)},ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({display.x/2,display.y/2},ImGuiCond_FirstUseEver,{0.5f,0.5f});
    ImGui::SetNextWindowSizeConstraints({320,260},{display.x-16,display.y-16});
    bool open=true,played=false;
    if(ImGui::Begin(L(UiText::TaskInputs),&open)) {
        Text(draft.entry.label);
        ImGui::BeginChild("fields",{0,std::max(60.0f,ImGui::GetContentRegionAvail().y-110)});
        auto& selection=draft.Selection();
        const auto* rows=snapshot.catalog?snapshot.catalog->rows.get():nullptr;
        auto field=[&](const std::string& name) {ImGui::PushID(name.c_str());Text(name);ImGui::SetNextItemWidth(-90);};
        auto reset=[&](auto& map,const std::string& name) {
            ImGui::SameLine();if(ImGui::SmallButton(L(UiText::TaskDefault))){map.erase(name);draft.Changed();}
            ImGui::PopID();
        };
        ImGui::PushID("scalars");
        for(const auto& input:draft.entry.inputs) {
            field(input.name);auto value=EmptyValue(input.type);
            if(input.default_value)value=*input.default_value;
            if(const auto found=draft.entry.query.inputs.find(input.name);found!=draft.entry.query.inputs.end())value=found->second;
            if(const auto found=selection.query.inputs.find(input.name);found!=selection.query.inputs.end())value=found->second;
            const bool changed=std::visit([&](auto& v) {
                using T=std::decay_t<decltype(v)>;
                if constexpr(std::is_same_v<T,bool>)return ImGui::Checkbox("##value",&v);
                else if constexpr(std::is_same_v<T,std::int64_t>)return ImGui::InputScalar("##value",ImGuiDataType_S64,&v);
                else if constexpr(std::is_same_v<T,double>)return ImGui::InputDouble("##value",&v);
                else return TextInput("##value",v);
            },value);
            if(changed){selection.query.inputs[input.name]=std::move(value);draft.Changed();}
            reset(selection.query.inputs,input.name);
        }
        ImGui::PopID();ImGui::PushID("targets");
        for(const auto& name:draft.entry.bindings.targets) {
            field(name);const auto target=selection.visual.targets.find(name);
            auto label=std::string(T(target==selection.visual.targets.end()?UiText::TaskDefault:UiText::TaskUnbound));
            if(target!=selection.visual.targets.end() && target->second) {
                const auto found=std::find_if(snapshot.characters.begin(),snapshot.characters.end(),[&](const auto& row){return row.id==target->second;});
                label=found==snapshot.characters.end()?T(UiText::TaskTargetMissing):ActorName(found->package,rows,language_);
            }
            if(BeginChoices("##target",label.c_str())) {
                if(ImGui::Selectable(L(UiText::TaskUnbound))){selection.visual.targets[name]=0;draft.Changed();}
                for(const auto& row:snapshot.characters)if(!row.deleting && row.scene==actor->scene) {
                    const auto identity=std::to_string(row.id);ImGui::PushID(identity.c_str());
                    ImGui::BeginDisabled(!row.active || !row.enabled);
                    if(ImGui::Selectable(ActorName(row.package,rows,language_).c_str(),target!=selection.visual.targets.end() && target->second==row.id)) {
                        selection.visual.targets[name]=row.id;draft.Changed();
                    }
                    ImGui::EndDisabled();ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            reset(selection.visual.targets,name);
        }
        ImGui::PopID();ImGui::PushID("choices");
        for(const auto& [name,options]:draft.entry.bindings.choices) {
            field(name);const auto choice=selection.visual.choices.find(name);
            if(BeginChoices("##choice",choice==selection.visual.choices.end()?T(UiText::TaskDefault):choice->second.c_str())) {
                for(const auto& option:options)if(ImGui::Selectable(option.c_str(),choice!=selection.visual.choices.end() && choice->second==option)) {
                    selection.visual.choices[name]=option;draft.Changed();
                }
                ImGui::EndCombo();
            }
            reset(selection.visual.choices,name);
        }
        ImGui::PopID();ImGui::PushID("anchors");
        for(const auto& name:draft.entry.bindings.anchors) {
            ImGui::PushID(name.c_str());Text(name);
            auto found=selection.visual.anchors.find(name);
            if(found==selection.visual.anchors.end()) {
                if(ImGui::Button(L(UiText::TaskUseActorPose))) {
                    selection.visual.anchors[name]={actor->placement.position,actor->placement.rotation};draft.Changed();
                }
            } else {
                auto& anchor=found->second;
                float position[]={anchor.position.x,anchor.position.y,anchor.position.z};
                float rotation[]={anchor.rotation.x,anchor.rotation.y,anchor.rotation.z,anchor.rotation.w};
                ImGui::SetNextItemWidth(-1);Text(T(UiText::WorldPosition));
                if(ImGui::InputFloat3("##position",position)){anchor.position={position[0],position[1],position[2]};draft.Changed();}
                ImGui::SetNextItemWidth(-1);Text(T(UiText::TaskQuaternion));
                if(ImGui::InputFloat4("##rotation",rotation)){anchor.rotation={rotation[0],rotation[1],rotation[2],rotation[3]};draft.Changed();}
            }
            if(ImGui::SmallButton(L(UiText::TaskDefault))){selection.visual.anchors.erase(name);draft.Changed();}
            ImGui::PopID();
        }
        ImGui::PopID();ImGui::EndChild();
        draft.Query(host);
        ImGui::BeginChild("query-result",{0,65});
        Text(draft.error);
        if(draft.dirty || draft.pending)Text(T(UiText::TaskChecking));
        else if(draft.result)for(const auto& reason:draft.result->query.reasons)Text(reason.source+": "+reason.detail);
        ImGui::EndChild();
        ImGui::BeginDisabled(!draft.Ready() || !actor->active || !actor->enabled);
        if(ImGui::Button(L(UiText::Play))){host.Post(draft.command);played=true;}
        ImGui::EndDisabled();
    }
    ImGui::End();if(!open || played)task_inputs_.reset();
}
}
