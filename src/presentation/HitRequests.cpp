#include <hksr/framework/presentation/HitRequests.hpp>
#include <hksr/entity/LifecycleHooks.hpp>
#include <hksr/model/ProfileInputs.hpp>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace hksr::framework {
namespace {
void Text(const std::string& value) {
    if(value.empty() || value.find('\0')!=std::string::npos)throw std::invalid_argument("invalid hit layer/state");
}
void Validate(const HitCommand& value) {
    std::set<std::string> layers;bool animation=bool(value.body);
    for(const auto& layer:value.layers) {
        Text(layer.layer);
        if(!layers.insert(layer.layer).second)throw std::invalid_argument("duplicate hit layer");
        if(!std::isfinite(layer.weight) || !std::isfinite(layer.transition) || layer.transition<0 || !std::isfinite(layer.start))
            throw std::invalid_argument("invalid hit layer parameters");
        if(!layer.state.empty()){Text(layer.state);animation=true;}
    }
    if(value.body) {
        const auto& body=*value.body;Text(body.state);Text(body.layer);
        if(layers.count(body.layer) || !std::isfinite(body.transition) || body.transition<0 || !std::isfinite(body.start) ||
            (body.normalized_end && !std::isfinite(*body.normalized_end)))
            throw std::invalid_argument("invalid hit body request");
    }
    if(!animation)throw std::invalid_argument("hit request has no animation");
    if(!value.expression.empty())Text(value.expression);
    if(!std::isfinite(value.expression_transition) || value.expression_transition<0)throw std::invalid_argument("invalid hit expression transition");
    if(value.expression_seconds && (!std::isfinite(*value.expression_seconds) || *value.expression_seconds<0))
        throw std::invalid_argument("invalid hit expression duration");
    if(value.angle && !std::isfinite(*value.angle))throw std::invalid_argument("invalid hit angle");
}
}
struct HitRequests::State {
    struct Layer {
        HitLayer command;int index=-1;
        float baseline=0;bool written=false;
        std::shared_ptr<const AnimationRequestStatus> animation;
    };
    struct Entry {
        HitCommand command;
        std::shared_ptr<HitRequestStatus> status;
        void* animator=nullptr;
        std::vector<Layer> layers;
        std::shared_ptr<const AnimationRequestStatus> body;
        std::unique_ptr<api::ExpressionOverride> expression;
        std::unique_ptr<api::BlinkOverride> blink;
        int body_layer=-1;
        bool ending=false,expression_ending=false;
        std::optional<std::pair<int,float>> expression_return;
        std::optional<float> expression_remaining;
        std::uint64_t expression_frame=0;
    };
    api::EntityToken owner;api::GameWorld* world;
    api::ActorAnimationSession& engine;AnimationRequests& animations;
    std::vector<std::shared_ptr<Entry>> entries;
    HitReturnResolver resolve_return;
    std::uint64_t next=0,model=0;void* animator=nullptr;bool servicing=false;
    bool Live() const {
        return !api::TrackedEntities().IsDisposed(owner) && !api::TrackedEntities().IsRetiring(owner) &&
            api::TrackedEntities().Current(owner.entity).generation==owner.generation;
    }
    bool Model(const Entry& entry) {
        if(!Live() || !entry.animator)return false;
        const auto component=engine.Resources().CurrentComponent(api::AnimationComponent::Model);
        if(!component.call.returned || !component.object)return false;
        const auto value=engine.Models().Read(static_cast<api::CharacterModelComponent*>(component.object));
        return Live() && value.animator.read.returned && value.animator.value==entry.animator &&
            value.changing.read.returned && !value.changing.value;
    }
    void Error(Entry& entry,const std::string& message) {
        if(entry.status->error.empty())entry.status->error=message;
        entry.ending=entry.status->cancelled=true;
    }
    void Expression(Entry& entry) {
        auto live=[&]{return Live();};
        // Restore each lease independently, including after a partial failure.
        if(entry.blink) {
            try{entry.blink->Release(live);}catch(const std::exception& e){Error(entry,e.what());}
            entry.blink.reset();
        }
        if(entry.expression) {
            try {
                if(entry.expression_return) {
                    const auto [hash,blend]=*entry.expression_return;
                    const auto result=entry.expression->ReleaseTo(hash,blend,live);
                    if(result.attempted && !result.call.returned)throw std::runtime_error(result.detail);
                }else entry.expression->Release(live);
            }catch(const std::exception& e){Error(entry,e.what());}
            entry.expression.reset();
        }
        entry.status->expression_released=true;
    }
    void End(Entry& entry,bool restore) {
        if(restore)Expression(entry);
        else {entry.expression.reset();entry.blink.reset();}
        for(auto& layer:entry.layers) {
            if(restore && layer.written)try {
                if(Model(entry)) {
                    const auto weight=engine.Control().LayerWeight(entry.animator,layer.index);
                    // A different value belongs to the next writer. Never
                    // restore an old model or clear every secondary layer.
                    if(Model(entry) && weight.read.returned && weight.value==layer.command.weight) {
                        const auto call=engine.Control().SetLayerWeight(entry.animator,layer.index,layer.baseline);
                        if(!call.returned)Error(entry,"hit layer weight restoration failed");
                    }
                }
            }catch(const std::exception& e){Error(entry,e.what());}
            if(layer.animation)animations.Release(layer.animation->serial);
        }
        if(entry.body)animations.Release(entry.body->serial);
        entry.status->released=true;
    }
    void Begin(const std::shared_ptr<Entry>& entry,api::ManagedObjects& objects) {
        auto current=[&]{return !entry->ending && Model(*entry);};
        auto& control=engine.Control();auto& resources=engine.Resources();
        std::vector<Layer> resolved;
        std::set<int> indices;
        std::optional<AnimationCommand> body;
        auto state_exists=[&](const std::string& name,int layer) {
            auto* text=resources.Text(name,objects);if(!current())return;
            const auto hash=control.StateHash(text);if(!current())return;
            if(!hash.read.returned)throw std::runtime_error("hit animation hash unread");
            const auto present=control.HasState(entry->animator,layer,hash.value);if(!current())return;
            if(!present.read.returned || !present.value)throw std::runtime_error("hit animation unavailable: "+name);
        };
        if(entry->command.body) {
            const auto& command=*entry->command.body;
            auto* text=resources.Text(command.layer,objects);if(!current())return;
            const auto index=control.LayerIndex(entry->animator,text);if(!current())return;
            if(!index.read.returned || index.value<0)throw std::runtime_error("hit visual layer unavailable: "+command.layer);
            indices.insert(index.value);entry->body_layer=index.value;
            body=AnimationCommand{command.state,index.value,command.transition,command.start,command.normalized_end};
            state_exists(command.state,index.value);if(!current())return;
        }
        for(const auto& command:entry->command.layers) {
            auto* text=resources.Text(command.layer,objects);if(!current())return;
            const auto index=control.LayerIndex(entry->animator,text);if(!current())return;
            if(!index.read.returned)throw std::runtime_error("hit layer index unread: "+command.layer);
            // Some official controllers omit the override layer. Its absence
            // does not suppress the independently available additive layer.
            if(index.value<0)continue;
            if(index.value==0 || !indices.insert(index.value).second)throw std::runtime_error("hit layer binding is not independent");
            if(!command.state.empty()){state_exists(command.state,index.value);if(!current())return;}
            const auto weight=control.LayerWeight(entry->animator,index.value);if(!current())return;
            if(!weight.read.returned || !std::isfinite(weight.value))throw std::runtime_error("hit layer weight unread");
            resolved.push_back({command,index.value,weight.value});
        }
        if(!entry->command.body && std::none_of(resolved.begin(),resolved.end(),[](const auto& layer){return !layer.command.state.empty();}))
            throw std::runtime_error("no configured hit animation layer exists");
        if(!entry->command.expression.empty()) {
            auto* text=resources.Text(entry->command.expression,objects);if(!current())return;
            const auto hash=control.StateHash(text);if(!current())return;
            if(!hash.read.returned)throw std::runtime_error("hit expression hash unread");
            entry->expression=std::make_unique<api::ExpressionOverride>(api::AnimationOverrideTarget{owner,resources,engine.Models(),control},
                entry->animator,hash.value,entry->command.expression_transition,current);
        }
        if(!current())return;
        if(entry->command.angle) {
            const auto parameter=control.HitAngleParameter();if(!current())return;
            if(!parameter.read.returned)throw std::runtime_error("hit-angle parameter unavailable");
            const auto written=control.SetFloatParameter(entry->animator,parameter.value,*entry->command.angle);
            if(!written.returned)throw std::runtime_error("hit-angle parameter write failed");
            if(!current())return;
        }
        entry->layers=std::move(resolved);
        for(auto& layer:entry->layers) {
            if(!current())return;
            layer.written=true; // a failed native call may have partially written
            const auto call=control.SetLayerWeight(entry->animator,layer.index,layer.command.weight);
            if(!call.returned)throw std::runtime_error("hit layer weight write failed");
            if(!current())return;
            if(!layer.command.state.empty())layer.animation=animations.Submit({layer.command.state,layer.index,layer.command.transition,layer.command.start});
        }
        if(body)entry->body=animations.Submit(*body);
        // Apply expression after body submission on a later service boundary.
    }
    void Observe(Entry& entry) {
        bool accepted=true,entered=true,complete=true,left=true,any=false;
        auto read=[&](const std::shared_ptr<const AnimationRequestStatus>& animation) {
            if(!animation)return;
            any=true;accepted=accepted && animation->accepted;entered=entered && animation->entered;
            complete=complete && animation->complete;left=left && animation->state_left;
            if(!animation->error.empty())Error(entry,animation->error);
            else if(animation->cancelled || animation->released)Error(entry,"hit animation superseded");
        };
        read(entry.body);for(const auto& layer:entry.layers)read(layer.animation);
        if(entry.body)entry.status->body_end_reached=entry.body->end_reached;
        if(!any)return;
        entry.status->accepted=entry.status->accepted || accepted;
        entry.status->entered=entry.status->entered || entered;
        entry.status->clip_complete=entry.status->clip_complete || complete;
        entry.status->state_left=entry.status->state_left || left;
        if(left && !entry.command.body && !entry.command.receiver_return)entry.ending=true;
    }
    bool Return(Entry& entry,bool body,const api::WorldFrame& frame,api::ManagedObjects& objects) {
        if(!resolve_return)throw std::runtime_error("receiver return policy absent");
        auto target=resolve_return(body,frame,objects);
        if(!target || entry.ending || !Model(entry))return false;
        if(body) {
            if(target->body.empty())return false;
            const auto text=engine.Resources().Text(target->body,objects);
            if(entry.ending || !Model(entry))return false;
            const auto hash=engine.Control().StateHash(text);
            if(entry.ending || !Model(entry))return false;
            if(!hash.read.returned)throw std::runtime_error("receiver return state hash unread");
            const auto current=engine.Control().CurrentState(entry.animator,entry.body_layer);
            if(entry.ending || !Model(entry))return false;
            if(!current.read.returned)throw std::runtime_error("receiver return state unread");
            if(current.short_name_hash!=hash.value && current.full_path_hash!=hash.value)return false;
            // Native OnHit also requires the actual return animation to be
            // present. A different successor is not an invitation to replace it.
        }
        if(entry.expression) {
            Text(target->expression);
            const auto text=engine.Resources().Text(target->expression,objects);
            if(entry.ending || !Model(entry))return false;
            const auto hash=engine.Control().StateHash(text);
            if(entry.ending || !Model(entry))return false;
            if(!hash.read.returned)throw std::runtime_error("hit expression return hash unread");
            entry.expression_return=std::make_pair(hash.value,target->expression_transition);
        }
        if(body)entry.status->body_returned=true;
        return true;
    }
};
HitRequests::HitRequests(api::EntityToken owner,api::GameWorld* world,api::ActorAnimationSession& engine,AnimationRequests& animations)
    :state_(std::make_unique<State>(State{owner,world,engine,animations})) {
    if(!owner.entity || !owner.generation || !world || engine.Owner().entity!=owner.entity || engine.Owner().generation!=owner.generation)
        throw std::invalid_argument("hit receiver requires its owned animation session");
}
HitRequests::~HitRequests()=default;
void HitRequests::ConfigureReturn(HitReturnResolver resolve){state_->resolve_return=std::move(resolve);}
std::shared_ptr<const HitRequestStatus> HitRequests::Submit(HitCommand command) {
    Validate(command);Cancel();auto& s=*state_;
    auto entry=std::make_shared<State::Entry>();entry->command=std::move(command);
    entry->status=std::make_shared<HitRequestStatus>();entry->status->serial=++s.next;entry->status->entity_generation=s.owner.generation;
    s.entries.push_back(entry);return entry->status;
}
void HitRequests::ReleaseExpression(std::uint64_t serial) {
    for(auto& entry:state_->entries)if(entry->status->serial==serial)entry->expression_ending=true;
}
void HitRequests::Release(std::uint64_t serial,bool cancel) {
    for(auto& entry:state_->entries)if(entry->status->serial==serial){entry->ending=true;entry->status->cancelled=cancel;}
}
void HitRequests::Cancel() {
    for(auto& entry:state_->entries){entry->ending=true;entry->status->cancelled=true;}
}
void HitRequests::ReplaceLayer(int layer) {
    if(layer==0){Cancel();return;}
    for(auto& entry:state_->entries) {
        if(entry->body_layer==layer || std::any_of(entry->layers.begin(),entry->layers.end(),[&](const auto& value){return value.index==layer;}))
            entry->ending=entry->status->cancelled=true;
        // Expressions relinquish their lease before any independent layer
        // request is delivered, even when it asks for the same state hash.
        entry->expression_ending=true;
    }
}
bool HitRequests::Drained() const {return state_->entries.empty();}
void HitRequests::CollectReceipts() {
    auto& s=*state_;if(s.servicing)return;
    for(const auto& entry:s.entries)if(!entry->ending && entry->animator)s.Observe(*entry);
}
void HitRequests::Retire(const api::WorldFrame& frame) {
    auto& s=*state_;if(s.servicing || s.entries.empty())return;
    s.servicing=true;struct Guard{bool& value;~Guard(){value=false;}} guard{s.servicing};
    const bool live=s.Live();
    const bool own_frame=frame.world==s.world && frame.disposing.read.returned && !frame.disposing.value;
    const auto snapshot=s.entries;
    for(const auto& entry:snapshot) {
        if(!live){s.Error(*entry,"hit owner ended");s.End(*entry,false);continue;}
        if(!own_frame){s.Error(*entry,"hit owner world left");continue;}
        if(entry->ending)s.End(*entry,true);
        else if(entry->expression_ending)s.Expression(*entry);
    }
    s.entries.erase(std::remove_if(s.entries.begin(),s.entries.end(),[](const auto& entry){return entry->status->released;}),s.entries.end());
}
void HitRequests::Service(const api::WorldFrame& frame,bool ready,api::ManagedObjects& objects) {
    auto& s=*state_;if(s.servicing || s.entries.empty())return;
    s.servicing=true;struct Guard{bool& value;~Guard(){value=false;}} guard{s.servicing};
    const bool live=s.Live();
    const bool own_frame=frame.world==s.world && frame.disposing.read.returned && !frame.disposing.value;
    const auto snapshot=s.entries;
    for(const auto& entry:snapshot) {
        if(!live){s.Error(*entry,"hit owner ended");s.End(*entry,false);continue;}
        if(!own_frame){s.Error(*entry,"hit owner world left");continue;}
        if(entry->ending){s.End(*entry,true);continue;}
        if(!ready)continue;
        try {
            if(!entry->animator) {
                auto& resources=s.engine.Resources();resources.RetainOwner();
                const auto entity=s.engine.Entities().Read(s.owner.entity);
                if(!s.Live() || entry->ending)continue;
                if(!entity.owner_world.read.returned || entity.owner_world.value!=s.world || !entity.loaded.read.returned)
                    throw std::runtime_error("hit owner observation unavailable");
                if(!entity.loaded.value)continue;
                const auto component=resources.Component(api::AnimationComponent::Model,objects);
                if(!s.Live() || entry->ending)continue;
                if(!component.call.returned || !component.object)throw std::runtime_error("hit model unavailable");
                const auto model=s.engine.Models().Read(static_cast<api::CharacterModelComponent*>(component.object));
                if(!s.Live() || entry->ending)continue;
                if(!model.changing.read.returned || !model.animator.read.returned)throw std::runtime_error("hit model unread");
                if(model.changing.value || !model.animator.value)continue;
                const auto initialized=s.engine.Inputs().AnimatorInitialized(model.animator.value);
                if(!s.Live() || entry->ending)continue;
                if(!initialized.read.returned)throw std::runtime_error("hit Animator initialization unread");
                if(!initialized.value)continue;
                if(s.animator!=model.animator.value){s.animator=model.animator.value;++s.model;}
                entry->animator=s.animator;entry->status->model_generation=s.model;
                s.Begin(entry,objects);
            } else if(!s.Model(*entry))s.Error(*entry,"hit model changed");
            if(!entry->ending) {
                s.Observe(*entry);
                auto current=[&]{return !entry->ending && s.Model(*entry);};
                if(!entry->ending && entry->status->state_left && entry->command.receiver_return &&
                    (!entry->command.body || entry->status->body_end_reached) &&
                    s.Return(*entry,bool(entry->command.body),frame,objects))entry->ending=true;
                if(entry->ending){s.End(*entry,true);continue;}
                if(entry->expression_ending)s.Expression(*entry);
                else if(entry->status->accepted && entry->expression && current()) {
                    if(entry->command.expression_seconds) {
                        if(!entry->expression_remaining)entry->expression_remaining=entry->command.expression_seconds;
                        else if(frame.sequence>entry->expression_frame && std::isfinite(frame.delta_time) && frame.delta_time>=0)
                            *entry->expression_remaining-=frame.delta_time;
                        entry->expression_frame=frame.sequence;
                        if(*entry->expression_remaining<=0) {
                            if(!entry->command.receiver_return || s.Return(*entry,false,frame,objects)) {
                                entry->expression_ending=true;s.Expression(*entry);
                            }
                            continue;
                        }
                    }
                    const auto result=entry->expression->Apply(current);
                    if(result.attempted && !result.call.returned)throw std::runtime_error(result.detail);
                    if(!entry->blink && entry->command.blink_source>=0 && current()) {
                        const auto emotion=s.engine.Resources().Component(api::AnimationComponent::Emotion,objects);
                        if(!current())continue;
                        if(!emotion.call.returned)throw std::runtime_error("hit emotion component lookup failed");
                        // OnHit itself skips blink writes for an absent emotion
                        // component. That does not invalidate the body/face call.
                        if(!emotion.object)continue;
                        entry->blink=std::make_unique<api::BlinkOverride>(api::AnimationOverrideTarget{s.owner,s.engine.Resources(),s.engine.Models(),s.engine.Control()},entry->command.blink_source);
                        const auto blink_result=entry->blink->Apply(objects,current);
                        if(blink_result.attempted && !blink_result.call.returned)throw std::runtime_error(blink_result.detail);
                    }
                }
            }
        }catch(const std::exception& error){s.Error(*entry,error.what());}
        if(entry->ending)s.End(*entry,true);
    }
    s.entries.erase(std::remove_if(s.entries.begin(),s.entries.end(),[](const auto& entry){return entry->status->released;}),s.entries.end());
}
}
