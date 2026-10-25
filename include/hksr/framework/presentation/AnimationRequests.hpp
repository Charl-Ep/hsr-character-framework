#pragma once
#include <hksr/animation/ActorAnimationSession.hpp>
#include <hksr/entity/EntityAccess.hpp>
#include <hksr/world/WorldFrames.hpp>
#include <memory>
#include <map>
#include <optional>
#include <functional>

namespace hksr::framework {
struct AnimationCommand {
    std::string state;int layer=0;float transition=0;float normalized_start=0;
    std::optional<float> normalized_end;
};
struct AnimationRequestStatus {
    std::uint64_t serial=0,entity_generation=0,model_generation=0;
    bool accepted=false,entered=false,complete=false,cancelled=false,released=false;
    std::optional<double> progress;
    std::string error;
    // The configured request boundary and observed state departure are not
    // the clip's completion or the lifetime of attached resources.
    bool end_reached=false,state_left=false;
};
struct AnimationRequestEndpoint {
    std::uint64_t entity_generation=0;
    std::function<std::shared_ptr<const AnimationRequestStatus>(const AnimationCommand&)> submit;
    std::function<void(std::uint64_t)> release;
};
// Request observations on an existing owned session. No automatic successor,
// event registration, entity lookup by name or gameplay state mutation.
class AnimationRequests {
public:
    using BodyPlayer=std::function<api::CallResult(const AnimationCommand&)>;
    AnimationRequests(api::EntityToken,api::GameWorld*,api::ActorAnimationSession&,BodyPlayer={});
    std::shared_ptr<const AnimationRequestStatus> Submit(AnimationCommand);
    void Release(std::uint64_t);
    void Cancel(std::optional<int> layer={},const std::string& reason="animation request superseded");
    void Service(const api::WorldFrame&,bool inputs_ready,api::ManagedObjects&);
private:
    struct Entry;
    bool Live(const std::shared_ptr<Entry>&) const;
    void Finish(const std::shared_ptr<Entry>&,const std::string&);
    api::EntityToken owner_;
    api::GameWorld* world_;
    api::ActorAnimationSession& engine_;
    BodyPlayer body_player_;
    std::map<std::uint64_t,std::shared_ptr<Entry>> entries_;
    std::uint64_t next_=0,model_=0;
    void* animator_=nullptr;
    bool servicing_=false;
};
}
