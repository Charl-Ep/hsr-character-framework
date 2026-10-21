#include <hksr/framework/application/ApplicationService.hpp>
#include <algorithm>
#include <cmath>
#include <tuple>
#include <random>
#include <iomanip>
#include <sstream>

namespace hksr::framework {
namespace {
bool Equal(float a,float b) {return a==b || (std::isnan(a) && std::isnan(b));}
bool Equal(const api::CallResult& a,const api::CallResult& b) {return a.returned==b.returned && a.exception==b.exception;}
template<class T> bool Equal(const api::Observed<T>& a,const api::Observed<T>& b) {return a.value==b.value && Equal(a.read,b.read);}
template<class T,class F> bool Optional(const std::optional<T>& a,const std::optional<T>& b,F equal) {
    return a.has_value()==b.has_value() && (!a || equal(*a,*b));
}
bool Equal(const api::Position3& a,const api::Position3& b) {return Equal(a.x,b.x) && Equal(a.y,b.y) && Equal(a.z,b.z);}
bool Equal(const api::Rotation4& a,const api::Rotation4& b) {return Equal(a.x,b.x) && Equal(a.y,b.y) && Equal(a.z,b.z) && Equal(a.w,b.w);}
bool Equal(const CharacterPlacement& a,const CharacterPlacement& b) {return Equal(a.position,b.position) && Equal(a.rotation,b.rotation) && a.snap_to_ground==b.snap_to_ground;}
bool Equal(const LookIntent& a,const LookIntent& b) {return a.mode==b.mode && a.target==b.target && Equal(a.point,b.point) && Equal(a.max_distance,b.max_distance);}
bool Equal(const ActionReceipt& a,const ActionReceipt& b) {
    // The immutable request belongs to serial within the creation epoch. Never
    // compare or traverse its configuration graph/shared resource payloads.
    return std::tie(a.serial,a.outcome,a.detail)==std::tie(b.serial,b.outcome,b.detail) &&
        Equal(a.hash,b.hash) && Equal(a.has_state,b.has_state) && Equal(a.initialized,b.initialized) && Equal(a.call,b.call);
}
bool Equal(const CharacterPackage& a,const CharacterPackage& b) {
    return std::tie(a.id,a.display_name,a.npc_id,a.source,a.avatar_id)==std::tie(b.id,b.display_name,b.npc_id,b.source,b.avatar_id) &&
        Optional(a.appearance,b.appearance,[](const auto& x,const auto& y){
            return std::tie(x.template_prefab,x.asset_directory,x.character_config,x.weapon_config,x.shapes)==
                std::tie(y.template_prefab,y.asset_directory,y.character_config,y.weapon_config,y.shapes);
        });
}
bool Equal(const SceneCharacterStatus& a,const SceneCharacterStatus& b) {
    if(a.transient!=b.transient)return false;
    if(a.branch_state.phase!=b.branch_state.phase || a.branch_state.values.size()!=b.branch_state.values.size())return false;
    for(std::size_t i=0;i<a.branch_state.values.size();++i) {
        const auto& x=a.branch_state.values[i];const auto& y=b.branch_state.values[i];
        if(std::tie(x.scope,x.name,x.value)!=std::tie(y.scope,y.name,y.value))return false;
    }
    if(a.presentation_phase!=b.presentation_phase || a.visual_program!=b.visual_program || a.program_actions!=b.program_actions)return false;
    if(std::tie(a.id,a.creations,a.revision,a.active_revision,a.creation_ready,a.creation_error,a.enabled,a.active,a.deleting,
        a.retiring,a.pending_inputs,a.scene,a.error,a.look_support,a.battle_idle_state)!=
        std::tie(b.id,b.creations,b.revision,b.active_revision,b.creation_ready,b.creation_error,b.enabled,b.active,b.deleting,
        b.retiring,b.pending_inputs,b.scene,b.error,b.look_support,b.battle_idle_state))return false;
    if(!Equal(a.package,b.package) || !Equal(a.placement,b.placement) || !Equal(a.look,b.look))return false;
    if(!Optional(a.action,b.action,[](const auto& x,const auto& y){return
        std::tie(x.id,x.creation,x.phase,x.outcome,x.detail)==std::tie(y.id,y.creation,y.phase,y.outcome,y.detail);}))return false;
    if(!Optional(a.session,b.session,[](const auto& x,const auto& y){return
        std::tie(x.phase,x.baseline_attempted,x.alternate_available,x.closed,x.selected_action,x.submitted_action,x.handoff_request,x.dispatch_error,x.waiting,x.errors)==
        std::tie(y.phase,y.baseline_attempted,y.alternate_available,y.closed,y.selected_action,y.submitted_action,y.handoff_request,y.dispatch_error,y.waiting,y.errors);}))return false;
    if(!Optional(a.handoff,b.handoff,[](const auto& x,const auto& y){return
        std::tie(x.requested,x.servicing,x.playback_serial,x.phase,x.ready_domain,x.error,x.initialization_note,x.event_value_note,x.closed)==
        std::tie(y.requested,y.servicing,y.playback_serial,y.phase,y.ready_domain,y.error,y.initialization_note,y.event_value_note,y.closed) &&
        Optional(x.output,y.output,[](const auto& p,const auto& q){return Equal(p,q);}) &&
        Equal(x.initial_state.read,y.initial_state.read) && Equal(x.initial_state.normalized_time,y.initial_state.normalized_time) &&
        std::tie(x.initial_state.short_name_hash,x.initial_state.full_path_hash,x.initial_state.loop)==
        std::tie(y.initial_state.short_name_hash,y.initial_state.full_path_hash,y.initial_state.loop);}))return false;
    if(!Optional(a.playback,b.playback,[](const auto& x,const auto& y){return
        std::tie(x.latest_serial,x.pending,x.closing,x.closed,x.completion_pending,x.completion_note,x.visual_note,x.blink_note,x.ready_note,x.effect_note,x.presentation_phase,x.presentation_note)==
        std::tie(y.latest_serial,y.pending,y.closing,y.closed,y.completion_pending,y.completion_note,y.visual_note,y.blink_note,y.ready_note,y.effect_note,y.presentation_phase,y.presentation_note) &&
        Optional(x.last,y.last,[](const auto& p,const auto& q){return Equal(p,q);}) &&
        Optional(x.successor,y.successor,[](const auto& p,const auto& q){return
            std::tie(p.predecessor,p.serial,p.state,p.returned,p.observed,p.controller_state)==std::tie(q.predecessor,q.serial,q.state,q.returned,q.observed,q.controller_state);});}))return false;
    if(!Optional(a.controls,b.controls,[](const auto& x,const auto& y){return
        std::tie(x.placement_pending,x.placement_phase,x.gaze_phase,x.placement_error,x.gaze_error,x.live.ready,x.live.error)==
        std::tie(y.placement_pending,y.placement_phase,y.gaze_phase,y.placement_error,y.gaze_error,y.live.ready,y.live.error) &&
        Equal(x.look,y.look) && Equal(x.live.position,y.live.position) && Equal(x.live.rotation,y.live.rotation) &&
        Equal(x.live.position_read,y.live.position_read) && Equal(x.live.rotation_read,y.live.rotation_read);}))return false;
    return Optional(a.outfit,b.outfit,[](const auto& x,const auto& y){return
        std::tie(x.started,x.applicable,x.applied,x.restore_attempted,x.closed,x.avatar,x.gender,x.base_type,x.config_refresh_required,x.errors)==
        std::tie(y.started,y.applicable,y.applied,y.restore_attempted,y.closed,y.avatar,y.gender,y.base_type,y.config_refresh_required,y.errors) &&
        Equal(x.apply,y.apply) && Equal(x.refresh,y.refresh) && Equal(x.restore,y.restore);});
}
bool Equal(const OperationStatus& a,const OperationStatus& b) {
    return std::tie(a.id,a.character,a.source_request,a.revision,a.action_intent,a.creation,a.kind,a.phase,a.detail)==
        std::tie(b.id,b.character,b.source_request,b.revision,b.action_intent,b.creation,b.kind,b.phase,b.detail);
}
template<class Map,class F> std::vector<std::uint64_t> Changed(const Map& a,const Map& b,F equal) {
    std::vector<std::uint64_t> out;auto x=a.begin(),y=b.begin();
    while(x!=a.end() || y!=b.end()) {
        if(y==b.end() || (x!=a.end() && x->first<y->first)){out.push_back(x++->first);continue;}
        if(x==a.end() || y->first<x->first){out.push_back(y++->first);continue;}
        if(!equal(x->second,y->second))out.push_back(x->first);
        ++x;++y;
    }
    return out;
}
}
ApplicationEvents::ApplicationEvents(std::size_t retained):retained_(retained) {
    // Session identity, not a source/content hash. A cursor from a previous
    // process or another application service must never resume this stream.
    std::random_device entropy;std::ostringstream id;id<<std::hex<<std::setfill('0');
    for(unsigned i=0;i<4;++i)id<<std::setw(8)<<static_cast<std::uint32_t>(entropy());
    stream_=id.str();
}
bool ApplicationEvents::Publish(const ApplicationSnapshot& previous,const ApplicationSnapshot& next) {
    ApplicationChange change;
    change.runtime=std::tie(previous.prepared,previous.closing,previous.closed,previous.map_error,previous.binding_errors,previous.camera_presentations)!=
        std::tie(next.prepared,next.closing,next.closed,next.map_error,next.binding_errors,next.camera_presentations) ||
        !Optional(previous.scene,next.scene,[](const auto& a,const auto& b){return a.key==b.key && a.incarnation==b.incarnation;});
    change.runtime=change.runtime || previous.layouts.sequence!=next.layouts.sequence || previous.layouts.available!=next.layouts.available;
    // Published catalog/inspection values are immutable. Their owning readers
    // replace snapshots on publication, not on every framework frame.
    change.catalog=previous.catalog!=next.catalog;
    change.inspection=previous.npc_actions!=next.npc_actions || previous.controller_actions!=next.controller_actions ||
        previous.controller_catalog_error!=next.controller_catalog_error;
    std::map<std::uint64_t,const SceneCharacterStatus*> a,b;
    for(const auto& actor:previous.characters)a.emplace(actor.id,&actor);
    for(const auto& actor:next.characters)b.emplace(actor.id,&actor);
    change.characters=Changed(a,b,[](auto x,auto y){return Equal(*x,*y);});
    for(const auto id:change.characters) {
        const auto old=a.find(id),current=b.find(id);
        if(old==a.end() || current==b.end())continue;
        if(old->second->visual_program!=current->second->visual_program || old->second->program_actions!=current->second->program_actions)
            change.inspection=true;
    }
    change.operations=Changed(previous.operations.entries,next.operations.entries,[](const auto& x,const auto& y){return Equal(x,y);});
    if(change.Empty())return false;
    change.sequence=sequence_+1;events_.push_back(std::move(change));++sequence_;
    while(events_.size()>retained_)events_.pop_front();
    return true;
}
ApplicationChangeLog ApplicationEvents::Read(const std::optional<ApplicationCursor>& after) const {
    ApplicationChangeLog out;out.cursor={stream_,sequence_};
    out.reset=!after || after->stream!=stream_ || after->sequence>sequence_ ||
        (after->sequence<sequence_ && (events_.empty() || after->sequence<events_.front().sequence-1));
    if(!out.reset)for(const auto& event:events_)if(event.sequence>after->sequence)out.events.push_back(event);
    return out;
}
}
