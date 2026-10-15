#include <hksr/framework/layout/LayoutController.hpp>
#include <hksr/framework/character/SceneCharacters.hpp>
#include <algorithm>
#include <stdexcept>
#include <tuple>

namespace hksr::framework {
namespace {
bool Equal(const MapLayoutStatus& a,const MapLayoutStatus& b) {
    return std::tie(a.initialized,a.busy,a.phase,a.error,a.storage_error,a.operation,a.name,a.command,a.revision,a.saved_revision,a.names)==
        std::tie(b.initialized,b.busy,b.phase,b.error,b.storage_error,b.operation,b.name,b.command,b.revision,b.saved_revision,b.names);
}
}
LayoutController::LayoutController(SceneCharacters& characters,CatalogBrowser& catalog,std::shared_ptr<LayoutIo> io,
    std::function<void(std::uint64_t)> removed,bool private_models)
    :characters_(characters),catalog_(catalog),io_(std::move(io)),removed_(std::move(removed)),private_models_(private_models) {}
void LayoutController::Release(Map& map) {
    for(auto id:map.sources)catalog_.ReleaseSource(id);
    map.sources.clear();map.plans.clear();map.actions.clear();
}
void LayoutController::Fail(Map& map,std::string error) {
    Release(map);map.phase=Phase::Failed;map.status.phase="failed";map.status.busy=false;map.status.error=std::move(error);
}
void LayoutController::Begin(Map& map,SceneLayout layout) {
    ValidateSceneLayout(layout);Release(map);map.incoming=std::move(layout);
    map.phase=Phase::Preparing;map.status.phase="preparing";map.status.busy=true;map.status.error.clear();
    for(const auto& row:map.incoming->characters)map.sources.push_back(catalog_.Prepare(row.source));
}
void LayoutController::Prepare(Map& map) {
    std::vector<CharacterPlan> plans;std::vector<std::optional<PlacedAction>> actions;
    const auto snapshot=catalog_.Snapshot();bool pending=false;
    for(std::size_t i=0;i<map.sources.size();++i) {
        const auto* source=snapshot->Source(map.sources[i]);
        if(!source)throw std::runtime_error("layout source request was released");
        if(source->Pending()){pending=true;continue;}
        if(source->phase!=SourceRequestPhase::Ready) {
            std::string error="layout source unresolved (entry "+std::to_string(map.incoming->characters[i].key)+"): "+source->error;
            for(const auto& note:source->result.errors)error+=" "+note;
            throw std::runtime_error(error);
        }
        auto plan=catalog_.Plan(source->id);
        plan.presentation_phase=map.incoming->characters[i].presentation_phase;
        if((plan.package.appearance || plan.payload) && !private_models_)
            throw std::runtime_error("private model restoration requires the model release observer");
        const auto& saved=map.incoming->characters[i].action;
        std::optional<PlacedAction> action;
        // Even a proposal seen early can gain exit tasks during discovery. A
        // configured/ready request waits for its source's complete projection,
        // not unrelated sources in the shared catalog.
        if(saved && !std::holds_alternative<NpcMotion>(*saved) && !source->actions.finished){pending=true;continue;}
        try {action=RestoreLayoutAction(saved,source->actions);}
        catch(const std::exception&) {
            const auto* configured=saved?std::get_if<SavedConfiguredAction>(&*saved):nullptr;
            if(!configured || configured->domain!=ActionDomain::Alternate || !plan.alternate ||
                !plan.alternate->initial_state.available || plan.alternate->initial_state.value!=configured->state)throw;
            action=PlacedAction{ActionDomain::Alternate,FreeStyleState{configured->state}};
        }
        plans.push_back(std::move(plan));actions.push_back(std::move(action));
    }
    if(pending)return;
    map.plans=std::move(plans);map.actions=std::move(actions);
    map.old.clear();map.added.clear();
    for(auto id:characters_.Ids())if(characters_.Status(id).scene==map.incoming->scene)map.old.push_back(id);
    // All file/source/action preparation completed before touching old actors.
    for(auto id:map.old){characters_.Delete(id);catalog_.ForgetCharacter(id);if(removed_)removed_(id);}
    map.phase=Phase::Removing;map.status.phase="removing";
}
void LayoutController::Install(Map& map) {
    if(std::any_of(map.old.begin(),map.old.end(),[&](auto id){return characters_.Contains(id);}))return;
    std::map<std::uint64_t,std::uint64_t> keys;
    for(std::size_t i=0;i<map.incoming->characters.size();++i) {
        const auto& saved=map.incoming->characters[i];
        const auto id=characters_.Add(map.incoming->scene,std::move(map.plans[i]),std::move(map.actions[i]),saved.placement);
        keys.emplace(saved.key,id);map.added.push_back(id);
        characters_.SetEnabled(id,saved.enabled);
        catalog_.InspectActions(id,characters_.Plan(id).action_sources);
    }
    for(std::size_t i=0;i<map.added.size();++i) {
        auto look=map.incoming->characters[i].look;
        if(look.mode==LookMode::Instance)look.target=keys.at(look.target);
        characters_.Look(map.added[i],look);
    }
    Release(map);map.status.initialized=true;map.phase=Phase::Applying;map.status.phase="applying";
    // The intended layout is now installed. Persist it even if the game later
    // refuses a particular actor; keep that actor's actual failure visible.
    map.captured.reset();map.incoming.reset();
}
void LayoutController::ObserveApplied(Map& map) {
    bool pending=false;
    for(auto id:map.added) {
        if(!characters_.Contains(id))continue; // a later user deletion wins
        const auto s=characters_.Status(id);if(s.deleting || !s.enabled)continue;
        if(!s.creation_error.empty())throw std::runtime_error("restored character "+std::to_string(id)+": "+s.creation_error);
        if(s.action && s.action->phase==ActionIntentPhase::Failed)
            throw std::runtime_error("restored action "+std::to_string(id)+": "+s.action->detail);
        if(!s.creation_ready || (s.action && (s.action->phase==ActionIntentPhase::Waiting || s.action->phase==ActionIntentPhase::Dispatching)))pending=true;
    }
    if(!pending){map.phase=Phase::Ready;map.status.phase="ready";map.status.busy=false;}
}
void LayoutController::Advance(const std::optional<SceneAddress>& scene) {
    if(closed_)return;
    const auto next=scene?scene->key:std::string{};
    if(next!=current_){Capture(true);current_=next;}
    if(scene && !maps_.count(scene->key)) {
        auto& map=maps_[scene->key];map.status.busy=true;map.status.phase="reading";
        map.status.operation="restore_current";
        map.io=io_->Submit({LayoutIoKind::ReadCurrent,scene->key});
        io_->Submit({LayoutIoKind::List,scene->key});
    }
    for(auto& result:io_->Take()) {
        const auto found=maps_.find(result.request.scene);if(found==maps_.end())continue;
        auto& map=found->second;const auto kind=result.request.kind;
        if(kind==LayoutIoKind::WriteCurrent) {
            if(result.error.empty())map.status.saved_revision=std::max(map.status.saved_revision,result.request.revision);
            map.status.storage_error=result.error;continue;
        }
        if(kind==LayoutIoKind::List) {
            if(result.error.empty())map.status.names=std::move(result.names);
            if(result.id!=map.io)continue;
        }else if(result.id!=map.io)continue;
        map.io=0;
        try {
            if(!result.error.empty())throw std::runtime_error(result.error);
            if(kind==LayoutIoKind::ReadCurrent || kind==LayoutIoKind::Load) {
                if(result.layout)Begin(map,std::move(*result.layout));
                else {map.status.initialized=true;map.phase=Phase::Ready;map.status.phase="ready";map.status.busy=false;}
            } else {
                if(kind!=LayoutIoKind::List)map.status.names=std::move(result.names);
                map.status.busy=false;map.status.phase="ready";
            }
        }catch(const std::exception& e){Fail(map,e.what());}
    }
    for(auto& entry:maps_) {
        auto& map=entry.second;
        // Queries may be prepared on any safe exploration frame; replacement
        // and creation of this map can only commit when it is actually current.
        if(entry.first!=current_)continue;
        try {
            if(map.phase==Phase::Preparing)Prepare(map);
            if(map.phase==Phase::Removing)Install(map);
            if(map.phase==Phase::Applying)ObserveApplied(map);
        }catch(const std::exception& e){Fail(map,e.what());}
    }
}
bool LayoutController::Replacing(const std::string& scene) const {
    const auto found=maps_.find(scene);if(found==maps_.end())return false;
    const auto& m=found->second;
    return m.phase==Phase::Reading || m.phase==Phase::Preparing || m.phase==Phase::Removing ||
        (m.status.busy && m.status.operation=="load");
}
void LayoutController::CaptureMap(const std::string& scene,Map& map) {
    if(!map.status.initialized || Replacing(scene))return;
    try {
        auto value=CaptureSceneLayout(characters_,scene);
        if(map.captured && SameLayout(value,*map.captured))return;
        map.captured=std::make_shared<const SceneLayout>(std::move(value));++map.status.revision;
        io_->Submit({LayoutIoKind::WriteCurrent,scene,{},map.captured,false,map.status.revision});
        map.status.storage_error.clear();
    }catch(const std::exception& e){map.status.storage_error=e.what();}
}
void LayoutController::Capture(bool force) {
    if(closed_)return;
    const auto now=std::chrono::steady_clock::now();
    if(!force && now-captured_at_<std::chrono::milliseconds(250))return;
    captured_at_=now;for(auto& entry:maps_)CaptureMap(entry.first,entry.second);
}
void LayoutController::Execute(const LayoutCommand& command,const std::string& scene,std::uint64_t serial) {
    if(closed_)throw std::runtime_error("layout service is closing");
    const auto found=maps_.find(scene);if(found==maps_.end())throw std::runtime_error("map working state not observed yet");
    auto& map=found->second;
    if(map.status.busy)throw std::runtime_error("this map already has a layout operation in progress");
    map.status.command=serial;map.status.name=command.name;map.status.error.clear();
    LayoutIoRequest request;request.scene=scene;request.name=command.name;request.overwrite=command.overwrite;
    switch(command.operation) {
    case LayoutOperation::Refresh:request.kind=LayoutIoKind::List;map.status.operation="refresh";break;
    case LayoutOperation::Save:
        request.kind=LayoutIoKind::Save;map.status.operation="save";
        request.layout=std::make_shared<const SceneLayout>(CaptureSceneLayout(characters_,scene));break;
    case LayoutOperation::Load:request.kind=LayoutIoKind::Load;map.status.operation="load";break;
    case LayoutOperation::Delete:request.kind=LayoutIoKind::Delete;map.status.operation="delete_snapshot";break;
    case LayoutOperation::RetryCurrent:
        if(map.status.initialized) {
            // Explicit retry of a failed automatic write uses the CURRENT
            // values, never an older queued snapshot.
            map.captured.reset();CaptureMap(scene,map);map.status.operation="retry_save";return;
        }
        request.kind=LayoutIoKind::ReadCurrent;map.phase=Phase::Reading;map.status.operation="restore_current";break;
    case LayoutOperation::KeepCurrent:
        if(map.status.initialized)throw std::runtime_error("working state already initialized");
        map.status.initialized=true;map.phase=Phase::Ready;map.status.phase="ready";map.status.operation="keep_current";
        map.captured.reset();CaptureMap(scene,map);return;
    }
    map.io=io_->Submit(std::move(request));map.status.busy=true;map.status.phase="io";
}
LayoutSnapshot LayoutController::Snapshot() const {
    bool changed=!published_.available || published_.maps.size()!=maps_.size();
    for(const auto& entry:maps_) {
        const auto found=published_.maps.find(entry.first);
        if(found==published_.maps.end() || !Equal(found->second,entry.second.status))changed=true;
    }
    if(changed){published_.available=true;++published_.sequence;published_.maps.clear();
        for(const auto& entry:maps_)published_.maps.emplace(entry.first,entry.second.status);}
    return published_;
}
void LayoutController::Close() {
    if(closed_)return;Capture(true);closed_=true;io_->Seal();
    for(auto& entry:maps_) {Release(entry.second);if(entry.second.status.busy){entry.second.status.busy=false;entry.second.status.phase="cancelled";}}
}
}
