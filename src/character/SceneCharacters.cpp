#include <hksr/framework/character/SceneCharacters.hpp>
#include <hksr/framework/presentation/VisualProgramRuntime.hpp>
#include <stdexcept>
#include <cmath>
#include <chrono>

namespace hksr::framework {
void SceneCharacters::ConfigurePrograms(VisualProgramLibrary programs) {
    PublishTaskCatalogs(std::move(programs.catalogs));programs_.sources=std::move(programs.sources);
}
std::uint64_t SceneCharacters::PublishTaskCatalogs(TaskCatalogInputs catalogs) {
    if(closing_)throw std::logic_error("character registry is closing");
    const auto revision=task_catalog_revision_+1;
    TaskCatalogInputs published;
    for(const auto& [name,input]:catalogs) {
        if(!input || !input->tasks || name!=input->tasks->id || input->program!=input->tasks->visual)
            throw std::invalid_argument("invalid task catalog publication: "+name);
        auto copy=std::make_shared<VisualProgramInput>(*input);copy->catalog_revision=revision;
        published.emplace(name,std::move(copy));
    }
    programs_.catalogs=std::move(published);task_catalog_revision_=revision;return revision;
}
void SceneCharacters::BindProfile(CharacterPlan& plan,std::uint64_t id) const {
    if(!plan.visual_program)for(const auto& root:plan.action_sources) {
        const auto found=programs_.sources.find(root.source.path);
        if(found!=programs_.sources.end() && (found->second->tasks?found->second->tasks->domain==root.domain:
            root.domain==ActionDomain::Alternate && root.source.kind==api::ActionConfigKind::Battle)) {
            plan.visual_program=found->second;break;
        }
    }
    if(plan.visual_program) {
        auto input=std::make_shared<VisualProgramInput>(*plan.visual_program);
        input->character=id;
        const bool saved=plan.persistence==CharacterPersistence::Layout;
        input->resolve_character=[this,saved](std::uint64_t target){
            const auto found=entries_.find(target);
            if(saved && found!=entries_.end() && found->second.plan.persistence==CharacterPersistence::Session)
                throw std::invalid_argument("a saved visual context cannot retain a session-only target");
            return VisualTarget(target);
        };
        input->restore_target=[this,id](std::uint64_t target) {
            const auto found=entries_.find(target),owner=entries_.find(id);
            if(found==entries_.end() || owner==entries_.end() || found->second.status.deleting || !found->second.status.enabled ||
                found->second.status.scene!=owner->second.status.scene)
                return VisualStepResult{VisualStepState::Failed,"saved target is unavailable: "+std::to_string(target)};
            const auto& entry=found->second;
            if(entry.failed_revision==entry.revision)
                return VisualStepResult{VisualStepState::Failed,"saved target creation failed: "+entry.creation_error};
            return VisualStepResult{VisualStepState::Done,{}}; // native readiness is checked by the existing resolver
        };
        plan.visual_program=std::move(input);
    }
    if(profiles_)for(const auto& root:plan.action_sources)if(root.domain==ActionDomain::Alternate && root.source.kind==api::ActionConfigKind::Battle) {
        const auto found=profiles_->sources.find(root.source.path);
        if(found!=profiles_->sources.end()){plan.presentation_profile=found->second;break;}
    }
    PresentationState state;state.Configure(plan.presentation_profile,plan.presentation_phase);
    plan.presentation_phase=state.Phase();if(!state.Note().empty())plan.source_notes.push_back(state.Note());
}
void SceneCharacters::ConfigureTaskPreview(CharacterPlan& plan,const std::string& catalog) const {
    ConfigureTaskCatalog(plan,catalog);plan.persistence=CharacterPersistence::Session;
}
void SceneCharacters::ConfigureTaskCatalog(CharacterPlan& plan,const std::string& catalog) const {
    const auto found=programs_.catalogs.find(catalog);
    if(found==programs_.catalogs.end() || !found->second || !found->second->tasks)
        throw std::invalid_argument("task catalog not registered: "+catalog);
    const auto& input=found->second;
    bool matches=false;
    for(const auto& root:plan.action_sources)
        if(root.domain==input->tasks->domain && root.source.path==input->program->source)matches=true;
    if(!matches)throw std::invalid_argument("task catalog source/domain does not match the prepared character: "+catalog);
    plan.visual_program=input;plan.visual_state.reset();plan.task_selection.reset();
}
api::EntityOrigin SceneCharacters::VisualTarget(std::uint64_t id) const {
    const auto found=entries_.find(id);
    if(closing_ || found==entries_.end())return {};
    const auto& entry=found->second;
    if(!entry.status.enabled || entry.status.deleting || !entry.active || entry.active->revision!=entry.revision)return {};
    const auto& creation=entry.active->session->Character().Creation();
    if(creation.removal_requested || creation.closed)return {};
    for(const auto& entity:creation.entities)if(entity.token.entity==creation.factory.entity && !entity.disposed &&
        !api::TrackedEntities().IsDisposed(entity.token) && api::TrackedEntities().Current(entity.token.entity).generation==entity.token.generation)
        return {entity.token,entry.active->world};
    return {};
}
SceneCharacters::SceneCharacters(CharacterSessionServices services)
    :services_(std::move(services)) {
    create_=[this](CharacterSession& session,const api::WorldFrame& frame,const SceneCreation& placement) {
        const auto& package=session.Plan().package;
        if(package.construction==ConstructionKind::StoryActor) {
            if(!package.appearance)throw std::invalid_argument("independent actor model source absent");
            session.Create(api::StoryCreateInput{frame.world,frame.manager.value,"hksr.character."+std::to_string(placement.id),
                package.appearance->template_prefab,package.appearance->character_config});
            return;
        }
        // Store the owner before construction can return a partial result.
        // Prepare itself returns that result with all known GC roots.
        creation_inputs_.push_back({placement.id,{}});
        auto& input=creation_inputs_.back().value;
        input=services_.character.prepare(session.Plan().package.npc_id,
            ("hksr.character."+std::to_string(placement.id)).c_str(),placement.placement.position,placement.placement.rotation);
        if(!input->Status().ready) throw std::runtime_error(input->Status().error.empty()?"private NPC inputs not ready":input->Status().error);
        session.Create({frame.world,frame.manager.value,input->Definition(),input->Row(),true});
        // api::EntityCreationSession retains its own roots before calling the factory. These
        // construction roots can now drain independently of actor lifetime.
    };
}
std::uint64_t SceneCharacters::Add(std::string scene,CharacterPlan plan,std::optional<PlacedAction> action,CharacterPlacement placement) {
    if(closing_ || scene.empty()) throw std::invalid_argument("scene character needs a scene and an open registry");
    BindProfile(plan,next_+1);
    Entry entry; entry.plan=std::move(plan); entry.action=std::move(action);
    if(entry.plan.task_selection) {
        if(entry.action)throw std::invalid_argument("a character cannot restore an action and a task simultaneously");
        entry.intent_owner=Entry::IntentOwner::Task;
    }
    if(entry.action)entry.action_intent=++entry.next_action;
    entry.placement=placement;
    entry.status.id=++next_; entry.status.scene=std::move(scene);
    entries_.emplace(next_,std::move(entry)); return next_;
}
std::uint64_t SceneCharacters::Select(std::uint64_t id,PlacedAction action) {
    auto& entry=Edit(id);
    if(const auto* selected=std::get_if<ProgramAction>(&action.request)) {
        const auto input=entry.plan.visual_program;
        if(action.domain!=ActionDomain::Alternate || !input || !input->program || input->program->id!=selected->program ||
            !input->program->actions.count(selected->action))throw std::invalid_argument("visual program action absent from this character");
    }
    entry.action=std::move(action);entry.action_intent=++entry.next_action;
    entry.intent_owner=Entry::IntentOwner::Action;
    entry.plan.task_selection.reset();
    entry.pending_task.reset();++entry.task_revision;entry.task_preparation_error.clear();
    entry.action_error.clear();entry.visual_restore_error.clear();SubmitAction(entry);
    return entry.action_intent;
}
TaskControlResult SceneCharacters::ControlTask(std::uint64_t id,std::uint64_t creation,TaskControlInput input,const api::WorldFrame& frame) {
    auto& entry=Edit(id);
    if(!frame.battle.read.returned || frame.battle.value)
        throw std::logic_error("task previews require an exploration world");
    if(!creation || entry.status.creations!=creation || !Active(id,frame))
        throw std::logic_error("task command targets an inactive or replaced creation");
    if(input.operation==TaskControlOperation::Select) {
        if(!input.selection)throw std::invalid_argument("task selection missing");
        ValidateTaskSelection(*input.selection);
        if(entry.plan.persistence==CharacterPersistence::Layout)for(const auto& [name,target]:input.selection->visual.targets) {
            if(!target)continue;
            const auto found=entries_.find(target);
            if(found==entries_.end() || found->second.status.deleting || found->second.status.scene!=entry.status.scene ||
                found->second.plan.persistence!=CharacterPersistence::Layout)
                throw std::invalid_argument("saved task target must belong to this map layout: "+name);
        }
        auto queried=entry.active->session->ControlTask({TaskControlOperation::Query,input.selection});
        if(queried.selection->query.availability!=TaskAvailability::Available)return queried;
        const auto& definition=entry.plan.visual_program->tasks->Resolve(queried.selection->resolved).definition;
        if(definition.control && !definition.control->presentation)
            return entry.active->session->ControlTask(std::move(input));
        entry.pending_task=std::move(input.selection);entry.pending_task_finish=false;
        if(entry.plan.persistence==CharacterPersistence::Layout){entry.plan.task_selection=entry.pending_task;entry.action.reset();}
        ++entry.task_revision;entry.task_preparation_error.clear();
        entry.intent_owner=Entry::IntentOwner::Task;entry.active->session->SupersedeQueuedAction();
        if(auto submitted=SubmitTask(entry))return *submitted;
        queried.preparing=entry.pending_task?std::optional<TaskKey>(entry.pending_task->task):std::nullopt;
        queried.preparation_error=entry.task_preparation_error;return queried;
    }
    if(input.operation==TaskControlOperation::Finish && entry.pending_task) {
        entry.pending_task_finish=true;input.operation=TaskControlOperation::Query;
    }
    if(input.operation==TaskControlOperation::Cancel){entry.pending_task.reset();entry.plan.task_selection.reset();++entry.task_revision;entry.task_preparation_error.clear();}
    auto result=entry.active->session->ControlTask(std::move(input));
    if(entry.pending_task)result.preparing=entry.pending_task->task;
    if(!entry.task_preparation_error.empty())result.preparation_error=entry.task_preparation_error;
    if(result.preparation_error.empty() && entry.intent_owner==Entry::IntentOwner::Task)
        result.preparation_error=entry.active->session->Status().dispatch_error;
    return result;
}
bool SceneCharacters::PrepareDomain(Entry& entry,ActionDomain domain) {
    if(!entry.active)return false;
    if(!entry.active->controls)return true;
    const auto* handoff=entry.active->session->Handoff();
    if((handoff && handoff->Status().ready_domain!=domain) ||
        entry.active->controls->Status().gaze_phase==api::GazePhase::Restoring) {
        const auto phase=entry.active->controls->Suspend();
        if(phase==api::ControlPhase::Waiting || phase==api::ControlPhase::Running)return false;
        if(phase!=api::ControlPhase::Returned)throw std::runtime_error("look target restoration failed before model handoff");
    }
    return true;
}
std::optional<TaskControlResult> SceneCharacters::SubmitTask(Entry& entry) {
    if(!entry.pending_task || !entry.active || closing_ || !entry.status.enabled ||
        entry.active->revision!=entry.revision)return {};
    const auto serial=entry.task_revision,creation=entry.status.creations;
    const auto selection=*entry.pending_task;
    try {
        if(!PrepareDomain(entry,entry.plan.visual_program->tasks->domain))return {};
        if(serial!=entry.task_revision || creation!=entry.status.creations || !entry.active ||
            closing_ || !entry.status.enabled || entry.active->revision!=entry.revision)return {};
        auto result=entry.active->session->ControlTask({TaskControlOperation::Select,selection});
        if(serial==entry.task_revision) {
            entry.pending_task.reset();
            if(result.selection && result.selection->intent) {
                entry.active->action_intent=entry.action_intent;
                if(entry.pending_task_finish)entry.active->session->ControlTask({TaskControlOperation::Finish,{}});
            } else if(result.selection) {
                entry.task_preparation_error="task conditions changed while preparing";
                for(const auto& reason:result.selection->query.reasons)entry.task_preparation_error+="; "+reason.source+": "+reason.detail;
            }
        }
        return result;
    } catch(const std::exception& error) {
        if(serial==entry.task_revision){entry.task_preparation_error=error.what();entry.pending_task.reset();}
        return {};
    }
}
void SceneCharacters::SubmitAction(Entry& entry) {
    if(entry.intent_owner==Entry::IntentOwner::Task || !entry.action || !entry.active || closing_ || !entry.status.enabled ||
        entry.active->revision!=entry.revision || entry.active->action_intent==entry.action_intent || !entry.action_error.empty())return;
    const auto intent=entry.action_intent;
    const auto creation=entry.status.creations;
    try {
        if(!PrepareDomain(entry,entry.action->domain))return;
        if(entry.action_intent!=intent || entry.status.creations!=creation || !entry.active ||
            closing_ || !entry.status.enabled || entry.active->revision!=entry.revision)return;
        entry.active->session_action=entry.active->session->Queue(entry.action->domain,entry.action->request);
        entry.active->action_intent=entry.action_intent;
    } catch(const std::exception& error) {if(entry.action_intent==intent)entry.action_error=error.what();}
}
std::shared_ptr<const ControlSubmission> SceneCharacters::Place(std::uint64_t id,CharacterPlacement pose,bool relative) {
    auto& entry=Edit(id);
    if(!entry.active || !entry.active->controls || !entry.status.enabled || entry.active->revision!=entry.revision)
        throw std::runtime_error("position controls require an active instance");
    auto submission=std::make_shared<ControlSubmission>();
    submission->Bind(entry.status.creations,entry.active->controls->Place(pose.position,pose.rotation,relative,pose.snap_to_ground));
    if(pose.snap_to_ground)entry.placement.snap_to_ground=pose.snap_to_ground;
    return submission;
}
std::shared_ptr<const ControlSubmission> SceneCharacters::Look(std::uint64_t id,LookIntent intent) {
    if(closing_)throw std::logic_error("scene registry closing");
    if(!std::isfinite(intent.max_distance) || intent.max_distance<0)throw std::invalid_argument("gaze distance must be finite and nonnegative");
    if(intent.mode==LookMode::Instance && intent.target==id)throw std::invalid_argument("cannot look at own head");
    if(intent.mode==LookMode::Point && (!std::isfinite(intent.point.x) || !std::isfinite(intent.point.y) || !std::isfinite(intent.point.z)))
        throw std::invalid_argument("look point must be finite");
    auto& entry=Edit(id);
    if(intent.mode==LookMode::Instance && entry.plan.persistence==CharacterPersistence::Layout &&
        entries_.at(intent.target).plan.persistence==CharacterPersistence::Session)
        throw std::invalid_argument("a saved character cannot retain a session-only gaze target");
    auto submission=std::make_shared<ControlSubmission>();
    const bool in_session=entry.active && entry.active->controls && entry.active->revision==entry.revision;
    if(in_session)
        submission->Bind(entry.status.creations,entry.active->controls->Look(intent));
    if(entry.look_submission)entry.look_submission->Supersede(in_session);
    entry.look=intent;entry.look_submission=submission;
    if(!entry.status.enabled)submission->Complete(api::ControlPhase::Cancelled,"gaze intent saved for disabled instance");
    return submission;
}
void SceneCharacters::SetEnabled(std::uint64_t id,bool enabled) {
    auto& entry=Edit(id);
    if(closing_ && enabled) throw std::logic_error("scene registry closing");
    if(entry.status.enabled!=enabled) {
        if(!enabled)SaveVisualState(entry);
        entry.status.enabled=enabled; ++entry.revision;
    }
    if(!enabled && entry.look_submission)entry.look_submission->Complete(api::ControlPhase::Cancelled,"instance disabled");
}
void SceneCharacters::Rebuild(std::uint64_t id) {
    if(closing_) throw std::logic_error("scene registry closing");
    auto& entry=Edit(id);SaveVisualState(entry);entry.status.enabled=true; ++entry.revision;
}
void SceneCharacters::ReplacePlan(std::uint64_t id,CharacterPlan plan) {
    if(closing_) throw std::logic_error("scene registry closing");
    auto& entry=Edit(id);
    plan.persistence=entry.plan.persistence;
    entry.plan=std::move(plan);entry.action.reset();entry.action_intent=0;entry.action_error.clear();entry.visual_restore_error.clear();++entry.revision;
    entry.pending_task.reset();++entry.task_revision;entry.task_preparation_error.clear();
    entry.intent_owner=Entry::IntentOwner::Action;
    BindProfile(entry.plan,id);
    entry.status.error.clear();
}
SceneCharacters::Entry& SceneCharacters::Edit(std::uint64_t id) {
    auto& entry=entries_.at(id);
    if(entry.status.deleting)throw std::logic_error("instance is being deleted");
    return entry;
}
void SceneCharacters::Delete(std::uint64_t id) {
    auto& entry=entries_.at(id);
    if(entry.status.deleting)return;
    entry.status.deleting=true;entry.status.enabled=false;++entry.revision;
    if(entry.look_submission)entry.look_submission->Complete(api::ControlPhase::Cancelled,"instance deleted");
    for(auto& item:entries_)if(!item.second.status.deleting &&
        item.second.look.mode==LookMode::Instance && item.second.look.target==id) {
        LookIntent disabled;disabled.mode=LookMode::Disabled;Look(item.first,disabled);
    }
}
void SceneCharacters::Shutdown() {
    closing_=true;for(auto& item:entries_) {
        item.second.status.enabled=false;
        if(item.second.look_submission)item.second.look_submission->Complete(api::ControlPhase::Cancelled,"host shutdown");
    }
}
void SceneCharacters::Retire(Entry& entry) {
    if(!entry.active) return;
    if(!entry.status.deleting && entry.active->revision==entry.revision)SaveVisualState(entry);
    entry.pending_task.reset();++entry.task_revision;
    entry.intent_owner=entry.plan.task_selection?Entry::IntentOwner::Task:Entry::IntentOwner::Action;
    if(entry.active->controls) {
        // Preserve returned gaze receipts, but keep an unapplied saved target
        // waiting across automatic map retirement rather than cancelling it.
        if(entry.look_submission)entry.look_submission->AwaitCreation();
        api::PlayerPlacement saved;saved.ready=true;saved.position=entry.placement.position;saved.rotation=entry.placement.rotation;
        const auto pose=entry.active->controls->PlannedPlacement(saved);
        if(pose.ready){entry.placement.position=pose.position;entry.placement.rotation=pose.rotation;}
        entry.active->controls->Cancel();
    }
    entry.active->session->Remove();
    entry.retired.push_back(std::move(*entry.active)); entry.active.reset();
}
bool SceneCharacters::LiveEntities(const Epoch& epoch) {
    const auto& state=epoch.session->Character().Creation();
    if(state.creation.incomplete) return true; // unknown footprint is an actual ownership uncertainty
    for(const auto& entity:state.entities)
        if(!entity.disposed && !api::TrackedEntities().IsDisposed(entity.token)) return true;
    return false;
}
void SceneCharacters::Service(const api::WorldFrame& frame,const std::optional<SceneAddress>& scene,
    AppearanceServices appearance,api::AnimationInputTypes types) {
    if(servicing_) return;
    servicing_=true; struct Exit { bool& flag; ~Exit(){flag=false;} } exit{servicing_};
    for(auto it=creation_inputs_.begin();it!=creation_inputs_.end();) {
        if(!it->value || it->value->Release()) it=creation_inputs_.erase(it); else ++it;
    }
    const auto ids=Ids(); // synchronous callbacks can Add, but not extend this delivery's batch
    const bool address_available=scene && !scene->key.empty() && scene->incarnation;
    const bool can_create=!closing_ && address_available && frame.world &&
        frame.manager.read.returned && frame.manager.value && frame.disposing.read.returned && !frame.disposing.value &&
        frame.battle.read.returned && !frame.battle.value;
    bool started_creation=false;
    std::chrono::steady_clock::time_point creation_start;
    for(auto id:ids) {
        auto& entry=entries_.at(id);
        const auto* pending_handoff=entry.active?entry.active->session->Handoff():nullptr;
        const bool pending=!entry.active ||
            entry.active->revision!=entry.revision || entry.active->action_intent!=entry.action_intent ||
            entry.active->session->Status().selected_action!=entry.active->session->Status().submitted_action ||
            !pending_handoff || pending_handoff->Status().phase!=HandoffPhase::Ready;
        const api::OperationTrace::Sink disabled;
        auto advance=[&](Epoch& epoch) {
            api::OperationSpan span("character.advance");
            try { epoch.session->Service(frame,appearance,types); }
            catch(const std::exception& error) { entry.status.error=error.what(); }
            catch(...) { entry.status.error="unknown character service failure"; }
        };
        const bool here=address_available && scene->key==entry.status.scene;
        // A missing scene observation is not proof of leaving a scene. It
        // prevents new creation, but does not destroy already-owned instances.
        if(entry.active && (closing_ || !entry.status.enabled || entry.active->revision!=entry.revision ||
            (address_available && (!here || entry.active->incarnation!=scene->incarnation || entry.active->world!=frame.world)))) Retire(entry);
        for(auto it=entry.retired.begin();it!=entry.retired.end();) {
            api::OperationTrace timing("character.retire",id,it->creation,frame.sequence,
                it->session->Plan().package.id.c_str(),timing_enabled_?timing_:disabled);
            const bool controls_closed=!it->controls || it->controls->Close(frame);
            advance(*it);
            if(it->session->Status().closed && controls_closed) it=entry.retired.erase(it); else ++it;
        }
        if(entry.active) {
            api::OperationTrace timing("character.advance",id,entry.active->creation,frame.sequence,
                entry.plan.package.id.c_str(),timing_enabled_ && pending?timing_:disabled);
            if(!frame.battle.read.returned || frame.battle.value){
                if(entry.pending_task)entry.task_preparation_error="task preparation cancelled by world change";
                entry.pending_task.reset();++entry.task_revision;
            }
            else SubmitTask(entry);
            SubmitAction(entry);
            advance(*entry.active);
            // Adopt only a successfully played automatic successor of THIS
            // current user request. Do not replay it or rewrite its old receipt.
            if(const auto* handoff=entry.active->session->Handoff()) {
                if(entry.active->revision==entry.revision)entry.plan.presentation_phase=handoff->Playback().presentation_phase;
                const auto& output=handoff->Playback().successor;
                if(entry.intent_owner==Entry::IntentOwner::Action && output && (output->returned || output->observed) && entry.active->revision==entry.revision &&
                    entry.active->action_intent==entry.action_intent &&
                    entry.active->session->Status().submitted_action==entry.active->session_action &&
                    handoff->Status().phase==HandoffPhase::Ready &&
                    handoff->Status().servicing==entry.active->session->Status().handoff_request &&
                    output->predecessor==handoff->Status().playback_serial && handoff->Status().ready_domain && handoff->SuccessorSettled()) {
                    if(*handoff->Status().ready_domain==ActionDomain::Alternate && output->state==handoff->CurrentIdle())
                        entry.action=PlacedAction{ActionDomain::Alternate,BattleIdle{}};
                    else entry.action=PlacedAction{*handoff->Status().ready_domain,FreeStyleState{output->state,0,0,{},output->controller_state}};
                }
            }
            if(entry.active->session->Status().closed) Retire(entry);
            else if(entry.active->controls) {
                api::GameEntity* target=nullptr;
                if(entry.look.mode==LookMode::Player && player_)target=player_(frame);
                else if(entry.look.mode==LookMode::Instance && entries_.count(entry.look.target)) {
                    if(const auto* other=Active(entry.look.target,frame))target=other->Character().Creation().factory.entity;
                }
                const auto* handoff=entry.active->session->Handoff();
                const bool pending_action=entry.pending_task || (entry.intent_owner==Entry::IntentOwner::Action &&
                    entry.action && entry.active->action_intent!=entry.action_intent);
                const bool ready=handoff && handoff->Status().phase==HandoffPhase::Ready && !pending_action;
                entry.active->controls->Service(frame,appearance,target,ready);
                const auto& live=entry.active->controls->Status();
                if(ready && live.live.ready && !live.placement_pending && live.placement_phase!=api::PlacementPhase::Failed){
                    entry.placement.position=live.live.position;entry.placement.rotation=live.live.rotation;
                }
            }
        }
        if(entry.active || !can_create || !entry.status.enabled || !here || closing_) continue;
        bool predecessor_live=false;
        for(const auto& old:entry.retired)
            if(old.world==frame.world && old.incarnation==scene->incarnation && LiveEntities(old)) predecessor_live=true;
        if(predecessor_live) continue; // GC/native cleanup alone does not prevent a replacement
        if(entry.attempted_world==frame.world && entry.attempted_incarnation==scene->incarnation && entry.attempted_revision==entry.revision) continue;
        // Spread a restored batch across frames. Native factories cannot be
        // preempted; always admit the first pending creation to avoid starving
        // it behind already-active actors, then use elapsed time, not a count cap.
        if(started_creation && std::chrono::steady_clock::now()-creation_start>=std::chrono::milliseconds(2))continue;
        if(!started_creation){creation_start=std::chrono::steady_clock::now();started_creation=true;}
        entry.attempted_world=frame.world; entry.attempted_incarnation=scene->incarnation; entry.attempted_revision=entry.revision;
        entry.status.error.clear();
        entry.action_error=entry.visual_restore_error;
        entry.failed_revision=0;entry.creation_error.clear();
        ++entry.status.creations; // includes constructor failures before an epoch exists
        api::OperationTrace timing("character.create",id,entry.status.creations,frame.sequence,
            entry.plan.package.id.c_str(),timing_enabled_?timing_:disabled);
        try {
            {
                api::OperationSpan span("character.construct");
                entry.active.emplace(Epoch{std::make_unique<CharacterSession>(frame.world,entry.plan,services_),frame.world,scene->incarnation,entry.revision});
                entry.active->creation=entry.status.creations;
            }
            if(entry.action && entry.action_error.empty()) {
                entry.active->session_action=entry.active->session->Queue(entry.action->domain,entry.action->request);
                entry.active->action_intent=entry.action_intent;
            }
            {
                api::OperationSpan span("character.factory");
                create_(*entry.active->session,frame,SceneCreation{id,entry.placement});
            }
            if(entry.active->session->Status().phase==SessionPhase::Failed) {
                entry.failed_revision=entry.attempted_revision;
                entry.creation_error=entry.active->session->Status().errors.empty()?"NPC creation failed":entry.active->session->Status().errors.front();
                if(entry.look_submission)entry.look_submission->Complete(api::ControlPhase::Failed,entry.creation_error);
            }
            if(create_controls_) {
                api::OperationSpan span("controls.initialize");
                const auto& npc=entry.active->session->Character().Creation();
                for(const auto& entity:npc.entities)if(entity.token.entity==npc.factory.entity && !entity.disposed) {
                    try {
                        entry.active->controls=std::make_unique<CharacterControls>(create_controls_(entity.token,frame.world));
                        if(entry.placement.snap_to_ground || entry.plan.package.construction==ConstructionKind::StoryActor)
                            entry.active->controls->Place(entry.placement.position,entry.placement.rotation,false,entry.placement.snap_to_ground);
                        auto receipt=entry.active->controls->Look(entry.look);
                        if(entry.look_submission)entry.look_submission->Bind(entry.status.creations,std::move(receipt));
                    } catch(const std::exception& e) {
                        entry.status.error=std::string("optional controls: ")+e.what();
                        if(entry.look_submission)entry.look_submission->Complete(api::ControlPhase::Failed,entry.status.error);
                    }
                    break;
                }
            }
            if(entry.active->session->Status().phase==SessionPhase::NotCreated) throw std::runtime_error("creation callback did not submit the private NPC input");
            // Callback may remove/rebuild/shutdown. Retain its newer intent;
            // don't run appearance/actions for a superseded creation.
            if(closing_ || !entry.status.enabled || entry.active->revision!=entry.revision) Retire(entry);
            else {api::OperationSpan span("character.first-service");entry.active->session->Service(frame,appearance,types);}
        } catch(const std::exception& error) {
            entry.status.error=error.what();entry.creation_error=error.what();
            if(entry.look_submission)entry.look_submission->Complete(api::ControlPhase::Failed,error.what());
            entry.failed_revision=entry.attempted_revision;Retire(entry);
        }
        catch(...) {
            entry.status.error="unknown character creation failure";entry.creation_error=entry.status.error;
            if(entry.look_submission)entry.look_submission->Complete(api::ControlPhase::Failed,entry.status.error);
            entry.failed_revision=entry.attempted_revision;Retire(entry);
        }
    }
    // Entries remain owned and serviced while engine/session cleanup is pending.
    // Erase only at the delivery boundary, never from a synchronous callback.
    for(auto it=entries_.begin();it!=entries_.end();) {
        bool inputs=false;
        for(const auto& input:creation_inputs_)if(input.character==it->first && input.value){inputs=true;break;}
        if(it->second.status.deleting && !it->second.active && it->second.retired.empty() && !inputs)
            it=entries_.erase(it);
        else ++it;
    }
}
SceneCharacterStatus SceneCharacters::Status(std::uint64_t id) const {
    const auto& entry=entries_.at(id); auto out=entry.status;
    std::shared_ptr<CharacterTaskStatus> task_view;
    out.transient=entry.plan.persistence==CharacterPersistence::Session;
    out.active=entry.active.has_value(); out.retiring=entry.retired.size();
    for(const auto& input:creation_inputs_)if(input.character==id && input.value)++out.pending_inputs;
    out.revision=entry.revision;
    if(entry.failed_revision==entry.revision)out.creation_error=entry.creation_error;
    out.package=entry.plan.package;out.placement=entry.placement;out.look=entry.look;
    if(const auto input=entry.plan.visual_program;input && input->program) {
        out.visual_program=input->program->id;
        if(input->tasks){
            out.task_catalog=input->tasks->id;out.task_catalog_revision=input->catalog_revision;
            task_view=std::make_shared<CharacterTaskStatus>();out.tasks=task_view;task_view->domain=input->tasks->domain;
            for(const auto& [name,task]:input->tasks->tasks)task_view->definitions.emplace(name,task.definition);
        }
        for(const auto& [key,action]:input->program->actions)out.program_actions.emplace(key,action.state);
    }
    if(entry.plan.alternate && entry.plan.alternate->initial_state.available)
        out.battle_idle_state=entry.plan.alternate->initial_state.value;
    PresentationState presentation;presentation.Configure(entry.plan.presentation_profile,entry.plan.presentation_phase);
    out.presentation_phase=presentation.Phase();out.battle_idle_state=presentation.Idle(out.battle_idle_state);
    if(entry.action && entry.intent_owner==Entry::IntentOwner::Action)
        out.action=ActionIntentStatus{entry.action_intent,0,ActionIntentPhase::Waiting,{},"waiting for this intent's active creation"};
    if(out.action && !entry.action_error.empty()) {out.action->phase=ActionIntentPhase::Failed;out.action->detail=entry.action_error;}
    if(entry.active) {
        out.active_revision=entry.active->revision;
        out.session=entry.active->session->Status();
        const auto& character=entry.active->session->Character();
        const auto& npc=character.Creation();
        if(npc.create_returned && !npc.removal_requested && !npc.closed && out.active_revision==out.revision) {
            for(const auto& receipt:npc.entities)if(receipt.token.entity==npc.factory.entity && !receipt.disposed &&
                !api::TrackedEntities().IsDisposed(receipt.token) && receipt.observation.loaded.read.returned && receipt.observation.loaded.value)
                out.creation_ready=true;
            const auto* handoff=entry.active->session->Handoff();
            out.creation_ready=out.creation_ready && handoff && handoff->Status().phase==HandoffPhase::Ready && handoff->Status().ready_domain.has_value();
            if(handoff && handoff->Status().ready_domain==ActionDomain::Alternate) {
                if(const auto* overlay=character.AlternatePayload())
                    out.creation_ready=out.creation_ready && overlay->Active() && overlay->Active()->published;
            } else if(const auto* visual=character.AppearanceConsumer())
                out.creation_ready=out.creation_ready && visual->Active() && visual->Active()->Slot().adopted && visual->Active()->Slot().supply.returned;
        }
        if(const auto* outfit=entry.active->session->Character().Outfit())out.outfit=*outfit;
        for(const auto& error:entry.active->session->Character().Status().errors) {
            if(!out.error.empty())out.error+="; ";out.error+=error;
        }
        if(const auto* handoff=entry.active->session->Handoff()) {
            out.handoff=handoff->Status();out.playback=handoff->Playback();
            if(task_view && handoff->HasTasks())task_view->control=handoff->QueryTasks();
            if(task_view) {
                if(entry.pending_task)task_view->control.preparing=entry.pending_task->task;
                if(!entry.task_preparation_error.empty())task_view->control.preparation_error=entry.task_preparation_error;
                if(task_view->control.preparation_error.empty() && entry.intent_owner==Entry::IntentOwner::Task)
                    task_view->control.preparation_error=entry.active->session->Status().dispatch_error;
            }
            // Use the same live phase owner as automatic completion. The plan
            // only supplies the fallback before a battle model becomes active.
            if(out.active_revision==out.revision && out.handoff->ready_domain==ActionDomain::Alternate) {
                out.battle_idle_state=handoff->CurrentIdle();
                out.branch_state=handoff->BranchState();
            }
        }
        if(out.action && entry.active->action_intent!=entry.action_intent && entry.action_error.empty()) {
            out.action->creation=out.creations;out.action->detail="waiting for official gaze restoration before model handoff";
        }
        if(out.action && entry.active->revision==entry.revision && entry.active->action_intent==entry.action_intent) {
            auto& action=*out.action;const auto& session=*out.session;action.creation=out.creations;
            if(session.submitted_action==entry.active->session_action && !session.dispatch_error.empty()) {
                action.phase=ActionIntentPhase::Failed;action.detail=session.dispatch_error;
            } else if(session.submitted_action==entry.active->session_action && session.handoff_request &&
                out.handoff && out.handoff->servicing==session.handoff_request) {
                const auto& handoff=*out.handoff;
                action.phase=ActionIntentPhase::Dispatching;action.detail="waiting for model/input preparation or official playback call";
                if(handoff.phase==HandoffPhase::Failed) {action.phase=ActionIntentPhase::Failed;action.detail=handoff.error;}
                else if(handoff.output) {
                    action.outcome=handoff.output->outcome;action.detail=handoff.output->detail;
                    switch(*action.outcome) {
                    case ActionOutcome::Returned:action.phase=ActionIntentPhase::Returned;break;
                    case ActionOutcome::Waiting:break;
                    case ActionOutcome::Cancelled:case ActionOutcome::OwnerEnded:action.phase=ActionIntentPhase::Cancelled;break;
                    default:action.phase=ActionIntentPhase::Failed;break;
                    }
                }
            }
        }
        if(entry.active->controls)out.controls=entry.active->controls->Status();
        if(const auto* appearance=entry.active->session->Character().AppearanceConsumer()) {
            if(const auto* visual=appearance->Active()) {
                const auto& look=visual->Look();
                out.look_support=look.existing?"模型已有注视支持":look.prepared?"官方 NPC 注视配置已映射":look.not_authored?"NPC 源未提供普通注视组件":"注视配置尚未准备完成";
                for(const auto& error:look.errors)out.look_support+="; "+error;
                if(!appearance->Status().look_source.empty())out.look_support+="; source="+appearance->Status().look_source;
            }
        }
    }
    return out;
}
std::optional<VisualProgramSnapshot> SceneCharacters::CaptureVisualProgram(std::uint64_t id) const {
    const auto& entry=entries_.at(id);
    if(entry.plan.persistence==CharacterPersistence::Session)return {};
    if(!entry.visual_restore_error.empty())throw std::runtime_error(entry.visual_restore_error);
    if(entry.active && entry.active->revision==entry.revision)
        if(const auto* handoff=entry.active->session->Handoff())return handoff->CaptureVisualProgram();
    return entry.plan.visual_state;
}
void SceneCharacters::SaveVisualState(Entry& entry) {
    // An unavailable reconstruction recipe must never prevent removal. Keep
    // the error explicit instead of replaying a continuation with empty state.
    try {entry.plan.task_selection=CaptureTask(entry.status.id);entry.plan.visual_state=CaptureVisualProgram(entry.status.id);entry.visual_restore_error.clear();}
    catch(const std::exception& e){entry.plan.visual_state.reset();entry.visual_restore_error=e.what();}
}
std::optional<TaskSelection> SceneCharacters::CaptureTask(std::uint64_t id) const {
    const auto& entry=entries_.at(id);
    if(entry.plan.persistence==CharacterPersistence::Session || entry.intent_owner!=Entry::IntentOwner::Task)return {};
    if(entry.pending_task)return entry.pending_task;
    if(entry.active && !entry.active->session->Status().dispatch_error.empty())return entry.plan.task_selection;
    if(entry.active && entry.active->revision==entry.revision)
        if(const auto* handoff=entry.active->session->Handoff();handoff && handoff->HasTasks())return handoff->CaptureTask();
    return entry.plan.task_selection;
}
void SceneCharacters::RestoreTask(std::uint64_t id,TaskSelection selection) {
    auto& entry=Edit(id);
    if(entry.active)throw std::logic_error("saved task must be installed before creation");
    ValidateTaskSelection(selection);
    if(!entry.plan.visual_program || !entry.plan.visual_program->tasks)throw std::invalid_argument("saved task catalog absent");
    entry.plan.visual_program->tasks->Resolve(selection.task);
    entry.plan.task_selection=std::move(selection);entry.action.reset();entry.intent_owner=Entry::IntentOwner::Task;
}
void SceneCharacters::RestoreVisualProgram(std::uint64_t id,VisualProgramSnapshot value) {
    auto& entry=Edit(id);
    if(entry.active)throw std::logic_error("visual snapshot must be installed before character creation");
    CheckVisualRestore(entry.plan,value);entry.plan.visual_state=std::move(value);entry.visual_restore_error.clear();
}
void SceneCharacters::CheckVisualRestore(CharacterPlan plan,const VisualProgramSnapshot& value) const {
    BindProfile(plan,0);
    if(!plan.visual_program || !plan.visual_program->program)throw std::invalid_argument("saved visual program is not configured");
    ValidateVisualRestore(*plan.visual_program->program,value);
}
std::vector<std::uint64_t> SceneCharacters::Ids() const {
    std::vector<std::uint64_t> out; for(const auto& item:entries_) out.push_back(item.first); return out;
}
const CharacterSession* SceneCharacters::Active(std::uint64_t id,const api::WorldFrame& frame) const {
    if(!Contains(id))return nullptr;
    const auto& entry=entries_.at(id);
    if(closing_ || !entry.status.enabled || !entry.active || entry.active->revision!=entry.revision ||
        entry.active->world!=frame.world || !frame.disposing.read.returned || frame.disposing.value) return nullptr;
    return entry.active->session.get();
}
bool SceneCharacters::Closed() const {
    if(!closing_ || !creation_inputs_.empty()) return false;
    for(const auto& item:entries_) if(item.second.active || !item.second.retired.empty()) return false;
    return true;
}
}
