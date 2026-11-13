#pragma once
#include <hksr/framework/presentation/playback/AnimationRequests.hpp>
#include <map>

namespace hksr::framework::detail {
// One actor's expression setter requests. Observation release does not restore
// the face, and completed writes do not acquire an animation clock.
class ExpressionPlayer {
  public:
    ExpressionPlayer(api::EntityToken, api::GameWorld*, api::ActorAnimationSession&);
    std::shared_ptr<const AnimationRequestStatus> Submit(std::string state, float transition);
    void Release(std::uint64_t);
    void Cancel();
    void Service(const api::WorldFrame&, api::ManagedObjects&);
    // Reentrant native calls must see closure before the owner drains siblings.
    void BeginClose() {
        closing_ = true;
    }

  private:
    struct Write {
        std::shared_ptr<AnimationRequestStatus> status;
        std::string state;
        float transition = .2f;
    };
    bool Ended() const;
    api::EntityToken owner_;
    api::GameWorld* world_;
    api::ActorAnimationSession& engine_;
    std::map<std::uint64_t, std::shared_ptr<Write>> requests_;
    std::uint64_t next_ = 0;
    bool closing_ = false;
};
}
