#pragma once
#include <hksr/framework/presentation/scene/VisualStepResult.hpp>
#include <hksr/framework/presentation/audio/VisualBgmTypes.hpp>
namespace hksr::framework {
class ActorBgmScopes {
  public:
    explicit ActorBgmScopes(std::unique_ptr<api::ActorBgmSync>);
    ~ActorBgmScopes();
    VisualStepResult Execute(std::uint64_t, const VisualBgmSync&);
    bool Release(std::uint64_t);
    bool Service();
    bool Close();
    bool Current() const;
    const std::string& Error() const;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
}
