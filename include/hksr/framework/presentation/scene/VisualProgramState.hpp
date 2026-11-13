#pragma once
#include <hksr/framework/tasks/ContextNumbers.hpp>
#include <hksr/framework/tasks/ContextValues.hpp>
#include <hksr/math/SpatialTypes.hpp>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <variant>
#include <tuple>

namespace hksr::framework {
struct VisualSpatialAnchor {
    api::Position3 position;
    api::Rotation4 rotation;
};
using VisualSpatialAnchors = std::map<std::string, VisualSpatialAnchor>;
VisualSpatialAnchors ParseVisualSpatialAnchors(const std::string& json);
struct VisualProgramParameters {
    std::map<std::string, std::uint64_t> targets; // zero clears an explicit entity selection
    std::map<std::string, std::string> choices;
    VisualSpatialAnchors anchors;
    VisualSpatialAnchors position_targets;             // captured world poses, not entity identities
    std::map<std::string, std::string> target_aliases; // immutable captured name bindings
    std::map<std::string, std::vector<std::string>>
        target_collections; // ordered named inputs; an explicit empty list stays empty
};
void ValidateVisualParameters(const VisualProgramParameters&);
bool SameVisualParameters(const VisualProgramParameters&, const VisualProgramParameters&);
void RemapVisualParameters(VisualProgramParameters&, const std::map<std::uint64_t, std::uint64_t>&);
// Current presentation inputs, separate from a persistence recipe or an
// animation receipt. Scope distinguishes values retained by named contexts.
struct VisualBranchValue {
    std::string scope, name;
    std::variant<std::string, float, std::int64_t, double, bool> value;
};
struct VisualBranchState {
    std::string phase;
    std::vector<VisualBranchValue> values;
};
// Instance identity is separate from the definition and from the transient
// resource generation. Zero selects an authored singleton context.
struct VisualContextKey {
    std::string definition;
    std::uint64_t instance = 0;
    VisualContextKey() = default;
    VisualContextKey(std::string name, std::uint64_t id = 0) : definition(std::move(name)), instance(id) {}
    bool operator<(const VisualContextKey& other) const {
        return std::tie(definition, instance) < std::tie(other.definition, other.instance);
    }
    bool operator==(const VisualContextKey& other) const {
        return definition == other.definition && instance == other.instance;
    }
};
struct VisualContextBinding {
    std::string name, category, ability;
    std::uint64_t receiver = 0, caster = 0; // Framework character IDs, never native addresses
};
// Value-only reconstruction inputs; no native requests, playback clocks or
// event tokens. Context order is entry order, not lexical name order.
struct VisualContextSnapshot {
    std::string name;
    VisualProgramParameters inputs;
    std::map<std::string, float> variables;
    std::uint64_t instance = 0;
    std::optional<VisualContextBinding> binding;
    ContextNumbers numbers;
    std::vector<std::string> effects; // selected persistent effect keys, in creation order
    ContextValues values;
};
struct VisualProgramSnapshot {
    std::string program;
    std::optional<VisualContextSnapshot> scene;
    std::vector<VisualContextSnapshot> contexts;
};
void ValidateVisualSnapshot(const VisualProgramSnapshot&);
bool SameVisualSnapshot(const VisualProgramSnapshot&, const VisualProgramSnapshot&);
void RemapVisualTargets(VisualProgramSnapshot&, const std::map<std::uint64_t, std::uint64_t>&);
}
