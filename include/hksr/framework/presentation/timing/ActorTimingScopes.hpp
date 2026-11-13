#pragma once
#include <hksr/animation/ActorTiming.hpp>
#include <map>
#include <string>
#include <vector>

namespace hksr::framework {
enum class TimingClearState { Done, Pending, Failed };
class ActorTimingScopes {
  public:
    explicit ActorTimingScopes(api::ActorTiming&);
    void Layer(std::uint64_t, const std::string&, float weight, float duration);
    void Speed(std::uint64_t, float);
    void Scale(std::uint64_t, const std::string&, const api::ActorTimeScaleInput&);
    TimingClearState ClearScale(std::uint64_t, const std::string&, bool fade);
    TimingClearState ClearFinite(std::uint64_t, bool fade);
    bool Release(std::uint64_t, bool natural = false);
    bool Close();
    const std::string& Error() const {
        return error_;
    }

  private:
    struct Value {
        std::uint64_t scope;
        float value;
    };
    struct Slot {
        api::ActorTimingModel model;
        float baseline = 0, last = 0;
        std::vector<Value> values;
    };
    struct Clock {
        std::uint64_t scope;
        std::string key;
        api::ActorTimeScaleToken token;
        bool finite = false, fading = false;
    };
    void Set(std::uint64_t, const std::string&, float, float, bool speed);
    api::ActorTiming& timing_;
    std::map<std::string, Slot> slots_;
    std::vector<Clock> clocks_;
    std::string prefix_;
    std::string error_;
};
}
