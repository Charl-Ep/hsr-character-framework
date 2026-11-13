#pragma once
#include <hksr/framework/presentation/model/VisualModelTypes.hpp>
#include <memory>

namespace hksr::framework {
// Last explicit writer per family wins within this actor/model. Withdrawals
// restore the next scope or captured target; no animation channel is claimed.
class ActorModelScopes {
  public:
    explicit ActorModelScopes(std::unique_ptr<api::ActorModelPresentation>);
    ~ActorModelScopes();
    void Face(std::uint64_t, api::FaceMaterialTransition);
    void Scale(std::uint64_t, float);
    void Follow(std::uint64_t, api::EntityOrigin);
    void Service();
    bool Current() const;
    bool Release(std::uint64_t);
    bool Close();
    const std::string& Error() const;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
}
