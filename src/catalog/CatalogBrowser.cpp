#include <hksr/framework/catalog/CatalogBrowser.hpp>
#include <hksr/framework/catalog/ActionSourceStore.hpp>
#include <stdexcept>
#include <type_traits>
#include <algorithm>
#include <chrono>

namespace hksr::framework {
CatalogBrowser::CatalogBrowser(CatalogReaders readers,std::function<void(const CatalogWork&)> report)
    :readers_(std::move(readers)),actions_(readers_.document,readers_.abilities,readers_.visuals,readers_.ready_defaults,readers_.begin_abilities),
     report_(std::move(report)) {actions_.SetSourceStore(readers_.source_store);Publish();}
void CatalogBrowser::Publish() {
    if(source_batch_){publication_dirty_=true;return;}
    publication_dirty_=false;
    for(const auto& entry:source_roots_) {
        const auto view=actions_.Snapshot(entry.second);
        if(source_views_[entry.first]==view)continue;
        auto value=std::make_shared<SourceRequestStatus>(*state_.sources.at(entry.first));
        value->actions=*view;state_.sources[entry.first]=std::move(value);source_views_[entry.first]=view;
    }
    for(const auto& entry:character_sources_) {
        const auto view=actions_.Snapshot(entry.second.roots);
        const auto previous=state_.characters.find(entry.first);
        if(character_views_[entry.first]==view && previous!=state_.characters.end() &&
            previous->second->revision==entry.second.revision)continue;
        state_.characters[entry.first]=std::make_shared<const CharacterActionCatalog>(CharacterActionCatalog{
            entry.second.revision,*view});
        character_views_[entry.first]=view;
    }
    snapshot_=std::make_shared<const CatalogSnapshot>(state_);
}
void CatalogBrowser::RequestActions() {
    if(source_batch_){sources_dirty_=true;return;}
    sources_dirty_=false;
    std::vector<ActionSourceRoot> roots;
    for(const auto& entry:source_roots_)roots.insert(roots.end(),entry.second.begin(),entry.second.end());
    for(const auto& entry:character_sources_)roots.insert(roots.end(),entry.second.roots.begin(),entry.second.roots.end());
    for(const auto& source:pose_sources_)roots.push_back({ActionDomain::Alternate,{api::ActionConfigKind::Battle,source}});
    actions_.Request(std::move(roots));actions_started_=true;
}
void CatalogBrowser::BatchSources(const std::function<void()>& update) {
    ++source_batch_;
    try{update();}catch(...){--source_batch_;throw;}
    --source_batch_;
    if(!source_batch_) {
        if(sources_dirty_)RequestActions();
        if(publication_dirty_)Publish();
    }
}
void CatalogBrowser::Request(bool refresh) {
    if(!state_.requested || refresh) {
        if(!work_since_)work_since_=std::chrono::steady_clock::now();
        table_pending_=true;state_.requested=true;state_.error.clear();
        if(refresh) {
            // Existing requests keep their copied result. Pending/new requests
            // use the refreshed shared table/cache; no UI selection is implied.
            preparation_.reset();
        }
        Publish();
    }
}
std::uint64_t CatalogBrowser::Prepare(BrowseSource source) {
    if(!work_since_)work_since_=std::chrono::steady_clock::now();
    const auto id=++next_source_;
    auto value=std::make_shared<SourceRequestStatus>();value->id=id;value->input=std::move(source);
    state_.sources.emplace(id,std::move(value));pending_sources_.push_back(id);
    Request();Publish();return id;
}
void CatalogBrowser::BeginSource(const BrowseSource& source) {
    std::visit([&](const auto& input) {
        using T=std::decay_t<decltype(input)>;
        if constexpr(std::is_same_v<T,BrowseNpc>) {
            const api::NpcCatalogRow* row=nullptr;unsigned count=0;
            for(const auto& npc:state_.rows->npcs.rows) if(npc.id==input.id) {row=&npc;++count;}
            if(count!=1) throw std::runtime_error(count?"selected NPC ID has multiple rows":"selected NPC ID absent");
            preparation_->Package(SourceCatalog::NpcPackage(*row));
        } else if constexpr(std::is_same_v<T,BrowsePlayer>) preparation_->Player(input.key,input.npc);
        else preparation_->Package(input);
    },source);
}
void CatalogBrowser::AdvanceSource() {
    const auto id=pending_sources_.front();pending_sources_.pop_front();
    auto value=std::make_shared<SourceRequestStatus>(*state_.sources.at(id));
    value->phase=SourceRequestPhase::Preparing;
    try {
        // Reproject this request against the one shared value cache. A turn
        // reads at most one config; round-robin prevents a broad identity scan
        // from blocking an unrelated explicit NPC/package request.
        BeginSource(value->input);preparation_->Advance();value->result=preparation_->Status();
        if(value->result.finished) {
            value->phase=value->result.character?SourceRequestPhase::Ready:SourceRequestPhase::Unresolved;
            if(value->result.character)source_roots_[id]=CharacterPlan::FromPrepared(value->result).action_sources;
        } else pending_sources_.push_back(id);
    } catch(const std::exception& error) {value->phase=SourceRequestPhase::Failed;value->error=error.what();}
    catch(...) {value->phase=SourceRequestPhase::Failed;value->error="unknown source preparation failure";}
    state_.sources[id]=std::move(value);RequestActions();publication_dirty_=true;
}
bool CatalogBrowser::AdvanceOne() {
        if(AdvanceHitInputs())return true;
        if(table_pending_) {
            ++work_.source_steps;
            table_pending_=false;state_.attempted=true;
            api::CharacterCatalog rows;state_.table_error.clear();
            try {rows=readers_.characters();}
            catch(const std::exception& error) {state_.table_error=error.what();}
            catch(...) {state_.table_error="unknown character table failure";}
            if(readers_.source_store)actions_.SetSourceStore(readers_.source_store);
            state_.rows=std::make_shared<const api::CharacterCatalog>(rows);
            preparation_=std::make_unique<SourcePreparation>(std::move(rows),CatalogQueries{
                readers_.identity,[this](const auto& path) {
                    return actions_.Load({api::ActionConfigKind::Battle,path}).presentation;
                }});
            publication_dirty_=true;
            return true;
        }
        const bool action_pending=actions_.ReadingAbilities() || (actions_started_ && !actions_.Finished());
        if(preparation_ && !pending_sources_.empty() && (source_turn_ || !action_pending)) {
            ++work_.source_steps;
            AdvanceSource();source_turn_=false;return true;
        }
        if(action_pending) {++work_.action_steps;actions_.Advance();source_turn_=true;return true;}
        return false;
}
void CatalogBrowser::Advance(std::uint32_t budget_microseconds) {
    if(sources_dirty_ && !source_batch_)RequestActions();
    const auto started=std::chrono::steady_clock::now();const auto revision=actions_.Revision();
    bool advanced=false;
    try {
        do {
            if(!AdvanceOne())break;
            if(!work_since_)work_since_=started;
            advanced=true;
        }while(std::chrono::steady_clock::now()-started<std::chrono::microseconds(budget_microseconds));
    }catch(const std::exception& error){state_.error=error.what();publication_dirty_=true;}
    catch(...){state_.error="unknown catalog query failure";publication_dirty_=true;}
    // Publish one immutable projection per slice, not once per cheap cached
    // request. Individual engine calls remain non-preemptible.
    const bool changed=publication_dirty_ || actions_.Revision()!=revision;
    if(changed)Publish();
    if(advanced || changed) {
        const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count();
        ++work_.slices;work_.total_us+=elapsed;work_.max_us=std::max(work_.max_us,static_cast<std::uint64_t>(elapsed));
    }
    if(work_.slices && hit_pending_.empty() && !table_pending_ && pending_sources_.empty() && !sources_dirty_ &&
        !actions_.ReadingAbilities() && (!actions_started_ || actions_.Finished())) {
        work_.elapsed_us=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now()-work_since_.value_or(started)).count());
        if(report_)try{report_(work_);}catch(...) {} // diagnostics do not own catalog progress
        if(readers_.source_store)readers_.source_store->Flush();
        work_={};work_since_.reset();
    }
}
std::uint64_t CatalogBrowser::RetrySource(std::uint64_t request) {
    const auto found=state_.sources.find(request);
    if(found==state_.sources.end())throw std::invalid_argument("source request absent or released");
    const auto previous=found->second;
    std::vector<std::string> paths;
    for(const auto& candidate:previous->result.battle.candidates) {
        paths.push_back(candidate.character_config);
        RefreshDocument({api::ActionConfigKind::Battle,candidate.character_config});
    }
    if(preparation_)preparation_->InvalidateFailures(paths);
    return Prepare(previous->input);
}
void CatalogBrowser::ReleaseSource(std::uint64_t request) {
    if(!state_.sources.erase(request))return; // repeated release is harmless
    pending_sources_.erase(std::remove(pending_sources_.begin(),pending_sources_.end(),request),pending_sources_.end());
    source_roots_.erase(request);source_views_.erase(request);RequestActions();Publish();
}
void CatalogBrowser::RefreshDocument(const ActionSourceKey& source) {
    if(source.kind==api::ActionConfigKind::Battle){hit_sources_.erase(source.path);hit_errors_.erase(source.path);}
    // Only catalogs referencing this document can have their row identities
    // changed. Unrelated actors remain usable with their existing revision.
    for(auto& entry:character_sources_)
        for(const auto& doc:actions_.View(entry.second.roots).documents)
            if(!(doc.input.source<source) && !(source<doc.input.source)) {entry.second.revision=++revision_;break;}
    actions_.Refresh(source);Publish();
}
void CatalogBrowser::RefreshAbility(const std::string& ability) {
    for(auto& entry:character_sources_)
        for(const auto& row:actions_.View(entry.second.roots).abilities)
            if(row.ability==ability) {entry.second.revision=++revision_;break;}
    actions_.RefreshAbility(ability);Publish();
}
void CatalogBrowser::InspectActions(std::uint64_t character,std::vector<ActionSourceRoot> roots) {
    if(!character)throw std::invalid_argument("action query requires a logical instance ID");
    const auto found=character_sources_.find(character);
    if(found!=character_sources_.end() && found->second.roots.size()==roots.size() &&
        std::equal(roots.begin(),roots.end(),found->second.roots.begin(),[](const auto& a,const auto& b){
            return a.domain==b.domain && !(a.source<b.source) && !(b.source<a.source);
        }))return;
    character_sources_.insert_or_assign(character,CharacterSources{++revision_,std::move(roots)});
    RequestActions();Publish();
}
PlacedAction CatalogBrowser::ResolveAction(std::uint64_t character,const CatalogActionSelection& selection) const {
    const auto found=state_.characters.find(character);
    if(found==state_.characters.end())throw std::runtime_error("request this instance's action catalog first");
    return ResolveCatalogAction(*found->second,selection);
}
std::optional<PoseCommand> CatalogBrowser::ResolvePose(const std::vector<ActionSourceRoot>& roots,const PoseSelection& selection) {
    const auto source=std::visit([](const auto& key){return key.source;},selection);
    const std::vector<ActionSourceRoot> selected{{ActionDomain::Alternate,{api::ActionConfigKind::Battle,source}}};
    if(!std::any_of(roots.begin(),roots.end(),[&](const auto& root) {
        return root.domain==ActionDomain::Alternate && root.source.kind==api::ActionConfigKind::Battle && root.source.path==source;
    }))throw std::invalid_argument("pose configuration does not belong to this actor: "+source);
    if(pose_sources_.insert(source).second)RequestActions();
    if(std::holds_alternative<ReactionSelection>(selection))InspectVisuals(selected);
    // Read the existing per-source value cache. No engine calls or roster scan
    // here; Advance owns pending reads and shares them with ordinary menus.
    const auto view=actions_.Snapshot(selected);
    if(const auto* ready=std::get_if<ReadyPoseSelection>(&selection)) {
        if(!view->Ready(ActionDomain::Alternate))return {};
        std::optional<PoseCommand> result;
        for(const auto& set:view->ready_actions)if(set->source==source)
            for(std::size_t i=0;i<set->poses.size();++i) {
                const auto& pose=set->poses[i];
                if(pose.skill_index!=ready->skill || pose.variant_index!=ready->variant)continue;
                if(result)throw std::runtime_error("ambiguous ready pose source");
                result=ReadyState{set,i};
            }
        if(!result)throw std::runtime_error("ready pose absent from bound source");
        return result;
    }
    const auto& reaction=std::get<ReactionSelection>(selection);
    for(const auto& values:view->visual_sources)if(values.character_config==source) {
        const auto resolved=ResolveVisualState(values,reaction.flag,reaction.recover);
        if(!resolved)throw std::runtime_error("reaction absent from bound source");
        return *resolved;
    }
    if(!view->finished)return {};
    throw std::runtime_error("reaction configuration unavailable");
}
std::optional<HitCatalogInputs> CatalogBrowser::ResolveHitInputs(const std::string& source,bool visuals) {
    if(source.empty())throw std::invalid_argument("receiver character source absent");
    if(const auto failed=hit_errors_.find(source);failed!=hit_errors_.end())throw std::runtime_error(failed->second);
    const auto found=hit_sources_.find(source);
    if(found!=hit_sources_.end() && found->second.character && hit_weights_ && hit_names_ && hit_animations_ &&
        (!visuals || found->second.visuals)) {
        auto out=found->second;out.weights=hit_weights_;out.names=hit_names_;out.animations=hit_animations_;return out;
    }
    hit_pending_[source]=hit_pending_[source] || visuals;return {};
}
bool CatalogBrowser::AdvanceHitInputs() {
    if(hit_pending_.empty())return false;
    const auto source=hit_pending_.begin()->first;const bool visuals=hit_pending_.begin()->second;
    try {
        // One focused read per work item, no ActionCatalog::Request or ability
        // traversal. Shared inputs reuse the same cache as the action menus.
        if(!hit_weights_)hit_weights_=std::make_shared<const api::HitAnimationDefaults>(readers_.hit_weights());
        else if(!hit_names_)hit_names_=std::make_shared<const api::VisualReactionInput>(readers_.hit_names());
        else if(!hit_animations_)hit_animations_=std::make_shared<const api::ReadyAnimationDefaults>(actions_.LoadReadyDefaults());
        else if(!hit_sources_[source].character)
            hit_sources_[source].character=std::make_shared<const api::CharacterHitInput>(readers_.hit_character(source));
        else if(visuals && !hit_sources_[source].visuals)
            hit_sources_[source].visuals=std::make_shared<const api::VisualConfigSources>(actions_.LoadVisuals(source));
        else hit_pending_.erase(source);
    }catch(const std::exception& error){hit_errors_[source]=error.what();hit_pending_.erase(source);}
    ++work_.action_steps;return true;
}
bool CatalogBrowser::InspectVisuals(const std::vector<ActionSourceRoot>& roots) {
    bool changed=false;
    for(const auto& root:roots)if(root.source.kind==api::ActionConfigKind::Battle)
        changed=actions_.RequestVisuals(root.source.path) || changed;
    if(changed){actions_started_=true;Publish();}
    return changed;
}
void CatalogBrowser::ForgetCharacter(std::uint64_t id) {
    character_sources_.erase(id);character_views_.erase(id);state_.characters.erase(id);RequestActions();Publish();
}
CharacterPlan CatalogBrowser::Plan(std::uint64_t request,std::shared_ptr<const ModelAssetInput> payload,std::string look) const {
    const auto found=state_.sources.find(request);
    if(found==state_.sources.end())throw std::invalid_argument("source request absent or released");
    if(found->second->phase!=SourceRequestPhase::Ready)
        throw std::runtime_error("source request has no prepared character plan");
    auto plan=CharacterPlan::FromPrepared(found->second->result,std::move(payload),std::move(look));
    plan.source_intent=found->second->input;return plan;
}
api::NpcMotionSources CatalogBrowser::NpcMotions(void* component) const {
    if(!readers_.npc_motions) throw std::runtime_error("NPC motion reader unavailable");
    return readers_.npc_motions(component);
}
}
