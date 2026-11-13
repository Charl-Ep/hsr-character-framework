#include "Menu.hpp"
#include "MenuLabels.hpp"
#include "Widgets.hpp"
#include "CurrentMapCharacters.hpp"
#include <misc/cpp/imgui_stdlib.h>

namespace hksr::framework::ui {
namespace {
TaskValue EmptyValue(TaskValueType type) {
    switch (type) {
    case TaskValueType::Boolean:
        return false;
    case TaskValueType::Integer:
        return std::int64_t(0);
    case TaskValueType::Number:
        return 0.0;
    default:
        return std::string{};
    }
}
}
void Menu::VariantInputs(const ApplicationSnapshot& snapshot) {
    const auto draft = *variant_inputs_;
    const auto* actor = FindCurrentMapCharacter(snapshot, draft.actor);
    if (!actor || actor->id != actor_ ||
        actor->creations != draft.creation) {
        variant_inputs_.reset();
        return;
    }
    const auto entries = CharacterTasks(snapshot, draft.actor);
    const auto entry = std::find_if(entries.begin(), entries.end(),
                                    [&](const auto& row) { return ActionRowIdentity(row) == draft.row; });
    if (entry == entries.end() || !HasTaskVariants(*entry)) {
        variant_inputs_.reset();
        return;
    }
    const auto display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize({std::min(580.0f, display.x - 32), std::min(400.0f, display.y - 32)},
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({display.x / 2, display.y / 2}, ImGuiCond_FirstUseEver, {0.5f, 0.5f});
    ImGui::SetNextWindowSizeConstraints({320, 220}, {display.x - 16, display.y - 16});
    bool open = true;
    if (ImGui::Begin(L(UiText::VisualSettings), &open)) {
        Text(entry->label);
        const auto selected = variant_settings_.Selected(*entry, draft.actor, draft.creation);
        for (const auto& variant : entry->variants) {
            ImGui::PushID(variant.key.c_str());
            if (ImGui::RadioButton(variant.label.c_str(), variant.key == selected))
                variant_settings_.Choose(*entry, draft.actor, draft.creation, variant.key);
            if (variant.key == selected) {
                Text(variant.detail);
                for (const auto& reason : variant.query.reasons)
                    Text(reason.detail);
            }
            ImGui::PopID();
        }
    }
    ImGui::End();
    if (!open)
        variant_inputs_.reset();
}
void Menu::TaskInputs(ApplicationService& host, const ApplicationSnapshot& snapshot) {
    auto& draft = *task_inputs_;
    const auto* actor = FindCurrentMapCharacter(snapshot, draft.command.id);
    if (!actor || actor->creations != draft.command.creation ||
        actor->id != actor_ || !actor->tasks || actor->task_catalog != draft.Selection().task.catalog ||
        !draft.CatalogMatches(actor->task_catalog_revision)) {
        task_inputs_.reset();
        return;
    }
    draft.Observe(snapshot, *actor);
    const auto display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize({std::min(580.0f, display.x - 32), std::min(560.0f, display.y - 32)},
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({display.x / 2, display.y / 2}, ImGuiCond_FirstUseEver, {0.5f, 0.5f});
    ImGui::SetNextWindowSizeConstraints({320, 260}, {display.x - 16, display.y - 16});
    bool open = true;
    if (ImGui::Begin(L(UiText::VisualSettings), &open)) {
        Text(draft.entry.label);
        ImGui::BeginChild("fields", {0, std::max(60.0f, ImGui::GetContentRegionAvail().y - 85)});
        auto& selection = draft.Selection();
        const auto* rows = snapshot.catalog ? snapshot.catalog->rows.get() : nullptr;
        auto field = [&](const std::string& name) {
            ImGui::PushID(name.c_str());
            Text(name);
            ImGui::SetNextItemWidth(-90);
        };
        auto reset = [&](auto& map, const std::string& name) {
            ImGui::SameLine();
            if (ImGui::SmallButton(L(UiText::TaskDefault))) {
                map.erase(name);
                draft.Changed();
            }
            ImGui::PopID();
        };
        ImGui::PushID("scalars");
        const bool technical = HKSR_DEVELOPMENT_UI != 0 && show_technical_inputs_;
        for (const auto& input : draft.entry.inputs) {
            if (input.diagnostic && !technical)
                continue;
            if (!input.choices.empty() && TaskInputChoiceCount(input, technical) <= 1 && input.default_value)
                continue;
            ImGui::PushID(input.name.c_str());
            Text(input.label.empty() ? input.name : input.label);
            if (!input.description.empty())
                Text(input.description);
            ImGui::SetNextItemWidth(-90);
            auto value = EmptyValue(input.type);
            if (input.default_value)
                value = *input.default_value;
            if (const auto found = draft.entry.query.inputs.find(input.name); found != draft.entry.query.inputs.end())
                value = found->second;
            if (const auto found = selection.query.inputs.find(input.name); found != selection.query.inputs.end())
                value = found->second;
            bool changed = false;
            if (!input.choices.empty()) {
                const auto current = std::find_if(input.choices.begin(), input.choices.end(), [&](const auto& option) {
                    return option.value == value && TaskInputChoiceVisible(option, technical);
                });
                if (BeginChoices("##value",
                                 current == input.choices.end() ? T(UiText::TaskUnbound) : current->label.c_str())) {
                    for (std::size_t index = 0; index < input.choices.size(); ++index) {
                        const auto& option = input.choices[index];
                        ImGui::PushID(static_cast<int>(index));
                        if (!TaskInputChoiceVisible(option, technical)) {
                            ImGui::PopID();
                            continue;
                        }
                        if (ImGui::Selectable(option.label.c_str(), option.value == value)) {
                            value = option.value;
                            changed = true;
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
            } else
                changed = std::visit(
                    [&](auto& v) {
                        using T = std::decay_t<decltype(v)>;
                        if constexpr (std::is_same_v<T, bool>)
                            return ImGui::Checkbox("##value", &v);
                        else if constexpr (std::is_same_v<T, std::int64_t>)
                            return ImGui::InputScalar("##value", ImGuiDataType_S64, &v);
                        else if constexpr (std::is_same_v<T, double>)
                            return ImGui::InputDouble("##value", &v);
                        else
                            return ImGui::InputText("##value", &v);
                    },
                    value);
            if (changed) {
                selection.query.inputs[input.name] = std::move(value);
                draft.Changed();
            }
            reset(selection.query.inputs, input.name);
        }
        ImGui::PopID();
        ImGui::PushID("targets");
        for (const auto& name : draft.entry.bindings.targets) {
            field(name);
            const auto target = selection.visual.targets.find(name);
            auto label =
                std::string(T(target == selection.visual.targets.end() ? UiText::TaskDefault : UiText::TaskUnbound));
            if (target != selection.visual.targets.end() && target->second) {
                const auto found = std::find_if(snapshot.characters.begin(), snapshot.characters.end(),
                                                [&](const auto& row) { return row.id == target->second; });
                label = found == snapshot.characters.end()
                            ? T(UiText::TaskTargetMissing)
                            : ActorName(found->package, rows, language_, found->skin_id,
                                        snapshot.catalog ? snapshot.catalog->skins.get() : nullptr);
            }
            if (BeginChoices("##target", label.c_str())) {
                if (ImGui::Selectable(L(UiText::TaskUnbound))) {
                    selection.visual.targets[name] = 0;
                    draft.Changed();
                }
                for (const auto& row : snapshot.characters)
                    if (!row.deleting && row.scene == actor->scene) {
                        const auto identity = std::to_string(row.id);
                        ImGui::PushID(identity.c_str());
                        ImGui::BeginDisabled(!row.active || !row.enabled);
                        if (ImGui::Selectable(ActorName(row.package, rows, language_, row.skin_id,
                                                        snapshot.catalog ? snapshot.catalog->skins.get() : nullptr)
                                                  .c_str(),
                                              target != selection.visual.targets.end() && target->second == row.id)) {
                            selection.visual.targets[name] = row.id;
                            draft.Changed();
                        }
                        ImGui::EndDisabled();
                        ImGui::PopID();
                    }
                ImGui::EndCombo();
            }
            reset(selection.visual.targets, name);
        }
        ImGui::PopID();
        ImGui::PushID("collections");
        for (const auto& name : draft.entry.bindings.collections) {
            ImGui::PushID(name.c_str());
            Text(name);
            bool custom = selection.visual.target_collections.count(name) != 0;
            if (ImGui::Checkbox(L(UiText::TaskChooseTargets), &custom)) {
                if (custom) {
                    selection.visual.target_collections[name] = {};
                    draft.Changed();
                } else
                    draft.DefaultCollection(name);
            }
            if (custom) {
                Text(T(UiText::TaskEmptyTargets));
                for (const auto& row : snapshot.characters)
                    if (!row.deleting && row.scene == actor->scene) {
                        const auto alias = "@collection:" + name + ":" + std::to_string(row.id);
                        const auto& members = selection.visual.target_collections.at(name);
                        bool selected = std::find(members.begin(), members.end(), alias) != members.end();
                        const auto identity = std::to_string(row.id);
                        ImGui::PushID(identity.c_str());
                        ImGui::BeginDisabled(!row.active || !row.enabled);
                        if (ImGui::Checkbox(ActorName(row.package, rows, language_, row.skin_id,
                                                      snapshot.catalog ? snapshot.catalog->skins.get() : nullptr)
                                                .c_str(),
                                            &selected))
                            draft.CollectionMember(name, row.id, selected);
                        ImGui::EndDisabled();
                        ImGui::PopID();
                    }
            } else
                Text(T(UiText::TaskDefault));
            ImGui::PopID();
        }
        ImGui::PopID();
        ImGui::PushID("choices");
        for (const auto& [name, options] : draft.entry.bindings.choices) {
            field(name);
            const auto choice = selection.visual.choices.find(name);
            if (BeginChoices("##choice", choice == selection.visual.choices.end() ? T(UiText::TaskDefault)
                                                                                  : choice->second.c_str())) {
                for (const auto& option : options)
                    if (ImGui::Selectable(option.c_str(),
                                          choice != selection.visual.choices.end() && choice->second == option)) {
                        selection.visual.choices[name] = option;
                        draft.Changed();
                    }
                ImGui::EndCombo();
            }
            reset(selection.visual.choices, name);
        }
        ImGui::PopID();
        ImGui::PushID("anchors");
        for (const auto& name : draft.entry.bindings.anchors) {
            ImGui::PushID(name.c_str());
            Text(name);
            auto found = selection.visual.anchors.find(name);
            if (found == selection.visual.anchors.end()) {
                if (ImGui::Button(L(UiText::TaskUseActorPose))) {
                    selection.visual.anchors[name] = {actor->placement.position, actor->placement.rotation};
                    draft.Changed();
                }
            } else {
                auto& anchor = found->second;
                float position[] = {anchor.position.x, anchor.position.y, anchor.position.z};
                float rotation[] = {anchor.rotation.x, anchor.rotation.y, anchor.rotation.z, anchor.rotation.w};
                ImGui::SetNextItemWidth(-1);
                Text(T(UiText::WorldPosition));
                if (ImGui::InputFloat3("##position", position)) {
                    anchor.position = {position[0], position[1], position[2]};
                    draft.Changed();
                }
                ImGui::SetNextItemWidth(-1);
                Text(T(UiText::TaskQuaternion));
                if (ImGui::InputFloat4("##rotation", rotation)) {
                    anchor.rotation = {rotation[0], rotation[1], rotation[2], rotation[3]};
                    draft.Changed();
                }
            }
            if (ImGui::SmallButton(L(UiText::TaskDefault))) {
                selection.visual.anchors.erase(name);
                draft.Changed();
            }
            ImGui::PopID();
        }
        ImGui::PopID();
        ImGui::EndChild();
        draft.Query(host);
        ImGui::BeginChild("query-result", {0, 65});
        Text(draft.error);
        if (draft.dirty || draft.pending)
            Text(T(UiText::TaskChecking));
        else if (draft.result)
            for (const auto& reason : draft.result->query.reasons)
                Text(reason.source + ": " + reason.detail);
        ImGui::EndChild();
    }
    ImGui::End();
    task_settings_.Remember(draft.command);
    if (!open)
        task_inputs_.reset();
}
}
