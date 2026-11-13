#include <hksr/framework/presentation/scene/VisualTargets.hpp>
#include <cmath>
#include <set>
#include <stdexcept>

namespace hksr::framework {
std::string BindVisualTarget(const std::string& name, const VisualTargetAliases& bindings) {
    const auto found = bindings.find(name);
    return found == bindings.end() ? name : found->second;
}
VisualProgramParameters BindVisualParameters(const VisualProgramParameters& inputs,
                                             const VisualTargetAliases& bindings) {
    auto out = inputs;
    for (auto& [name, members] : out.target_collections)
        for (auto& member : members)
            member = BindVisualTarget(member, bindings);
    for (const auto& [name, target] : bindings) {
        if (name == target)
            continue;
        out.targets.erase(name);
        out.position_targets.erase(name);
        out.target_aliases.erase(name);
        if (const auto value = inputs.targets.find(target); value != inputs.targets.end())
            out.targets[name] = value->second;
        if (const auto value = inputs.position_targets.find(target); value != inputs.position_targets.end())
            out.position_targets[name] = value->second;
        if (!out.targets.count(name) && !out.position_targets.count(name))
            out.target_aliases[name] = BindVisualTarget(target, inputs.target_aliases);
    }
    return out;
}
void ValidateVisualTargetDefaults(const VisualTargetDefaults& defaults) {
    for (const auto& [name, value] : defaults) {
        if (name.empty() || name == "Caster" || name.compare(0, 8, "partner:") == 0)
            throw std::invalid_argument("invalid default target name: " + name);
        std::set<std::string> visited{name};
        auto current = &value;
        while (const auto* alias = std::get_if<std::string>(current)) {
            if (alias->empty())
                throw std::invalid_argument("empty target alias: " + name);
            if (!visited.insert(*alias).second)
                throw std::invalid_argument("cyclic target alias: " + name);
            const auto found = defaults.find(*alias);
            if (found == defaults.end())
                break;
            current = &found->second;
        }
        if (const auto* point = std::get_if<api::Position3>(current))
            if (!std::isfinite(point->x) || !std::isfinite(point->y) || !std::isfinite(point->z))
                throw std::invalid_argument("nonfinite default position: " + name);
    }
}
VisualTargetBindings ResolveVisualTargetBindings(const VisualTargetDefaults& defaults,
                                                 const VisualProgramParameters& inputs,
                                                 const std::vector<std::string>& available_entities,
                                                 const std::vector<std::string>& bound_entities) {
    auto effective = defaults;
    for (const auto& [name, target] : inputs.target_aliases)
        effective[name] = target;
    ValidateVisualTargetDefaults(effective);
    const std::set<std::string> available(available_entities.begin(), available_entities.end());
    const std::set<std::string> bound(bound_entities.begin(), bound_entities.end());
    VisualTargetBindings out;
    std::set<std::string> visited;
    std::function<void(const std::string&)> resolve = [&](const std::string& name) {
        if (!visited.insert(name).second)
            return;
        if (name != "Caster") {
            if (const auto alias = inputs.target_aliases.find(name); alias != inputs.target_aliases.end()) {
                resolve(alias->second);
                const auto value = out.find(alias->second);
                if (value != out.end())
                    out.emplace(name, value->second);
                return;
            }
            const auto selected = inputs.targets.find(name);
            if ((selected == inputs.targets.end() || !selected->second) && inputs.position_targets.count(name)) {
                out.emplace(name, VisualPositionTarget{inputs.position_targets.at(name), false});
                return;
            }
        }
        // An explicit missing/deleted entity never falls through to a point.
        if (available.count(name)) {
            out.emplace(name, VisualEntityTarget{name});
            return;
        }
        if (bound.count(name))
            return;
        const auto selected = inputs.targets.find(name);
        if (selected != inputs.targets.end() && selected->second)
            return;
        if (const auto point = inputs.position_targets.find(name); point != inputs.position_targets.end()) {
            out.emplace(name, VisualPositionTarget{point->second, false});
            return;
        }
        const auto found = effective.find(name);
        if (found == effective.end())
            return;
        if (const auto* alias = std::get_if<std::string>(&found->second)) {
            resolve(*alias);
            const auto target = out.find(*alias);
            if (target != out.end())
                out.emplace(name, target->second);
        } else
            out.emplace(name, VisualPositionTarget{{std::get<api::Position3>(found->second), {}}, true});
    };
    for (const auto& name : available)
        resolve(name);
    for (const auto& [name, value] : inputs.targets) {
        (void)value;
        resolve(name);
    }
    for (const auto& [name, value] : inputs.position_targets) {
        (void)value;
        resolve(name);
    }
    for (const auto& [name, value] : effective) {
        (void)value;
        resolve(name);
    }
    return out;
}
VisualSpatialAnchor TransformVisualTarget(const VisualSpatialAnchor& owner, api::Position3 v) {
    const auto& r = owner.rotation;
    const float norm = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
    if (!std::isfinite(norm) || norm < 1e-6f)
        throw std::runtime_error("invalid owner rotation for position target");
    const api::Rotation4 q{r.x / norm, r.y / norm, r.z / norm, r.w / norm};
    const api::Position3 t{2 * (q.y * v.z - q.z * v.y), 2 * (q.z * v.x - q.x * v.z), 2 * (q.x * v.y - q.y * v.x)};
    return {{owner.position.x + v.x + q.w * t.x + q.y * t.z - q.z * t.y,
             owner.position.y + v.y + q.w * t.y + q.z * t.x - q.x * t.z,
             owner.position.z + v.z + q.w * t.z + q.x * t.y - q.y * t.x},
            q};
}
VisualSpatialAnchors CaptureVisualPositionTargets(const VisualTargetBindings& bindings,
                                                  const std::function<VisualSpatialAnchor()>& read_owner) {
    std::optional<VisualSpatialAnchor> owner;
    VisualSpatialAnchors out;
    for (const auto& [name, value] : bindings)
        if (const auto* point = std::get_if<VisualPositionTarget>(&value)) {
            if (point->local && !owner)
                owner = read_owner();
            out.emplace(name, point->local ? TransformVisualTarget(*owner, point->pose.position) : point->pose);
        }
    return out;
}
api::Rotation4 RotateVisualTarget(api::Rotation4 q, api::Position3 euler) {
    const auto multiply = [](api::Rotation4 a, api::Rotation4 b) {
        return api::Rotation4{
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
    };
    constexpr float radians = 3.14159265358979323846f / 360.f;
    const float x = euler.x * radians, y = euler.y * radians, z = euler.z * radians;
    // Local Euler offset: Z, then X, then Y.
    return multiply(q, multiply({0, std::sin(y), 0, std::cos(y)},
                                multiply({std::sin(x), 0, 0, std::cos(x)}, {0, 0, std::sin(z), std::cos(z)})));
}
VisualProgramParameters VisualTargetInputs(const VisualTargetDefaults& defaults, const VisualProgramParameters& inputs,
                                           const std::vector<std::string>& required) {
    VisualProgramParameters out;
    std::set<std::string> visited;
    std::function<void(const std::string&)> include = [&](const std::string& name) {
        if (!visited.insert(name).second)
            return;
        if (const auto found = inputs.targets.find(name); found != inputs.targets.end())
            out.targets.insert(*found);
        if (const auto found = inputs.position_targets.find(name); found != inputs.position_targets.end())
            out.position_targets.insert(*found);
        if (const auto found = inputs.target_aliases.find(name); found != inputs.target_aliases.end()) {
            out.target_aliases.insert(*found);
            include(found->second);
            return;
        }
        if (const auto found = defaults.find(name); found != defaults.end())
            if (const auto* alias = std::get_if<std::string>(&found->second))
                include(*alias);
    };
    for (const auto& name : required)
        include(name);
    return out;
}
}
