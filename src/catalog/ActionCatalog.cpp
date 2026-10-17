#include <hksr/framework/catalog/ActionCatalog.hpp>
#include <set>
#include <algorithm>
#include <stdexcept>

namespace hksr::framework {
ActionCatalog::ActionCatalog(api::OfficialCatalog& api,api::TypeRelationships& types):ActionCatalog([&api](auto kind,const auto& path) {
    return api.ReadActionConfig(kind,path);
},{},[&api](const auto& path) {
    return api.ReadVisualConfigSources(path);
},[&api]{return api.ReadReadyAnimationDefaults();},[&api,&types](const auto& names){return api.BeginAbilitySources(names,types);}){}
void ActionCatalog::Request(std::vector<ActionSourceRoot> roots) {
    auto key=[](const ActionSourceRoot& r){return std::tie(r.domain,r.source.kind,r.source.path);};
    std::sort(roots.begin(),roots.end(),[&](const auto& a,const auto& b){return key(a)<key(b);});
    roots.erase(std::unique(roots.begin(),roots.end(),[&](const auto& a,const auto& b){return key(a)==key(b);}),roots.end());
    if(revision_ && roots.size()==requested_.size() && std::equal(roots.begin(),roots.end(),requested_.begin(),[&](const auto& a,const auto& b){return key(a)==key(b);}))return;
    requested_=std::move(roots);
    std::set<std::tuple<ActionDomain,api::ActionConfigKind,std::string>> active;
    for(const auto& root:requested_)active.insert(key(root));
    for(auto it=views_.begin();it!=views_.end();) {
        if(std::any_of(it->first.begin(),it->first.end(),[&](const auto& root){return !active.count(root);}))it=views_.erase(it);
        else ++it;
    }
    for(auto it=discovery_.begin();it!=discovery_.end();) {
        if(!active.count(it->first))it=discovery_.erase(it);else ++it;
    }
    Rebuild();
}
const api::ActionConfigInput* ActionCatalog::Document(const ActionSourceKey& source) const {
    const auto found=documents_.find(source); return found==documents_.end()?nullptr:&found->second;
}
void ActionCatalog::InvalidateDocument(const ActionSourceKey& source) {
    for(auto it=discovery_.begin();it!=discovery_.end();) {
        if(it->second.used_documents.count(source) || it->second.used_visuals.count(source.path))it=discovery_.erase(it);
        else ++it;
    }
    for(auto it=views_.begin();it!=views_.end();) {
        if(it->second.documents.count(source) || it->second.visuals.count(source.path))it=views_.erase(it);
        else ++it;
    }
}
void ActionCatalog::InvalidateAbility(const std::string& ability) {
    for(auto it=discovery_.begin();it!=discovery_.end();) {
        if(it->second.used_abilities.count(ability))it=discovery_.erase(it);else ++it;
    }
    for(auto it=views_.begin();it!=views_.end();) {
        if(it->second.abilities.count(ability))it=views_.erase(it);
        else ++it;
    }
}
void ActionCatalog::Refresh(const ActionSourceKey& source) {
    documents_.erase(source);visual_sources_.erase(source.path);InvalidateDocument(source);Rebuild();
}
void ActionCatalog::RefreshAbility(const std::string& ability) {
    if(ability_read_ && std::find(reading_abilities_.begin(),reading_abilities_.end(),ability)!=reading_abilities_.end()) {
        ability_read_->Cancel();discard_read_=true;
    }
    ability_sources_.erase(ability);retirements_.erase(ability);InvalidateAbility(ability);Rebuild();
}
bool ActionCatalog::CancelPending() {
    if(!ability_read_)return true;
    ability_read_->Cancel();discard_read_=true;
    ability_read_->Advance(2000);
    if(!ability_read_->Done())return false;
    ability_read_.reset();reading_abilities_.clear();discard_read_=false;Rebuild();return true;
}
const api::ActionConfigInput& ActionCatalog::Load(const ActionSourceKey& source) {
    if(const auto* cached=Document(source)) return *cached;
    api::ActionConfigInput result; result.kind=source.kind; result.path=source.path; result.presentation.path=source.path;
    try {
        auto read=read_(source.kind,source.path);
        if(read.kind!=source.kind || read.path!=source.path) throw std::runtime_error("reader returned another action configuration");
        result=std::move(read);
    } catch(const std::exception& error) { result.issues.push_back({-1,"read document",error.what()}); }
    catch(...) { result.issues.push_back({-1,"read document","unknown failure"}); }
    const auto found=documents_.emplace(source,std::move(result)).first;
    InvalidateDocument(source);Rebuild();return found->second;
}
void ActionCatalog::StoreAbilities(const std::vector<std::string>& names,api::AbilitySources result) {
    std::map<std::string,api::AbilitySources> parts;
    for(const auto& name:names)parts.emplace(name,api::AbilitySources{});
    auto distribute=[&](auto& entries,auto member) {
        for(auto& entry:entries)if(auto it=parts.find(entry.ability);it!=parts.end())
            (it->second.*member).push_back(std::move(entry));
    };
    distribute(result.animations,&api::AbilitySources::animations);
    distribute(result.waits,&api::AbilitySources::waits);
    distribute(result.removals,&api::AbilitySources::removals);
    distribute(result.references,&api::AbilitySources::references);
    distribute(result.skill_mappings,&api::AbilitySources::skill_mappings);
    distribute(result.skill_continuations,&api::AbilitySources::skill_continuations);
    for(const auto& issue:result.issues) {
        if(issue.ability.empty())for(auto& part:parts)part.second.issues.push_back(issue);
        else if(auto it=parts.find(issue.ability);it!=parts.end())it->second.issues.push_back(issue);
    }
    for(auto& part:parts) {
        InvalidateAbility(part.first);
        retirements_.insert_or_assign(part.first,LinkActionRetirements(part.second));
        ability_sources_.insert_or_assign(part.first,std::move(part.second));
    }
}
void ActionCatalog::Advance() {
    if(advancing_) return;
    // Documents/defaults do not depend on completion of another source's
    // ability cursor. Both use the same outer official-frame budget.
    if(!pending_.empty()) {
        const auto source=pending_.front();advancing_=true;
        Load(source);advancing_=false;return;
    }
    if(pending_ready_) {
        advancing_=true;api::ReadyAnimationDefaults result;
        try{result=read_ready_();}catch(const std::exception& e){result.transition_in.error=e.what();}
        ready_defaults_=std::move(result);
        for(auto it=views_.begin();it!=views_.end();) {
            if(it->second.ready)it=views_.erase(it);else ++it;
        }
        for(auto it=discovery_.begin();it!=discovery_.end();) {
            if(it->second.uses_ready)it=discovery_.erase(it);else ++it;
        }
        advancing_=false;Rebuild();return;
    }
    if(!pending_visuals_.empty()) {
        const auto path=pending_visuals_.front();advancing_=true;api::VisualConfigSources result;result.character_config=path;
        try {result=read_visual_(path);}
        catch(const std::exception& e){result.character.issues.push_back({-1,"visual sources",e.what()});}
        catch(...){result.character.issues.push_back({-1,"visual sources","unknown failure"});}
        visual_sources_.insert_or_assign(path,std::move(result));
        InvalidateDocument({api::ActionConfigKind::Battle,path});advancing_=false;Rebuild();return;
    }
    {
        if(!ability_read_ && pending_abilities_.empty()) return;
        auto abilities=ability_read_?reading_abilities_:pending_abilities_;
        advancing_=true; api::AbilitySources result;
        try {
            if(begin_ability_) {
                if(!ability_read_) {
                    ability_read_=begin_ability_(abilities);reading_abilities_=abilities;discard_read_=false;
                    if(!ability_read_)throw std::runtime_error("ability reader not created");
                }
                ability_read_->Advance(2000);
                if(!discard_read_)if(auto* progressive=dynamic_cast<api::ProgressiveAbilitySourceRead*>(ability_read_.get())) {
                    auto completed=progressive->DrainCompleted();
                    if(!completed.abilities.empty()) {
                        const std::set<std::string> done(completed.abilities.begin(),completed.abilities.end());
                        StoreAbilities(completed.abilities,std::move(completed.sources));
                        reading_abilities_.erase(std::remove_if(reading_abilities_.begin(),reading_abilities_.end(),
                            [&](const auto& name){return done.count(name)!=0;}),reading_abilities_.end());
                        abilities=reading_abilities_;Rebuild();
                    }
                }
                if(!ability_read_->Done()){advancing_=false;return;}
                result=ability_read_->Take();ability_read_.reset();reading_abilities_.clear();
                if(discard_read_){discard_read_=false;advancing_=false;Rebuild();return;}
            }else result=read_ability_(abilities);
        }
        catch(const std::exception& error) {
            if(ability_read_){ability_read_->Cancel();discard_read_=true;}
            result.issues.push_back({{},error.what(),{}});
        }
        catch(...) {
            if(ability_read_){ability_read_->Cancel();discard_read_=true;}
            result.issues.push_back({{},"unknown discovery failure",{}});
        }
        StoreAbilities(abilities,std::move(result));
        advancing_=false; Rebuild(); return;
    }
}
void ActionCatalog::Rebuild() {
    ++revision_;
    pending_.clear();pending_abilities_.clear();pending_visuals_.clear();pending_ready_=false;
    std::set<ActionSourceKey> documents;
    std::set<std::string> abilities,visuals;
    // A newly read input only invalidates the root closures that depend on it.
    // Scheduling must not walk every other character's cached graph again.
    for(const auto& root:requested_) {
        const auto key=std::make_tuple(root.domain,root.source.kind,root.source.path);
        auto found=discovery_.find(key);
        if(found==discovery_.end())found=discovery_.emplace(key,Project({root},false)).first;
        const auto& view=found->second;
        for(const auto& source:view.documents)if(documents.insert(source).second)pending_.push_back(source);
        for(const auto& source:view.abilities)if(abilities.insert(source).second)pending_abilities_.push_back(source);
        for(const auto& source:view.visuals)if(visuals.insert(source).second)pending_visuals_.push_back(source);
        pending_ready_|=view.ready;
    }
    finished_=pending_.empty() && pending_abilities_.empty() && pending_visuals_.empty() && !pending_ready_ && !ability_read_;
}
const ActionCatalogStatus& ActionCatalog::Status() const {
    if(status_revision_!=revision_) {
        status_=Project(requested_).status;status_.finished=finished_;status_revision_=revision_;
    }
    return status_;
}
ActionCatalogStatus ActionCatalog::View(const std::vector<ActionSourceRoot>& roots) const {
    return *Snapshot(roots);
}
std::shared_ptr<const ActionCatalogStatus> ActionCatalog::Snapshot(const std::vector<ActionSourceRoot>& roots) const {
    ViewKey key;
    // Order is part of row identity; do not reorder a caller's projection.
    for(const auto& root:roots)key.emplace_back(root.domain,root.source.kind,root.source.path);
    if(const auto found=views_.find(key);found!=views_.end())return found->second.value;
    auto projection=Project(roots);
    auto value=std::make_shared<const ActionCatalogStatus>(std::move(projection.status));
    views_.emplace(std::move(key),CachedView{value,std::move(projection.used_documents),
        std::move(projection.used_abilities),std::move(projection.used_visuals),projection.uses_ready});
    return value;
}
ActionCatalog::Projection ActionCatalog::Project(const std::vector<ActionSourceRoot>& roots,bool values) const {
    Projection result;
    auto& status=result.status;auto& pending=result.documents;
    auto& pending_abilities=result.abilities;auto& pending_visuals=result.visuals;
    auto& pending_ready=result.ready;
    auto queue=roots;
    std::set<std::tuple<ActionDomain,api::ActionConfigKind,std::string>> visited;
    std::set<ActionSourceKey> scheduled;
    std::set<std::pair<ActionDomain,std::string>> seen_abilities;
    std::set<std::string> scheduled_abilities;
    std::set<std::string> seen_visuals;
    std::map<std::pair<ActionDomain,std::string>,std::size_t> proposal_indices;
    auto propose=[&](ActionDomain domain,const std::string& state,ActionReference reference) {
        if(state.empty()) return;
        auto [found,inserted]=proposal_indices.emplace(std::make_pair(domain,state),status.proposals.size());
        if(inserted)status.proposals.push_back({domain,state,{}});
        status.proposals[found->second].references.push_back(std::move(reference));
    };
    for(std::size_t i=0;i<queue.size();++i) {
        const auto input=queue[i]; // reference traversal may reallocate the queue
        if(!visited.emplace(input.domain,input.source.kind,input.source.path).second) continue;
        result.used_documents.insert(input.source);
        if(input.source.kind==api::ActionConfigKind::Battle && read_visual_ && seen_visuals.insert(input.source.path).second) {
            result.used_visuals.insert(input.source.path);
            const auto found=visual_sources_.find(input.source.path);
            if(found==visual_sources_.end())pending_visuals.push_back(input.source.path);
            else if(values) {
                const auto& sources=found->second;status.visual_sources.push_back(sources);
                if(sources.context_available) {
                    // Whole rows override by official flag, not by body name.
                    // Preserve source/index for two flags sharing one animation.
                    std::set<int> selected;
                    for(const auto* config:{&sources.defaults,&sources.character})for(const auto& row:config->behaviors.rows)
                        if(row.flag.available)selected.insert(row.flag.value);
                    for(const auto flag:selected)if(auto value=ResolveVisualState(sources,flag))status.visual_states.push_back(std::move(*value));
                }
            }
        }
        if(input.source.kind==api::ActionConfigKind::Battle && read_visual_ && !visual_sources_.count(input.source.path))
            status.pending_domains.insert(input.domain);
        ActionDocumentStatus document;if(values)document.input=input;
        if(input.source.path.empty()) {
            if(values) {document.attempted=true;document.issues.push_back({-1,"source path","empty configuration path"});
                status.documents.push_back(std::move(document));}continue;
        }
        const auto* value=Document(input.source);
        if(!value) {
            status.pending_domains.insert(input.domain);
            if(scheduled.insert(input.source).second) pending.push_back(input.source);
            if(values)status.documents.push_back(std::move(document));continue;
        }
        if(values){document.attempted=true;document.loaded=value->loaded;document.issues=value->issues;}
        if(value->kind==api::ActionConfigKind::Battle && input.domain==ActionDomain::Alternate) {
            if(read_ready_ && !ready_defaults_){pending_ready=true;status.pending_domains.insert(input.domain);}
            if(read_ready_)result.uses_ready=true;
            ReadyActions ready;
            if(values){ready=BuildReadyActions(*value,ready_defaults_.value_or(api::ReadyAnimationDefaults{}));
                ResolveReadyPreparations(ready,value->abilities,ability_sources_);}
            if(value->elation_config.available && !value->elation_config.value.empty()) {
                const ActionSourceKey linked{api::ActionConfigKind::Elation,value->elation_config.value};
                queue.push_back({input.domain,linked});
                if(values)if(const auto* elation=Document(linked))AppendElationReadyPoses(ready,*elation);
            }
            if(values && !ready.poses.empty())status.ready_actions.push_back(std::make_shared<const ReadyActions>(std::move(ready)));
        }
        auto note=[&](const char* field,bool available,const std::string& error) {
            if(!available && !error.empty()) document.issues.push_back({-1,field,error});
        };
        if(values && value->kind!=api::ActionConfigKind::AnimationEvent) {
            note("InitAnimStateName",value->presentation.initial_state.available,value->presentation.initial_state.error);
            if(value->presentation.initial_state.available)
                propose(input.domain,value->presentation.initial_state.value,{input.source,"InitAnimStateName",{}});
            note("AnimEventConfigList",value->presentation.event_files.available,value->presentation.event_files.error);
            note("DisableAnimEventLayers",value->presentation.disabled_event_layers.available,value->presentation.disabled_event_layers.error);
        }
        // A partially readable document contributes its proven literals, even
        // when an independent field/branch has issues.
        if(values)for(const auto& state:value->states) {
            propose(input.domain,state.value,{input.source,state.field,{}});
        }
        for(const auto& ability:value->abilities) {
            if(ability.value.empty()) continue;
            std::vector<std::string> chain{ability.value};std::set<std::string> chain_seen{ability.value};
            for(std::size_t ai=0;ai<chain.size();++ai) {
            const auto name=chain[ai];
            result.used_abilities.insert(name);
            const auto cached=ability_sources_.find(name);
            if(seen_abilities.emplace(input.domain,name).second) {
                AbilityDocumentStatus task;if(values){task.domain=input.domain;task.ability=name;}
                task.reader_available=bool(read_ability_) || bool(begin_ability_); task.attempted=cached!=ability_sources_.end();
                if(task.attempted && values) {
                    task.issues=cached->second.issues;
                    task.removal_sources=cached->second.removals.size();
                    task.linked_retirements=retirements_.at(name).size();
                }
                else if(!task.attempted && task.reader_available) {
                    status.pending_domains.insert(input.domain);
                    if(scheduled_abilities.insert(name).second) pending_abilities.push_back(name);
                } else if(!task.attempted && values)task.issues.push_back({name,"ability reader unavailable",{}});
                if(values)status.abilities.push_back(std::move(task));
            }
            if(values && cached!=ability_sources_.end()) for(const auto& animation:cached->second.animations) {
                if(animation.ability!=name) continue;
                if(animation.state.available) propose(input.domain,animation.state.value,{input.source,ability.field,animation,ability.value});
            }
            if(cached!=ability_sources_.end())for(const auto& ref:cached->second.references)
                if(ref.enabled.available && ref.enabled.value && ref.target.available && !ref.target.value.empty() && chain_seen.insert(ref.target.value).second)
                    chain.push_back(ref.target.value);
            }
        }
        auto add_file=[&](const std::string& file) {
            if(!file.empty()) queue.push_back({input.domain,{api::ActionConfigKind::AnimationEvent,file}});
        };
        if(value->kind==api::ActionConfigKind::AnimationEvent) {
            if(value->referenced_event_file.available) add_file(value->referenced_event_file.value);
        } else if(value->presentation.event_files.available) {
            for(const auto& file:value->presentation.event_files.values) add_file(file);
        }
        if(values)status.documents.push_back(std::move(document));
    }
    // Resolve once per distinct ability/domain, not once per reference field.
    if(values)for(const auto& key:seen_abilities)if(key.first==ActionDomain::Alternate) {
        const auto found=ability_sources_.find(key.second);if(found==ability_sources_.end())continue;
        for(const auto& retirement:retirements_.at(key.second)) {
            const auto proposal=proposal_indices.find({key.first,retirement.state});
            if(proposal!=proposal_indices.end())status.proposals[proposal->second].retirements.push_back(retirement);
        }
    }
    status.finished=pending.empty() && pending_abilities.empty() && pending_visuals.empty() && !pending_ready;
    return result;
}
}
