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
    requested_=std::move(roots);Rebuild();
}
const api::ActionConfigInput* ActionCatalog::Document(const ActionSourceKey& source) const {
    const auto found=documents_.find(source); return found==documents_.end()?nullptr:&found->second;
}
void ActionCatalog::Refresh(const ActionSourceKey& source) { documents_.erase(source);visual_sources_.erase(source.path); Rebuild(); }
void ActionCatalog::RefreshAbility(const std::string& ability) {
    if(ability_read_ && std::find(reading_abilities_.begin(),reading_abilities_.end(),ability)!=reading_abilities_.end()) {
        ability_read_->Cancel();discard_read_=true;
    }
    ability_sources_.erase(ability); Rebuild();
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
    Rebuild();return found->second;
}
void ActionCatalog::Advance() {
    if(advancing_) return;
    if(!ability_read_ && pending_.empty() && pending_abilities_.empty() && pending_visuals_.empty() && pending_ready_) {
        advancing_=true;api::ReadyAnimationDefaults result;
        try{result=read_ready_();}catch(const std::exception& e){result.transition_in.error=e.what();}
        ready_defaults_=std::move(result);advancing_=false;Rebuild();return;
    }
    if(!ability_read_ && pending_.empty() && pending_abilities_.empty() && !pending_visuals_.empty()) {
        const auto path=pending_visuals_.front();advancing_=true;api::VisualConfigSources result;result.character_config=path;
        try {result=read_visual_(path);}
        catch(const std::exception& e){result.character.issues.push_back({-1,"visual sources",e.what()});}
        catch(...){result.character.issues.push_back({-1,"visual sources","unknown failure"});}
        visual_sources_.insert_or_assign(path,std::move(result));advancing_=false;Rebuild();return;
    }
    if(ability_read_ || pending_.empty()) {
        if(!ability_read_ && pending_abilities_.empty()) return;
        const auto abilities=ability_read_?reading_abilities_:pending_abilities_;
        advancing_=true; api::AbilitySources result;
        try {
            if(begin_ability_) {
                if(!ability_read_) {
                    ability_read_=begin_ability_(abilities);reading_abilities_=abilities;discard_read_=false;
                    if(!ability_read_)throw std::runtime_error("ability reader not created");
                }
                ability_read_->Advance(2000);
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
        for(const auto& ability:abilities) {
            api::AbilitySources part;
            for(const auto& entry:result.animations) if(entry.ability==ability) part.animations.push_back(entry);
            for(const auto& entry:result.waits)if(entry.ability==ability)part.waits.push_back(entry);
            for(const auto& entry:result.removals)if(entry.ability==ability)part.removals.push_back(entry);
            for(const auto& entry:result.references)if(entry.ability==ability)part.references.push_back(entry);
            for(const auto& entry:result.skill_mappings)if(entry.ability==ability)part.skill_mappings.push_back(entry);
            for(const auto& entry:result.skill_continuations)if(entry.ability==ability)part.skill_continuations.push_back(entry);
            for(const auto& issue:result.issues) if(issue.ability.empty() || issue.ability==ability) part.issues.push_back(issue);
            ability_sources_.insert_or_assign(ability,std::move(part));
        }
        advancing_=false; Rebuild(); return;
    }
    const auto source=pending_.front(); advancing_=true;
    Load(source); advancing_=false;
    // Load already rebuilds the current intent, including synchronous changes.
}
void ActionCatalog::Rebuild() {
    ++revision_;
    auto view=Project(requested_);
    status_=std::move(view.status);pending_=std::move(view.documents);
    pending_abilities_=std::move(view.abilities);pending_visuals_=std::move(view.visuals);
    pending_ready_=view.ready;
    status_.finished=status_.finished && !ability_read_;
}
ActionCatalogStatus ActionCatalog::View(const std::vector<ActionSourceRoot>& roots) const {
    return Project(roots).status;
}
ActionCatalog::Projection ActionCatalog::Project(const std::vector<ActionSourceRoot>& roots) const {
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
    auto propose=[&](ActionDomain domain,const std::string& state,ActionReference reference) {
        if(state.empty()) return;
        auto found=std::find_if(status.proposals.begin(),status.proposals.end(),[&](const auto& p) { return p.domain==domain && p.state==state; });
        if(found==status.proposals.end()) {
            status.proposals.push_back({domain,state,{}}); found=std::prev(status.proposals.end());
        }
        found->references.push_back(std::move(reference));
    };
    for(std::size_t i=0;i<queue.size();++i) {
        const auto input=queue[i]; // reference traversal may reallocate the queue
        if(!visited.emplace(input.domain,input.source.kind,input.source.path).second) continue;
        if(input.source.kind==api::ActionConfigKind::Battle && read_visual_ && seen_visuals.insert(input.source.path).second) {
            const auto found=visual_sources_.find(input.source.path);
            if(found==visual_sources_.end())pending_visuals.push_back(input.source.path);
            else {
                const auto& sources=found->second;status.visual_sources.push_back(sources);
                if(sources.context_available) {
                    // Whole rows override by official flag, not by body name.
                    // Preserve source/index for two flags sharing one animation.
                    std::map<int,VisualState> selected;
                    for(const auto* config:{&sources.defaults,&sources.character})for(const auto& row:config->behaviors.rows)
                        if(row.flag.available) {
                            VisualState request;request.source=config->source;request.value=row;
                            request.expression_source=sources.defaults.source;
                            request.expression_transition=sources.expression_transition;
                            request.expression={true,"",{}};
                            if(!sources.defaults.loaded || !sources.defaults.behaviors.enumerated)
                                request.expression={false,"","global expression table unread"};
                            else for(const auto& global:sources.defaults.behaviors.rows)
                                if(global.flag.available && global.flag.value==row.flag.value)request.expression=global.emotion;
                            if(sources.blink_source.available && sources.blink_suppressed_expression.available && request.expression.available &&
                                request.expression.value==sources.blink_suppressed_expression.value)request.blink_source=sources.blink_source.value;
                            selected.insert_or_assign(row.flag.value,std::move(request));
                        }
                    for(auto& entry:selected)status.visual_states.push_back(std::move(entry.second));
                }
            }
        }
        ActionDocumentStatus document; document.input=input;
        if(input.source.path.empty()) {
            document.attempted=true; document.issues.push_back({-1,"source path","empty configuration path"});
            status.documents.push_back(std::move(document)); continue;
        }
        const auto* value=Document(input.source);
        if(!value) {
            if(scheduled.insert(input.source).second) pending.push_back(input.source);
            status.documents.push_back(std::move(document)); continue;
        }
        document.attempted=true; document.loaded=value->loaded; document.issues=value->issues;
        if(value->kind==api::ActionConfigKind::Battle && input.domain==ActionDomain::Alternate) {
            if(read_ready_ && !ready_defaults_)pending_ready=true;
            auto ready=BuildReadyActions(*value,ready_defaults_.value_or(api::ReadyAnimationDefaults{}));
            ResolveReadyPreparations(ready,value->abilities,ability_sources_);
            if(value->elation_config.available && !value->elation_config.value.empty()) {
                const ActionSourceKey linked{api::ActionConfigKind::Elation,value->elation_config.value};
                queue.push_back({input.domain,linked});
                if(const auto* elation=Document(linked))AppendElationReadyPoses(ready,*elation);
            }
            if(!ready.poses.empty())status.ready_actions.push_back(std::make_shared<const ReadyActions>(std::move(ready)));
        }
        auto note=[&](const char* field,bool available,const std::string& error) {
            if(!available && !error.empty()) document.issues.push_back({-1,field,error});
        };
        if(value->kind!=api::ActionConfigKind::AnimationEvent) {
            note("InitAnimStateName",value->presentation.initial_state.available,value->presentation.initial_state.error);
            if(value->presentation.initial_state.available)
                propose(input.domain,value->presentation.initial_state.value,{input.source,"InitAnimStateName",{}});
            note("AnimEventConfigList",value->presentation.event_files.available,value->presentation.event_files.error);
            note("DisableAnimEventLayers",value->presentation.disabled_event_layers.available,value->presentation.disabled_event_layers.error);
        }
        // A partially readable document contributes its proven literals, even
        // when an independent field/branch has issues.
        for(const auto& state:value->states) {
            propose(input.domain,state.value,{input.source,state.field,{}});
        }
        for(const auto& ability:value->abilities) {
            if(ability.value.empty()) continue;
            std::vector<std::string> chain{ability.value};std::set<std::string> chain_seen{ability.value};
            for(std::size_t ai=0;ai<chain.size();++ai) {
            const auto name=chain[ai];
            const auto cached=ability_sources_.find(name);
            if(seen_abilities.emplace(input.domain,name).second) {
                AbilityDocumentStatus task; task.domain=input.domain; task.ability=name;
                task.reader_available=bool(read_ability_) || bool(begin_ability_); task.attempted=cached!=ability_sources_.end();
                if(task.attempted) {
                    task.issues=cached->second.issues;
                    task.removal_sources=cached->second.removals.size();
                    task.linked_retirements=LinkActionRetirements(cached->second).size();
                }
                else if(task.reader_available) {
                    if(scheduled_abilities.insert(name).second) pending_abilities.push_back(name);
                } else task.issues.push_back({name,"ability reader unavailable",{}});
                status.abilities.push_back(std::move(task));
            }
            if(cached!=ability_sources_.end()) for(const auto& animation:cached->second.animations) {
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
        status.documents.push_back(std::move(document));
    }
    // Resolve once per distinct ability/domain, not once per reference field.
    for(const auto& key:seen_abilities)if(key.first==ActionDomain::Alternate) {
        const auto found=ability_sources_.find(key.second);if(found==ability_sources_.end())continue;
        for(auto& retirement:LinkActionRetirements(found->second)) {
            const auto proposal=std::find_if(status.proposals.begin(),status.proposals.end(),[&](const auto& p){
                return p.domain==key.first && p.state==retirement.state;
            });
            if(proposal!=status.proposals.end())proposal->retirements.push_back(std::move(retirement));
        }
    }
    status.finished=pending.empty() && pending_abilities.empty() && pending_visuals.empty() && !pending_ready;
    return result;
}
}
