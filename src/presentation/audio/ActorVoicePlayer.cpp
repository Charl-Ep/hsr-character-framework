#include "ActorVoicePlayer.hpp"
#include <stdexcept>
#include <utility>
#include <algorithm>

namespace hksr::framework::detail {
ActorVoicePlayer::ActorVoicePlayer(api::EntityOrigin actor, std::function<bool()> current)
    : actor_(actor), current_(std::move(current)) {}
void ActorVoicePlayer::Configure(std::uint32_t avatar, std::string source, Factory factory) {
    avatar_ = avatar;
    source_ = std::move(source);
    factory_ = std::move(factory);
}
bool ActorVoicePlayer::Configured() const {
    return avatar_ && bool(factory_);
}
bool ActorVoicePlayer::Play(api::VoiceCue cue, const std::string& state, const std::string& source) {
    if (!Configured() || !current_())
        return false;
    ++submitting_;
    struct Exit {
        std::size_t& count;
        ~Exit() {
            --count;
        }
    } exit{submitting_};
    const auto cancellation = cancellation_;
    auto slot = requests_.end();
    try {
        // Allocate ownership bookkeeping before the factory can submit audio.
        slot = requests_.insert(requests_.end(), nullptr);
        // A callback may reconfigure this player. Keep its factory alive.
        const auto start = factory_;
        *slot = start({actor_, avatar_, cue, source.empty() ? source_ : source, state});
        if (*slot) {
            if (cancellation != cancellation_ || !current_())
                (*slot)->Cancel();
        } else
            requests_.erase(slot);
        error_.clear();
    } catch (const std::exception& error) {
        if (slot != requests_.end()) {
            if (*slot)
                (*slot)->Cancel();
            else
                requests_.erase(slot);
        }
        error_ = error.what();
    }
    return true;
}
std::optional<std::string> ActorVoicePlayer::Service(bool own_frame) {
    if (servicing_)
        return {};
    servicing_ = true;
    struct Exit {
        bool& value;
        ~Exit() {
            value = false;
        }
    } exit{servicing_};
    std::optional<std::string> reported;
    // The list keeps iterators valid during native reentry. New submissions
    // join the next batch rather than extending this update indefinitely.
    auto remaining = requests_.size();
    for (auto it = requests_.begin(); remaining && it != requests_.end(); --remaining) {
        if (!*it) { // an in-flight factory can synchronously service the pool
            ++it;
            continue;
        }
        const bool current = own_frame && current_();
        if (!current)
            (*it)->Cancel();
        (*it)->Service(current);
        const auto& status = (*it)->Status();
        if (!status.error.empty()) {
            error_ = status.error;
            reported = error_;
        }
        if (status.closed)
            it = requests_.erase(it);
        else
            ++it;
    }
    return reported;
}
void ActorVoicePlayer::Cancel() {
    ++cancellation_;
    for (const auto& request : requests_)
        if (request)
            request->Cancel();
}
bool ActorVoicePlayer::Drained() const {
    return !submitting_ && !servicing_ && requests_.empty();
}
std::size_t ActorVoicePlayer::Count() const {
    return static_cast<std::size_t>(
        std::count_if(requests_.begin(), requests_.end(), [](const auto& request) { return bool(request); }));
}
const std::string& ActorVoicePlayer::Error() const {
    return error_;
}
}
