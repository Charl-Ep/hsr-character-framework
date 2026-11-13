#include <hksr/framework/character/CharacterSession.hpp>
#include <hksr/framework/presentation/scene/VisualProgramLibrary.hpp>
#include <stdexcept>

namespace hksr::framework {
CharacterPlan CharacterPlan::FromPrepared(const PreparedSource& source, std::shared_ptr<const ModelAssetInput> payload,
                                          std::string look_source) {
    const auto& summary = source.summary;
    if (!summary.finished || !summary.character)
        throw std::invalid_argument("character source preparation has not produced a selection");
    CharacterPlan out;
    out.package = *summary.character;
    out.payload = std::move(payload);
    out.skin = source.skin;
    out.npc_look_source = std::move(look_source);
    out.alternate = summary.presentation;
    if (out.package.appearance && !out.package.appearance->character_config.empty())
        out.action_sources.push_back(
            {ActionDomain::Exploration, {api::ActionConfigKind::Adventure, out.package.appearance->character_config}});
    if (out.alternate && summary.battle.status == SourceResolution::Ready && summary.battle.candidates.size() == 1)
        out.action_sources.push_back(
            {ActionDomain::Battle,
             {api::ActionConfigKind::Battle, summary.battle.candidates.front().character_config}});
    return out;
}
CharacterSession::CharacterSession(api::GameWorld* world, CharacterPlan plan, CharacterSessionServices services,
                                    CharacterRestoreState restore, VisualProgramBindings bindings)
    : world_(world), plan_(std::move(plan)), services_(std::move(services)), program_bindings_(std::move(bindings)),
      playback_rate_(restore.playback_rate), restore_(std::move(restore)),
      character_(world, plan_.package, plan_.payload, plan_.npc_look_source, services_.character, services_.events,
                 plan_.visual_root, plan_.skin) {
    if (playback_rate_ != 1 && playback_rate_ != 2)
        throw std::invalid_argument("playback rate must be 1 or 2");
    // Capture factory-created model/consumer inputs before private Art or
    // supplemental events mutate them. Without an alternate source no capture
    // is needed and none of its APIs become exploration prerequisites.
    if (plan_.alternate)
        character_.SetExplorationInputsEnabled(false);
}
void CharacterSession::Create(const api::NpcCreateInput& input) {
    if (status_.phase != SessionPhase::NotCreated || removing_)
        throw std::logic_error("session creation epoch is single-use");
    character_.Create(input);
    status_.phase = SessionPhase::PreparingBaseline;
    for (const auto& receipt : character_.Creation().entities)
        if (receipt.token.entity == character_.Creation().factory.entity && !receipt.disposed)
            owner_ = receipt.token;
    if (!character_.Creation().create_returned || !owner_.entity || character_.Creation().removal_requested) {
        status_.phase = SessionPhase::Failed;
        status_.errors.push_back("NPC creation did not produce a live owned result");
        const auto& errors = character_.Creation().errors;
        status_.errors.insert(status_.errors.end(), errors.begin(), errors.end());
        Remove();
    }
}
std::uint64_t CharacterSession::Queue(ActionDomain domain, ActionRequest action) {
    if (removing_ || status_.closed)
        throw std::logic_error("session closing");
    desired_ = Intent{++status_.selected_action, domain, std::move(action)};
    return status_.selected_action;
}
std::optional<ActionExecutionStatus> CharacterSession::ActionStatus() const {
    if (!desired_)
        return {};
    ActionExecutionStatus result;
    result.request = desired_->serial;
    if (removing_ || status_.closed || Ended()) {
        result.phase = ActionIntentPhase::Cancelled;
        result.outcome = Ended() ? ActionOutcome::OwnerEnded : ActionOutcome::Cancelled;
        result.detail = "action session is closing";
        return result;
    }
    result.detail = "waiting for session preparation";
    if (status_.submitted_action != desired_->serial)
        return result;
    if (!status_.dispatch_error.empty()) {
        result.phase = ActionIntentPhase::Failed;
        result.detail = status_.dispatch_error;
    } else if (handoff_ && status_.handoff_request) {
        result = handoff_->ActionStatus(status_.handoff_request);
        result.request = desired_->serial;
    }
    return result;
}
void CharacterSession::Create(const api::StoryCreateInput& input) {
    if (status_.phase != SessionPhase::NotCreated || removing_)
        throw std::logic_error("session creation epoch is single-use");
    character_.Create(input);
    status_.phase = SessionPhase::PreparingBaseline;
    for (const auto& receipt : character_.Creation().entities)
        if (receipt.token.entity == character_.Creation().factory.entity && !receipt.disposed)
            owner_ = receipt.token;
    if (!character_.Creation().create_returned || !owner_.entity || character_.Creation().removal_requested) {
        status_.phase = SessionPhase::Failed;
        status_.errors.push_back("independent actor creation did not produce a live owned result");
        const auto& errors = character_.Creation().errors;
        status_.errors.insert(status_.errors.end(), errors.begin(), errors.end());
        Remove();
    }
}
void CharacterSession::Remove() {
    removing_ = true;
    desired_.reset();
    if (baseline_)
        baseline_->Cancel();
}
void CharacterSession::PreparePlayback() {
    if (playback_prepared_)
        return;
    const auto requested = playback_rate_;
    playback_rate_ = 1;
    try {
        SetAudioRate(1);
    } catch (const std::exception& error) {
        status_.playback_error = error.what();
    }
    playback_prepared_ = true;
    if (requested == 2 && status_.playback_error.empty())
        try {
            SetPlaybackRate(2);
        } catch (const std::exception&) {
        } // the independent failure is retained in playback_error
}
void CharacterSession::SetAudioRate(unsigned rate) {
    if (!services_.audio)
        return;
    if (!audio_registered_) {
        services_.audio(owner_, world_, rate);
        audio_registered_ = true;
    } else if (services_.audio_rate)
        services_.audio_rate(owner_, world_, rate);
}
void CharacterSession::SetPlaybackRate(unsigned rate) {
    if (rate != 1 && rate != 2)
        throw std::invalid_argument("playback rate must be 1 or 2");
    if (removing_ || status_.closed)
        throw std::logic_error("session closing");
    if (!playback_prepared_) {
        playback_rate_ = rate;
        return;
    }
    try {
        if (rate == 1) {
            if (playback_scale_.value && !timing_->Remove(playback_scale_))
                throw std::runtime_error("actor playback scale has not been removed");
            playback_scale_ = {};
            playback_rate_ = 1;
            if (handoff_)
                handoff_->SetPlaybackRate(1);
            try {
                SetAudioRate(1);
                status_.playback_error.clear();
            } catch (const std::exception& error) {
                if (audio_registered_ && services_.clear_audio)
                    services_.clear_audio(owner_, world_);
                audio_registered_ = false;
                status_.playback_error = error.what();
            }
            return; // an audio failure cannot prevent withdrawing this actor's clock
        }
        if (playback_rate_ == 2)
            return;
        SetAudioRate(playback_rate_); // retry a previous audio registration failure
        if (!timing_ && services_.timing)
            timing_ = services_.timing(owner_, world_);
        if (!timing_)
            throw std::runtime_error("actor playback timing unavailable");
        SetAudioRate(2);
        api::ActorTimeScaleToken next;
        try {
            api::ActorTimeScaleInput input;
            input.factor = 2;
            next = timing_->Scale(input, "hksr.actor.playback");
            if (!next.value)
                throw std::runtime_error("actor playback scale was not installed");
        } catch (...) {
            try {
                SetAudioRate(playback_rate_);
            } catch (...) {
                if (audio_registered_ && services_.clear_audio)
                    services_.clear_audio(owner_, world_);
                audio_registered_ = false;
            }
            throw;
        }
        playback_scale_ = next;
        playback_rate_ = 2;
        if (handoff_)
            handoff_->SetPlaybackRate(2);
        status_.playback_error.clear();
    } catch (const std::exception& error) {
        status_.playback_error = error.what();
        throw;
    }
}
bool CharacterSession::ClosePlayback() {
    if (audio_registered_ && services_.clear_audio) {
        services_.clear_audio(owner_, world_);
        audio_registered_ = false;
    }
    if (timing_ && !timing_->Close())
        return false;
    timing_.reset();
    playback_scale_ = {};
    return true;
}
TaskControlResult CharacterSession::ControlTask(TaskControlInput input) {
    if (removing_ || status_.closed || !handoff_)
        throw std::logic_error("task session is not active");
    bool select = input.operation == TaskControlOperation::Select;
    if (select && input.selection && plan_.visual_program && plan_.visual_program->tasks) {
        const auto query = handoff_->QueryTasks(input.selection);
        const auto& definition = plan_.visual_program->tasks->Resolve(query.selection->resolved).definition;
        if (definition.mode == TaskExecutionMode::Input)
            select = false;
    }
    const bool cancel = input.operation == TaskControlOperation::Cancel;
    auto result = handoff_->ControlTask(std::move(input));
    if (select && result.selection && result.selection->intent) {
        desired_.reset();
        status_.dispatch_error.clear();
    }
    if (cancel)
        status_.dispatch_error.clear();
    return result;
}
bool CharacterSession::Ended() const {
    return owner_.entity && (api::TrackedEntities().IsDisposed(owner_) ||
                             api::TrackedEntities().Current(owner_.entity).generation != owner_.generation);
}
bool CharacterSession::CloseBaseline() {
    return !baseline_ || baseline_->Close();
}
void CharacterSession::StartHandoff(std::unique_ptr<api::ModelProfileRequest> request) {
    status_.alternate_available = static_cast<bool>(request);
    handoff_ = std::make_unique<ModelActionHandoff>(
        character_, owner_, world_, std::move(request), services_.handoff, DomainControllers{},
        plan_.alternate && plan_.alternate->initial_state.available ? plan_.alternate->initial_state.value
                                                                    : std::string{},
        plan_.package.avatar_id, plan_.alternate ? plan_.alternate->model.character_config : std::string{});
    handoff_->ConfigurePresentation(plan_.presentation_profile, restore_ ? restore_->presentation_phase : std::string{});
    handoff_->SetPlaybackRate(playback_rate_);
    if (services_.hit_inputs)
        for (const auto& root : plan_.action_sources)
            if (root.domain == ActionDomain::Battle && root.source.kind == api::ActionConfigKind::Battle) {
                handoff_->ConfigureReceiverPolicy(
                    [this, source = root.source.path](bool visual) { return services_.hit_inputs(source, visual); });
                break;
            }
    if (plan_.visual_program) {
        try {
            if (!services_.visual_program)
                throw std::logic_error("visual program runtime was not prepared");
            auto bindings = program_bindings_;
            bindings.skin = plan_.skin;
            auto player = services_.visual_program({owner_, world_}, *plan_.visual_program, bindings,
                                                    plan_.presentation_profile);
            if (!player)
                throw std::runtime_error("visual program factory returned no player");
            if (plan_.visual_program->tasks)
                handoff_->ConfigureTasks(plan_.visual_program->tasks, std::move(player),
                                         [this](const auto& selection) -> std::optional<PoseCommand> {
                                             if (!services_.pose)
                                                 throw std::runtime_error("pose sources unavailable");
                                             return services_.pose(plan_.action_sources, selection);
                                         });
            else
                handoff_->ConfigureVisualProgram(std::move(player));
        } catch (const std::exception& e) {
            // This is not a baseline failure. Do not reconstruct a second
            // handoff or silently play without the explicitly requested scene.
            status_.errors.push_back(e.what());
            status_.phase = SessionPhase::Failed;
            Remove();
            return;
        }
    }
    try {
        if (restore_ && restore_->visual_state)
            handoff_->RestoreVisualProgram(*restore_->visual_state);
        if (restore_ && restore_->manual_reaction)
            handoff_->RestoreManualReaction(*restore_->manual_reaction);
        if (restore_ && (restore_->task_selection ||
                         (!desired_ && restore_->visual_state && plan_.visual_program && plan_.visual_program->tasks)))
            handoff_->RestoreTasks(restore_->task_selection);
    } catch (const std::exception& error) {
        // A missing optional model prevents this saved task, not use of the
        // actor's independent exploration model or its ordinary controls.
        status_.dispatch_error = error.what();
        status_.errors.push_back(error.what());
    }
    restore_.reset();
    character_.SetExplorationInputsEnabled(true);
    status_.phase = SessionPhase::Active;
    status_.waiting.clear();
}
bool CharacterSession::PrepareBaseline(const api::WorldFrame& frame, AppearanceServices appearance,
                                       api::AnimationInputTypes types) {
    try {
        if (!character_.PrepareSkinBaseline(frame, appearance)) {
            status_.waiting = "preparing skin animation events";
            return false;
        }
        if (!plan_.alternate) {
            StartHandoff(nullptr);
            return true;
        }
        if (!baseline_)
            baseline_ = services_.baseline(owner_, world_, plan_.alternate->model);
        baseline_->Service(frame, appearance.assets.objects, appearance.assets.arrays, types);
        const auto& result = baseline_->Status();
        status_.baseline_attempted = result.attempted;
        status_.waiting = result.waiting;
        if (removing_ || Ended())
            return false;
        if (result.phase == api::BaselineCapturePhase::Ready)
            StartHandoff(baseline_->TakeRequest());
        else if (result.phase == api::BaselineCapturePhase::Failed) {
            status_.errors.insert(status_.errors.end(), result.errors.begin(), result.errors.end());
            // Product choice: a failed optional alternate must not remove the
            // independently usable exploration instance.
            StartHandoff(nullptr);
        }
    } catch (const std::exception& e) {
        status_.baseline_attempted = true;
        status_.errors.push_back(e.what());
        if (plan_.skin && !character_.Status().event_preparation_attempted) {
            status_.phase = SessionPhase::Failed;
            Remove();
        } else if (!removing_ && !Ended())
            StartHandoff(nullptr);
    }
    return static_cast<bool>(handoff_);
}
void CharacterSession::Service(const api::WorldFrame& frame, AppearanceServices appearance,
                               api::AnimationInputTypes types) {
    if (busy_ || status_.closed || (status_.phase == SessionPhase::NotCreated && !removing_))
        return;
    busy_ = true;
    struct Exit {
        bool& flag;
        ~Exit() {
            flag = false;
        }
    } exit{busy_};
    if (Ended() || character_.Creation().removal_requested || character_.Creation().closed)
        removing_ = true;
    if (removing_) {
        status_.phase = SessionPhase::Closing;
        if (!ClosePlayback())
            return; // release this session's clock/audio before its actor
        if (handoff_) {
            handoff_->Remove();
            handoff_->Service(frame, appearance, types);
        } else if (character_.Creation().create_started) {
            character_.Remove();
            character_.Service(frame, appearance, types);
        }
        status_.closed = CloseBaseline() && (!character_.Creation().create_started || character_.Status().closed) &&
                         (!handoff_ || handoff_->Status().closed);
        if (status_.closed)
            status_.phase = SessionPhase::Closed;
        return;
    }
    if (!handoff_) {
        if (frame.world != world_ || !frame.disposing.read.returned || frame.disposing.value)
            return;
        try {
            PreparePlayback();
        } catch (const std::exception& error) {
            status_.errors.push_back(error.what());
            status_.phase = SessionPhase::Failed;
            Remove();
            return;
        }
        if (!PrepareBaseline(frame, appearance, types))
            return;
    }
    CloseBaseline();
    if (removing_ || Ended())
        return;
    if (desired_ && desired_->serial != status_.submitted_action) {
        const auto intent = *desired_;
        status_.submitted_action = intent.serial; // failed selection is explicit, not retried every frame
        status_.handoff_request = 0;
        status_.dispatch_error.clear();
        try {
            status_.handoff_request = handoff_->Queue(intent.domain, intent.action);
        } catch (const std::exception& error) {
            status_.dispatch_error = error.what();
            status_.errors.push_back(error.what());
        }
    }
    handoff_->Service(frame, appearance, types);
}
}
