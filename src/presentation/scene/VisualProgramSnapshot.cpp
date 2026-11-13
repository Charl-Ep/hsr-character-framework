#include <hksr/framework/presentation/scene/VisualProgram.hpp>
#include <cmath>
#include <tuple>
#include <stdexcept>

namespace hksr::framework {
void ValidateVisualParameters(const VisualProgramParameters& value) {
    auto name = [](const std::string& text) { return !text.empty() && text.find('\0') == std::string::npos; };
    auto need = [](bool condition) {
        if (!condition)
            throw std::invalid_argument("invalid visual snapshot value");
    };
    for (const auto& [key, id] : value.targets)
        need(name(key));
    VisualTargetDefaults aliases;
    for (const auto& [key, target] : value.target_aliases) {
        need(name(key) && name(target));
        aliases[key] = target;
    }
    ValidateVisualTargetDefaults(aliases);
    for (const auto& [key, members] : value.target_collections) {
        need(name(key));
        for (const auto& member : members)
            need(name(member));
    }
    for (const auto& [key, choice] : value.choices)
        need(name(key) && name(choice));
    for (const auto* poses : {&value.anchors, &value.position_targets})
        for (const auto& [key, pose] : *poses) {
            const auto& p = pose.position;
            const auto& q = pose.rotation;
            const double norm = double(q.x) * q.x + double(q.y) * q.y + double(q.z) * q.z + double(q.w) * q.w;
            need(name(key) && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
                 std::isfinite(norm) && norm > 0);
        }
}
bool SameVisualParameters(const VisualProgramParameters& a, const VisualProgramParameters& b) {
    if (std::tie(a.targets, a.choices, a.target_aliases, a.target_collections) !=
        std::tie(b.targets, b.choices, b.target_aliases, b.target_collections))
        return false;
    for (const auto& pair : {std::make_pair(&a.anchors, &b.anchors),
                             std::make_pair(&a.position_targets, &b.position_targets)}) {
        if (pair.first->size() != pair.second->size())
            return false;
        for (const auto& [key, pose] : *pair.first) {
            const auto found = pair.second->find(key);
            if (found == pair.second->end())
                return false;
            const auto& other = found->second;
            const auto& p = pose.position;
            const auto& q = pose.rotation;
            const auto& r = other.position;
            const auto& s = other.rotation;
            if (std::tie(p.x, p.y, p.z, q.x, q.y, q.z, q.w) != std::tie(r.x, r.y, r.z, s.x, s.y, s.z, s.w))
                return false;
        }
    }
    return true;
}
void RemapVisualParameters(VisualProgramParameters& value, const std::map<std::uint64_t, std::uint64_t>& keys) {
    for (auto& [name, id] : value.targets)
        if (id)
            id = keys.at(id);
}
void ValidateVisualSnapshot(const VisualProgramSnapshot& value) {
    auto name = [](const std::string& text) { return !text.empty() && text.find('\0') == std::string::npos; };
    auto need = [](bool condition) {
        if (!condition)
            throw std::invalid_argument("invalid visual snapshot value");
    };
    need(name(value.program));
    std::set<VisualContextKey> names;
    auto context = [&](const VisualContextSnapshot& item) {
        if (item.binding)
            need(item.instance && item.binding->receiver && item.binding->caster && name(item.binding->name) &&
                 name(item.binding->category) && item.binding->ability.find('\0') == std::string::npos);
        ValidateVisualParameters(item.inputs);
        for (const auto& [key, number] : item.variables)
            need(name(key) && std::isfinite(number));
        for (const auto& [key, number] : item.numbers) {
            need(name(key));
            ValidateContextNumber(number);
        }
        for (const auto& [key, value] : item.values) {
            need(name(key));
            if (const auto* number = std::get_if<double>(&value))
                need(std::isfinite(*number));
        }
        for (const auto& key : item.effects)
            need(name(key));
    };
    if (value.scene) {
        need(value.scene->name.empty() && !value.scene->instance && !value.scene->binding &&
             value.scene->numbers.empty() && value.scene->effects.empty() && value.scene->values.empty());
        context(*value.scene);
    }
    for (const auto& item : value.contexts) {
        need(name(item.name) && names.insert({item.name, item.instance}).second);
        context(item);
    }
}
bool SameVisualSnapshot(const VisualProgramSnapshot& a, const VisualProgramSnapshot& b) {
    if (a.program != b.program || bool(a.scene) != bool(b.scene) || a.contexts.size() != b.contexts.size())
        return false;
    auto same = [](const VisualContextSnapshot& x, const VisualContextSnapshot& y) {
        if (x.numbers != y.numbers || x.effects != y.effects || x.values != y.values)
            return false;
        if (x.instance != y.instance || bool(x.binding) != bool(y.binding))
            return false;
        if (x.binding &&
            std::tie(x.binding->name, x.binding->category, x.binding->ability, x.binding->receiver,
                     x.binding->caster) != std::tie(y.binding->name, y.binding->category, y.binding->ability,
                                                    y.binding->receiver, y.binding->caster))
            return false;
        return x.name == y.name && x.variables == y.variables && SameVisualParameters(x.inputs, y.inputs);
    };
    if (a.scene && !same(*a.scene, *b.scene))
        return false;
    for (std::size_t i = 0; i < a.contexts.size(); ++i)
        if (!same(a.contexts[i], b.contexts[i]))
            return false;
    return true;
}
void RemapVisualTargets(VisualProgramSnapshot& value, const std::map<std::uint64_t, std::uint64_t>& keys) {
    auto remap = [&](auto& context) {
        RemapVisualParameters(context.inputs, keys);
        if (context.binding) {
            context.binding->receiver = keys.at(context.binding->receiver);
            context.binding->caster = keys.at(context.binding->caster);
        }
    };
    if (value.scene)
        remap(*value.scene);
    for (auto& context : value.contexts)
        remap(context);
}
void ValidateVisualRestore(const VisualProgram& program, const VisualProgramSnapshot& value) {
    ValidateVisualSnapshot(value);
    if (value.program != program.id)
        throw std::invalid_argument("snapshot belongs to another visual program");
    if (value.scene && (!value.scene->name.empty() || !program.restore))
        throw std::invalid_argument("visual scene restore recipe unavailable");
    for (const auto& context : value.contexts) {
        const auto found = program.contexts.find(context.name);
        if (found == program.contexts.end() || !found->second.restore)
            throw std::invalid_argument("visual context restore recipe unavailable or repeated: " + context.name);
        for (const auto& key : context.effects)
            if (!found->second.restore_effects.count(key) || !program.effects.count(key))
                throw std::invalid_argument("effect is not restorable by this context: " + context.name + "/" + key);
    }
}
}
