#include <hksr/framework/catalog/CatalogBrowser.hpp>
#include <stdexcept>
#include <type_traits>
#include <algorithm>
#include <chrono>

namespace hksr::framework {
CatalogBrowser::CatalogBrowser(CatalogReaders readers)
    :readers_(std::move(readers)),actions_(readers_.document,readers_.abilities,readers_.visuals,readers_.ready_defaults,readers_.begin_abilities) {Publish();}
void CatalogBrowser::Publish() {
    for(const auto& entry:source_roots_) {
        auto value=std::make_shared<SourceRequestStatus>(*state_.sources.at(entry.first));
        value->actions=actions_.View(entry.second);state_.sources[entry.first]=std::move(value);
    }
    for(const auto& entry:character_sources_)
        state_.characters[entry.first]=std::make_shared<const CharacterActionCatalog>(CharacterActionCatalog{
            entry.second.revision,actions_.View(entry.second.roots)});
    snapshot_=std::make_shared<const CatalogSnapshot>(state_);
}
void CatalogBrowser::RequestActions() {
    std::vector<ActionSourceRoot> roots;
    for(const auto& entry:source_roots_)roots.insert(roots.end(),entry.second.begin(),entry.second.end());
    for(const auto& entry:character_sources_)roots.insert(roots.end(),entry.second.roots.begin(),entry.second.roots.end());
    actions_.Request(std::move(roots));actions_started_=true;
}
void CatalogBrowser::Request(bool refresh) {
    if(!state_.requested || refresh) {
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
    state_.sources[id]=std::move(value);RequestActions();
}
bool CatalogBrowser::AdvanceOne() {
        if(table_pending_) {
            table_pending_=false;state_.attempted=true;
            api::CharacterCatalog rows;state_.table_error.clear();
            try {rows=readers_.characters();}
            catch(const std::exception& error) {state_.table_error=error.what();}
            catch(...) {state_.table_error="unknown character table failure";}
            state_.rows=std::make_shared<const api::CharacterCatalog>(rows);
            preparation_=std::make_unique<SourcePreparation>(std::move(rows),CatalogQueries{
                readers_.identity,[this](const auto& path) {
                    return actions_.Load({api::ActionConfigKind::Battle,path}).presentation;
                }});
            return true;
        }
        const bool action_pending=actions_.ReadingAbilities() || (actions_started_ && !actions_.Status().finished);
        if(preparation_ && !pending_sources_.empty() && (source_turn_ || !action_pending)) {
            AdvanceSource();source_turn_=false;return true;
        }
        if(action_pending) {actions_.Advance();source_turn_=true;return true;}
        return false;
}
void CatalogBrowser::Advance(std::uint32_t budget_microseconds) {
    const auto started=std::chrono::steady_clock::now();bool changed=false;
    try {
        do {
            if(!AdvanceOne())break;
            changed=true;
        }while(std::chrono::steady_clock::now()-started<std::chrono::microseconds(budget_microseconds));
    }catch(const std::exception& error){state_.error=error.what();changed=true;}
    catch(...){state_.error="unknown catalog query failure";changed=true;}
    // Publish one immutable projection per slice, not once per cheap cached
    // request. Individual engine calls remain non-preemptible.
    if(changed)Publish();
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
    source_roots_.erase(request);RequestActions();Publish();
}
void CatalogBrowser::RefreshDocument(const ActionSourceKey& source) {
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
void CatalogBrowser::ForgetCharacter(std::uint64_t id) {
    character_sources_.erase(id);state_.characters.erase(id);Publish();
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
