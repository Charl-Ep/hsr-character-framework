#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace hksr::framework {
struct LayerWeightModel {
    void* animator = nullptr;
    void* controller = nullptr;
    void* component = nullptr;
    bool operator==(const LayerWeightModel& other) const {
        return animator == other.animator && controller == other.controller && component == other.component;
    }
};
struct LayerWeightServices {
    std::function<std::optional<LayerWeightModel>()> current;
    std::function<int(void*, const std::string&)> index;
    std::function<float(void*, int)> read;
    std::function<void(void*, int, float)> write;
};
// Engine-thread contributions on one owned actor. No persistent native values:
// a restored script captures the new model's baseline when it executes again.
class AnimatorLayerWeights {
  public:
    explicit AnimatorLayerWeights(LayerWeightServices);
    void Set(std::uint64_t scope, const std::string& layer, float weight);
    bool Release(std::uint64_t scope);
    bool Close();
    const std::string& Error() const {
        return error_;
    }

  private:
    struct Contribution {
        std::uint64_t scope;
        float value;
    };
    struct Entry {
        LayerWeightModel model;
        int index;
        float baseline, last;
        std::vector<Contribution> contributions;
        std::optional<float> pending_restore;
    };
    bool Current(const LayerWeightModel&) const;
    LayerWeightServices services_;
    std::vector<Entry> entries_;
    std::string error_;
};
}
