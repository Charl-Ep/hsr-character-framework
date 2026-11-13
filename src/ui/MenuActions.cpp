#include "Menu.hpp"
#include "MenuLabels.hpp"
#include "Widgets.hpp"
#include "CharacterStatus.hpp"
#include <hksr/framework/application/CharacterTasks.hpp>

namespace hksr::framework::ui {
void Menu::Actions(ApplicationService& host, const ApplicationSnapshot& snapshot,
                   const SceneCharacterStatus& selected) {
    const bool npc = PureNpc(selected.package);
    if (action_actor_ != actor_ || action_generation_ != selected.creations) {
        action_actor_ = actor_;
        action_generation_ = selected.creations;
        action_view_ = !npc && selected.handoff && selected.handoff->ready_domain == ActionDomain::Battle
                           ? ActionView::Battle
                           : ActionView::Exploration;
    }
    if (!npc) {
        auto category = [&](UiText label, ActionView view) {
            if (ImGui::RadioButton(L(label), action_view_ == view))
                action_view_ = view;
        };
        category(UiText::Exploration, ActionView::Exploration);
        ImGui::SameLine();
        category(UiText::Battle, ActionView::Battle);
#if HKSR_DEVELOPMENT_UI
        ImGui::SameLine();
        category(UiText::ActionPreviews, ActionView::Preview);
#endif
    }
#if HKSR_DEVELOPMENT_UI
    ImGui::Checkbox(L(UiText::ShowUnavailableTasks), &show_unavailable_tasks_);
#endif
    bool playing = false;
    if (selected.tasks) {
        const auto& control = selected.tasks->control;
        std::set<std::string> errors{CharacterStatus(snapshot, selected, language_).error};
        auto error = [&](const std::string& message) {
            if (!message.empty() && errors.insert(message).second)
                Text(message);
        };
        error(control.status.error);
        if (control.status.input) {
            error(control.status.input->error);
            if (control.status.input->execution)
                for (const auto& issue : control.status.input->execution->issues)
                    error(issue);
        }
        error(control.preparation_error);
        const auto& playback = control.status;
        playing = playback.pending || control.preparing ||
                  (playback.execution && playback.execution->phase != TaskExecutionPhase::Finished);
    }
    if (playing) {
        ImGui::BeginDisabled(!selected.active);
        if (ImGui::Button(L(UiText::FinishTask)))
            host.Post(ControlCharacterTask{actor_, selected.creations, {TaskControlOperation::Finish, {}}});
        ImGui::SameLine();
        if (ImGui::Button(L(UiText::CancelTask)))
            host.Post(ControlCharacterTask{actor_, selected.creations, {TaskControlOperation::Cancel, {}}});
        ImGui::EndDisabled();
    } else
        ImGui::Dummy({0, ImGui::GetFrameHeight()});
    const auto entries = CharacterTasks(snapshot, actor_);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##action-filter", T(UiText::SearchActions), action_filter_, sizeof(action_filter_));
    const auto* current = snapshot.catalog ? snapshot.catalog->Actions(actor_) : nullptr;
    ImGui::BeginChild("action-list", {0, std::max(100.0f, ImGui::GetContentRegionAvail().y - 30)});
    int shown = 0;
    for (const auto& original : entries) {
        const auto entry = task_settings_.Apply(variant_settings_.Apply(original, actor_, selected.creations));
        if (!ActionVisible(entry, npc, action_view_, HKSR_DEVELOPMENT_UI != 0, show_unavailable_tasks_))
            continue;
        auto label = entry.recovery ? std::string(T(UiText::RecoverReaction)) + " · " + entry.label : entry.label;
        if (entry.kind == CharacterTaskKind::Idle)
            label = std::string(T(UiText::IdleState)) + " · " + label;
        auto detail = entry.detail;
        if (entry.kind == CharacterTaskKind::Reaction)
            detail += "\n" + std::string(T(UiText::ReactionScope));
        for (const auto& source : entry.sources)
            detail += "\n" + source.document + " · " + source.field;
        for (const auto& reason : entry.query.reasons)
            detail += "\n" + reason.source + ": " + reason.detail;
        if (!SourceMatches(label + " " + detail, action_filter_))
            continue;
        ++shown;
        ImGui::PushID(ActionRowIdentity(entry).c_str());
        const bool settings = HasTaskVariants(entry) ||
                              (HasEditableTaskInputs(entry, HKSR_DEVELOPMENT_UI != 0 && show_technical_inputs_) &&
                               std::holds_alternative<ControlCharacterTask>(entry.command));
        const float settings_width = settings ? ImGui::GetTextLineHeight() + 12 : 0;
        const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x -
                                               (settings ? settings_width + ImGui::GetStyle().ItemSpacing.x : 0));
        ImGui::BeginDisabled(!ActionCanRequest(entry));
        if (ImGui::Selectable(label.c_str(), false, 0, {width, ImGui::GetTextLineHeight() + 12}))
            host.Post(entry.command);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) &&
            (!detail.empty() || ImGui::CalcTextSize(label.c_str()).x > width)) {
            BeginHelp();
            Text(label);
            Text(detail);
            EndHelp();
        }
        ImGui::EndDisabled();
        if (settings) {
            ImGui::SameLine();
            if (GearButton("##settings", {settings_width, settings_width}, language_))
                OpenTaskInputs(entry);
        }
        ImGui::PopID();
    }
    if (!shown)
        ImGui::TextWrapped("%s", T(UiText::NoActionMatches));
    ImGui::EndChild();
    if (current && !current->actions.finished)
        ImGui::TextDisabled("%s", T(UiText::ReadingActions));
    if (variant_inputs_)
        VariantInputs(snapshot);
}
}
