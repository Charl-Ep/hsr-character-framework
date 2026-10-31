#include <hksr/framework/host/FrameworkHost.hpp>
#include <stdexcept>
#include <type_traits>
#include <sstream>
#include <algorithm>

namespace hksr::framework {
void FrameworkHost::Trace(const std::string& value) const noexcept {
    if(diagnostic_) try {diagnostic_(value);} catch(...) {} // diagnostics never own gameplay control
}
void FrameworkHost::RecordPerformance(const std::string& stage,std::uint64_t elapsed) noexcept {
    try {auto& row=performance_[stage];++row.calls;row.total_us+=elapsed;row.max_us=std::max(row.max_us,elapsed);}catch(...){}
}
void FrameworkHost::PublishPerformance() {
    const auto now=std::chrono::steady_clock::now();
    if(now-performance_since_<std::chrono::seconds(1))return;
    const bool slow=std::any_of(performance_.begin(),performance_.end(),[](const auto& row){return row.second.max_us>=8000;});
    if(slow)for(const auto& item:performance_)Trace("performance stage="+item.first+" calls="+std::to_string(item.second.calls)+
        " total_us="+std::to_string(item.second.total_us)+" max_us="+std::to_string(item.second.max_us));
    performance_.clear();performance_since_=now;
}
void FrameworkHost::Publish(const std::optional<SceneAddress>& scene) {
    ApplicationSnapshot out;
    out.prepared=true;out.closing=closing_;out.scene=scene;out.map_error=identity_->Error();
    out.closed=closing_ && characters_->Closed() && identity_->Closed() && type_roots_.empty() && effects_closed_ && !coordinator_->Catalog();
    coordinator_->Populate(out);
    for(const auto& entry:out.layouts.maps) {
        const auto& value=entry.second;
        const auto stamp=value.phase+":"+value.error;
        if(layout_traces_[entry.first]!=stamp) {
            layout_traces_[entry.first]=stamp;
            Trace("layout scene="+entry.first+" phase="+value.phase+" error="+value.error);
        }
    }
    if(out.catalog) {
        const auto& catalog=*out.catalog;
        for(auto it=source_traces_.begin();it!=source_traces_.end();)
            if(!catalog.sources.count(it->first))it=source_traces_.erase(it);else ++it;
        if(catalog.rows && traced_catalog_!=catalog.rows) {
            traced_catalog_=catalog.rows;
            const auto summary=[&](const char* name,const auto& batch) {
                Trace(std::string("catalog ")+name+" rows="+std::to_string(batch.rows.size())+" declared="+
                    std::to_string(batch.declared_count)+" visited="+std::to_string(batch.visited_count)+
                    " enumerated="+std::to_string(batch.enumerated));
                for(const auto& issue:batch.issues) Trace(std::string("catalog ")+name+" index="+std::to_string(issue.index)+" "+issue.operation+": "+issue.error);
            };
            summary("npc",catalog.rows->npcs);summary("player",catalog.rows->players);summary("avatar",catalog.rows->avatars);
            for(const auto& row:catalog.rows->npcs.rows) Trace("catalog npc id="+std::to_string(row.id)+" config="+row.character_config+" entity_config="+row.model_config);
            for(const auto& row:catalog.rows->players.rows) Trace("catalog player id="+std::to_string(row.id)+" enhanced="+std::to_string(row.enhanced)+
                " skin="+std::to_string(row.skin)+" avatar="+std::to_string(row.avatar)+" config="+row.character_config+" prefab="+row.prefab);
        }
        for(const auto& entry:catalog.sources)if(!entry.second->Pending()) {
            const auto& request=*entry.second;const auto& source=request.result;
            std::ostringstream line;line<<"source request="<<entry.first<<" phase="<<static_cast<int>(request.phase)<<" foundation="<<static_cast<int>(source.foundation.status);
            if(const auto* player=std::get_if<BrowsePlayer>(&request.input)) line<<" player="<<player->key.id<<" enhanced="<<player->key.enhanced<<" skin="<<player->key.skin;
            for(auto id:source.foundation.candidates) line<<" candidate="<<id;
            if(source.character) line<<" prepared="<<source.character->id;
            if(!request.error.empty())line<<" error="<<request.error;
            for(const auto& error:source.errors) line<<" error="<<error;
            for(const auto& error:source.foundation.errors) line<<" foundation_error="<<error;
            for(const auto& note:source.foundation.notes) line<<" foundation_note="<<note;
            auto& previous=source_traces_[entry.first];
            if(line.str()!=previous) {previous=line.str();Trace(previous);}
        }
    }
    out.npc_actions=npc_actions_;
    out.controller_actions=controller_actions_;
    out.controller_catalog_error=controller_catalog_error_;
    for(const auto& status:out.characters) {
        const auto id=status.id;std::ostringstream line;
        line<<"character="<<id<<" npc="<<status.package.npc_id<<" enabled="<<status.enabled
            <<" active="<<status.active<<" creations="<<status.creations<<" retiring="<<status.retiring
            <<" error="<<status.error;
        if(status.session) {
            line<<" phase="<<static_cast<int>(status.session->phase)<<" waiting="<<status.session->waiting;
            for(const auto& error:status.session->errors)line<<" session_error="<<error;
        }
        if(status.handoff)line<<" handoff="<<status.handoff->error<<" initialization="<<status.handoff->initialization_note<<" event_values="<<status.handoff->event_value_note;
        if(status.controls)line<<" placement="<<PlacementPhaseName(status.controls->placement_phase)
            <<" placement_error="<<status.controls->placement_error<<" gaze="<<GazePhaseName(status.controls->gaze_phase)
            <<" gaze_error="<<status.controls->gaze_error;
        if(!status.look_support.empty())line<<" look_support="<<status.look_support;
        if(status.outfit && status.outfit->applicable) {
            line<<" outfit_avatar="<<status.outfit->avatar<<" outfit_gender="<<status.outfit->gender
                <<" outfit_base="<<status.outfit->base_type<<" outfit_applied="<<status.outfit->applied
                <<" outfit_config_refresh="<<status.outfit->config_refresh_required<<" outfit_refreshed="<<status.outfit->refresh.returned;
            for(const auto& error:status.outfit->errors)line<<" outfit_error="<<error;
        }
        if(status.playback && status.playback->last) {
            const auto& r=*status.playback->last;
            line<<" completion="<<status.playback->completion_note;
            line<<" presentation_phase="<<status.playback->presentation_phase<<" presentation_note="<<status.playback->presentation_note;
            line<<" visual="<<status.playback->visual_note;
            line<<" blink="<<status.playback->blink_note;
            line<<" ready="<<status.playback->ready_note;
            line<<" effects="<<status.playback->effect_note;
            line<<" action="<<r.serial<<" outcome="<<static_cast<int>(r.outcome)<<" detail="<<r.detail;
            std::visit([&](const auto& request) {
                using T=std::decay_t<decltype(request)>;
                if constexpr(std::is_same_v<T,NpcMotion>) line<<" motion_id="<<request.id;
                else if constexpr(std::is_same_v<T,VisualState>)line<<" visual_source="<<request.source<<" flag="<<request.value.flag.value<<" recovery="<<request.recover;
                else if constexpr(std::is_same_v<T,ReadyState>)line<<" ready_source="<<(request.actions?request.actions->source:"")<<" ready_pose="<<request.pose;
                else if constexpr(std::is_same_v<T,ProgramAction>)line<<" program="<<request.program<<" program_action="<<request.action;
                else if constexpr(std::is_same_v<T,BattleIdle>)line<<" battle_idle=true";
                else line<<" state="<<request.name;
            },r.request);
        }
        const auto text=line.str();
        if(last_character_trace_[id]!=text){
            last_character_trace_[id]=text;
            Trace(text+" position="+std::to_string(status.placement.position.x)+","+std::to_string(status.placement.position.y)+","+std::to_string(status.placement.position.z));
        }
    }
    out.binding_errors=binding_errors_;application_.Publish(std::move(out));
}
}
