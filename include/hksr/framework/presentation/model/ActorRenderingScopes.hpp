#pragma once
#include <hksr/framework/presentation/model/VisualRenderingTypes.hpp>
#include <hksr/model/ActorRendering.hpp>
#include <hksr/entity/EntityOwner.hpp>
#include <functional>
#include <map>
#include <memory>
#include <string>
namespace hksr::framework {
// One registry per presentation host, shared by all source scenes. Scope IDs
// identify executions, not actors; overlapping writes to one receiver compose.
class ActorRenderingScopes {
  public:
    using Factory = std::function<std::unique_ptr<api::ActorRendering>(api::EntityOrigin)>;
    explicit ActorRenderingScopes(Factory);
    ~ActorRenderingScopes();
    bool Apply(api::EntityOrigin, std::uint64_t scope, const VisualRenderingOperation&);
    bool Release(std::uint64_t scope);
    bool ReleaseParticipant(api::EntityOrigin);
    bool Close();
    const std::string& Error() const {
        return error_;
    }
    std::size_t RetainedContributions() const;

  private:
    struct Receiver;
    std::vector<std::unique_ptr<Receiver>> receivers_;
    Factory factory_;
    std::string prefix_, error_;
};
}
