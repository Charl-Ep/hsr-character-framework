#include <hksr/framework/application/OperationHistory.hpp>
#include <algorithm>
#include <stdexcept>

namespace hksr::framework {
void OperationHistory::Add(OperationStatus operation,std::shared_ptr<const ControlSubmission> control) {
    if(!operation.id || snapshot_.entries.count(operation.id))throw std::logic_error("operation ID must be unique and nonzero");
    const auto id=operation.id;const bool done=operation.Terminal();
    snapshot_.entries.emplace(id,std::move(operation));++snapshot_.sequence;
    if(control && !done)controls_.emplace(id,std::move(control));
    if(done)terminal_.push_back(id);
    Trim();
}
void OperationHistory::Trim() {
    while(terminal_.size()>retained_terminal_) {
        snapshot_.entries.erase(terminal_.front());terminal_.pop_front();++snapshot_.sequence;
    }
}
void OperationHistory::Observe(const std::vector<SceneCharacterStatus>& characters,const CatalogSnapshot* catalog,bool closing) {
    for(auto& item:snapshot_.entries) {
        auto& value=item.second;if(value.Terminal())continue;
        const auto old_phase=value.phase;const auto old_detail=value.detail;const auto old_creation=value.creation;
        auto set=[&](OperationPhase phase,std::string detail){value.phase=phase;value.detail=std::move(detail);};
        if(value.kind==OperationKind::Placement || value.kind==OperationKind::Gaze) {
            const auto control=controls_.find(value.id);
            if(control==controls_.end())set(OperationPhase::Failed,"control receipt unavailable");
            else {
                const auto status=control->second->Snapshot();value.creation=status.creation;
                switch(status.result.phase) {
                case api::ControlPhase::Waiting:set(OperationPhase::Waiting,status.result.detail);break;
                case api::ControlPhase::Running:set(OperationPhase::Running,status.result.detail);break;
                case api::ControlPhase::Returned:set(OperationPhase::Returned,status.result.detail);break;
                case api::ControlPhase::Failed:set(OperationPhase::Failed,status.result.detail);break;
                case api::ControlPhase::Cancelled:set(OperationPhase::Cancelled,status.result.detail);break;
                case api::ControlPhase::Superseded:set(OperationPhase::Superseded,status.result.detail);break;
                }
                // Its owning scene/session supplies cancellation. A movement
                // call may already have returned before shutdown is serviced.
            }
        } else if(value.kind==OperationKind::PrepareSource) {
            const auto* source=catalog?catalog->Source(value.source_request):nullptr;
            if(closing)set(OperationPhase::Cancelled,"source preparation cancelled by host shutdown");
            else if(!source)set(OperationPhase::Cancelled,"source request released");
            else switch(source->phase) {
            case SourceRequestPhase::Queued:set(OperationPhase::Waiting,"source queued");break;
            case SourceRequestPhase::Preparing:set(OperationPhase::Running,"preparing source inputs");break;
            case SourceRequestPhase::Ready:set(OperationPhase::Returned,"character plan available; no entity created");break;
            case SourceRequestPhase::Unresolved:case SourceRequestPhase::Failed:
                set(OperationPhase::Failed,source->error.empty()?"source has no usable character plan":source->error);break;
            }
        } else {
            const auto it=std::find_if(characters.begin(),characters.end(),[&](const auto& c){return c.id==value.character;});
            if(it==characters.end()) {
                if(value.kind==OperationKind::Delete)set(OperationPhase::Returned,"instance deleted after owned resources drained");
                else set(OperationPhase::Cancelled,"logical instance no longer available");
            }
            else {
                const auto& c=*it;
                if(c.revision!=value.revision)set(OperationPhase::Superseded,"new lifecycle intent replaced this operation before completion");
                else if(value.kind==OperationKind::Delete)set(OperationPhase::Running,"waiting for deletion cleanup");
                else if(value.kind==OperationKind::Disable) {
                    if(!c.active && !c.retiring && !c.pending_inputs)set(OperationPhase::Returned,"instance sessions, controls and creation input roots drained");
                    else set(OperationPhase::Running,"waiting for actual session, control and creation input cleanup");
                } else if(closing)set(OperationPhase::Cancelled,"host shutdown; cleanup is tracked separately");
                else if(!c.enabled)set(OperationPhase::Cancelled,"instance is disabled");
                else if(value.kind==OperationKind::Action) {
                    if(!c.action || c.action->id!=value.action_intent)set(OperationPhase::Superseded,"new action intent replaced this operation before completion");
                    else {
                        value.creation=c.action->creation;
                        switch(c.action->phase) {
                        case ActionIntentPhase::Waiting:set(OperationPhase::Waiting,c.action->detail);break;
                        case ActionIntentPhase::Dispatching:set(OperationPhase::Running,c.action->detail);break;
                        case ActionIntentPhase::Returned:set(OperationPhase::Returned,"official playback call returned; animation completion is not asserted");break;
                        case ActionIntentPhase::Failed:set(OperationPhase::Failed,c.action->detail);break;
                        case ActionIntentPhase::Cancelled:set(OperationPhase::Cancelled,c.action->detail);break;
                        }
                        if(!value.Terminal() && !c.creation_error.empty())set(OperationPhase::Failed,c.creation_error);
                    }
                } else if(!c.creation_error.empty())set(OperationPhase::Failed,c.creation_error);
                else if(c.active_revision==value.revision && c.handoff && c.handoff->phase==HandoffPhase::Failed)
                    set(OperationPhase::Failed,c.handoff->error);
                else if(c.active_revision==value.revision && c.creation_ready) {
                    value.creation=c.creations;
                    set(OperationPhase::Returned,"owned entity loaded and requested appearance adopted; visual/effect completion is not asserted");
                } else if(c.active && c.active_revision==value.revision) {
                    value.creation=c.creations;set(OperationPhase::Running,"waiting for owned entity loading and appearance adoption");
                } else set(OperationPhase::Waiting,c.retiring?"waiting for previous creation retirement":"waiting for eligible scene creation");
            }
        }
        if(value.phase!=old_phase || value.detail!=old_detail || value.creation!=old_creation)++snapshot_.sequence;
        if(value.Terminal()) {terminal_.push_back(value.id);controls_.erase(value.id);}
    }
    Trim();
}
}
