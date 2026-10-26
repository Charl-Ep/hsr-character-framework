#pragma once
#include "AnimationRequests.hpp"
#include <hksr/animation/AnimationOverrides.hpp>
#include <vector>

namespace hksr::framework {
// Resolved by the receiving actor, not by the attacking actor. An empty state
// changes only the named layer's weight (the heavy-hit suppression case).
struct HitLayer {
    std::string layer,state;
    float weight=0,transition=0,start=0;
};
struct HitBody {
    std::string layer,state;
    float transition=0,start=0;
    std::optional<float> normalized_end;
};
struct HitCommand {
    std::vector<HitLayer> layers;
    std::optional<HitBody> body; // official visual-layer name, not a fixed index
    std::string expression;
    float expression_transition=0;
    int blink_source=-1;
    std::optional<float> expression_seconds;
    bool receiver_return=false;
    std::optional<float> angle;
};
struct HitReturn {
    std::string body,expression;
    float expression_transition=0;
};
// Requested only at a natural expression/animation boundary. nullopt means
// current receiver inputs are not ready, not permission to restore old state.
using HitReturnResolver=std::function<std::optional<HitReturn>(bool body,const api::WorldFrame&,api::ManagedObjects&)>;
struct HitRequestStatus {
    std::uint64_t serial=0,entity_generation=0,model_generation=0;
    bool accepted=false,entered=false,clip_complete=false,state_left=false;
    bool cancelled=false,released=false;
    bool expression_released=false,body_end_reached=false,body_returned=false;
    std::string error;
};
// Receiving actor's playback primitive. It shares the actor's animation
// requests and never resolves arbitrary entities or invokes battle tasks.
// Ordinary layer departure can finish a request; body-hit return is explicitly
// released by the receiver's logical controller, not inferred from clip time.
class HitRequests {
public:
    HitRequests(api::EntityToken,api::GameWorld*,api::ActorAnimationSession&,AnimationRequests&);
    ~HitRequests(); // no engine calls
    std::shared_ptr<const HitRequestStatus> Submit(HitCommand);
    void ConfigureReturn(HitReturnResolver);
    void ReleaseExpression(std::uint64_t);
    void Release(std::uint64_t,bool cancel=false);
    void Cancel(); // data only; Service drains the leases
    void ReplaceLayer(int); // a newer explicit actor animation owns this layer
    void Retire(const api::WorldFrame&); // cancellation only; no allocation/playback
    void Service(const api::WorldFrame&,bool inputs_ready,api::ManagedObjects&);
    bool Drained() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
