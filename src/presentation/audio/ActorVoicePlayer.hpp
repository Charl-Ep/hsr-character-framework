#pragma once
#include <hksr/framework/presentation/audio/ActorVoice.hpp>
#include <hksr/presentation/audio/OwnedSoundRequest.hpp>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <string>

namespace hksr::framework::detail {
// Per-actor requests. Cue selection and native-event deduplication are supplied
// by the caller; replacing an ordinary body action does not cancel this pool.
class ActorVoicePlayer {
  public:
    using Factory = std::function<std::unique_ptr<api::OwnedSoundRequest>(const ActorVoiceCue&)>;
    ActorVoicePlayer(api::EntityOrigin actor, std::function<bool()> current);
    void Configure(std::uint32_t avatar, std::string source, Factory);
    bool Configured() const;
    // False means no attempt; true publishes a fresh Error, including success.
    bool Play(api::VoiceCue, const std::string& state, const std::string& source = {});
    std::optional<std::string> Service(bool own_frame);
    void Cancel();
    bool Drained() const;
    std::size_t Count() const;
    const std::string& Error() const;

  private:
    api::EntityOrigin actor_;
    std::function<bool()> current_;
    std::uint32_t avatar_ = 0;
    std::string source_;
    Factory factory_;
    std::list<std::unique_ptr<api::OwnedSoundRequest>> requests_;
    std::string error_;
    std::size_t submitting_ = 0;
    bool servicing_ = false;
    std::uint64_t cancellation_ = 0;
};
}
