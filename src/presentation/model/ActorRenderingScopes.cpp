#include <hksr/framework/presentation/model/ActorRenderingScopes.hpp>
#include <algorithm>
#include <atomic>
#include <set>
#include <stdexcept>
namespace hksr::framework {
namespace {
bool Same(api::EntityOrigin a, api::EntityOrigin b) {
    return a.world == b.world && a.token.entity == b.token.entity && a.token.generation == b.token.generation;
}
void Need(api::RenderingWriteResult result) {
    if (result.state == api::RenderingWriteState::Failed)
        throw std::runtime_error(result.detail);
}
template <class T> void Add(std::vector<T>& rows, const T& item) {
    if (std::find(rows.begin(), rows.end(), item) == rows.end())
        rows.push_back(item);
}
template <class T> void Remove(std::vector<T>& rows, const T& item) {
    rows.erase(std::remove(rows.begin(), rows.end(), item), rows.end());
}
}
struct ActorRenderingScopes::Receiver {
    api::EntityOrigin origin;
    std::unique_ptr<api::ActorRendering> api;
    struct Patch {
        std::uint64_t scope = 0;
        std::uint32_t mask = 0;
        bool enabled = true;
    };
    struct Mask {
        api::RendererMaskValue baseline;
        std::uint32_t last = 0;
        std::vector<Patch> patches;
    };
    std::map<std::uintptr_t, Mask> masks;
    struct OutlinePatch {
        std::uint64_t scope = 0;
        bool reset = false;
        std::vector<std::uintptr_t> add, remove;
    };
    std::optional<api::OutlineTargets> baseline, last_outline;
    std::vector<OutlinePatch> outlines;
    bool outline_uncertain = false;
    std::map<std::pair<std::uint64_t, std::string>, std::string> hidden;
    static std::uint32_t Value(const Mask& row) {
        auto value = row.baseline.value;
        for (const auto& patch : row.patches)
            value = patch.enabled ? value | patch.mask : value & ~patch.mask;
        return value;
    }
    bool ApplyOutline() {
        auto add = baseline->add, remove = baseline->remove;
        for (const auto& row : outlines) {
            if (row.reset) {
                add.clear();
                remove.clear();
            }
            for (auto value : row.add)
                Add(add, value);
            for (auto value : row.remove)
                Add(remove, value);
        }
        // Native reset/add/remove is not atomic. Keep the ownership obligation
        // before entering it; a failed call may have changed only half a table.
        auto observe_failed = [&] {
            try {
                const auto actual = api->Outline();
                if (actual && actual->epoch == baseline->epoch && actual->effect == baseline->effect) {
                    last_outline = actual;
                    outline_uncertain = false;
                }
            } catch (...) {
            }
        };
        outline_uncertain = true;
        api::RenderingWriteResult result;
        try {
            result = api->SetOutline(*baseline, add, remove);
        } catch (...) {
            observe_failed();
            throw;
        }
        if (result.state == api::RenderingWriteState::Failed) {
            observe_failed();
            throw std::runtime_error(result.detail);
        }
        if (result.state == api::RenderingWriteState::Retired) {
            outline_uncertain = false;
            return false;
        }
        // Native filtering may reject a base renderer or a contradictory list
        // item. The recovery marker is the real output, not the requested list.
        const auto actual = api->Outline();
        if (!actual || !(actual->epoch == baseline->epoch) || actual->effect != baseline->effect)
            throw std::runtime_error("outline output could not be observed after write");
        last_outline = actual;
        outline_uncertain = false;
        return true;
    }
    bool OutlineUnchanged() {
        if (!last_outline)
            return true;
        const auto actual = api->Outline();
        if (!actual)
            return false;
        auto same = [](auto a, auto b) {
            std::sort(a.begin(), a.end());
            std::sort(b.begin(), b.end());
            return a == b;
        };
        return actual->epoch == last_outline->epoch && actual->effect == last_outline->effect &&
               same(actual->add, last_outline->add) && same(actual->remove, last_outline->remove);
    }
    bool Release(std::uint64_t scope) {
        bool complete = true;
        for (auto it = masks.begin(); it != masks.end();) {
            auto& row = it->second;
            auto copy = row.patches;
            copy.erase(std::remove_if(copy.begin(), copy.end(), [&](const auto& p) { return p.scope == scope; }),
                       copy.end());
            if (copy.size() == row.patches.size()) {
                ++it;
                continue;
            }
            const auto actual = api->ReadMask(row.baseline);
            if (!actual || *actual != row.last) {
                it = masks.erase(it);
                continue;
            }
            auto proposed = row;
            proposed.patches = copy;
            const auto value = Value(proposed);
            const auto result = api->Mask(row.baseline, value);
            if (result.state == api::RenderingWriteState::Failed) {
                complete = false;
                ++it;
                continue;
            }
            row.patches = std::move(copy);
            row.last = value;
            if (row.patches.empty())
                it = masks.erase(it);
            else
                ++it;
        }
        auto remaining = outlines;
        remaining.erase(
            std::remove_if(remaining.begin(), remaining.end(), [&](const auto& p) { return p.scope == scope; }),
            remaining.end());
        if (remaining.size() != outlines.size()) {
            if (!api->Current()) {
                outlines.clear();
                baseline.reset();
                last_outline.reset();
                outline_uncertain = false;
            } else if (outline_uncertain)
                throw std::runtime_error("partial outline write could not be observed; restoration obligation remains");
            else if (!OutlineUnchanged()) {
                outlines.clear();
                baseline.reset();
                last_outline.reset();
            } else {
                auto prior = outlines;
                outlines = std::move(remaining);
                try {
                    ApplyOutline();
                    if (outlines.empty()) {
                        baseline.reset();
                        last_outline.reset();
                    }
                } catch (...) {
                    outlines = std::move(prior);
                    complete = false;
                }
            }
        }
        for (auto it = hidden.begin(); it != hidden.end();)
            if (it->first.first == scope) {
                const auto result = api->Visible(it->second, true);
                if (result.state == api::RenderingWriteState::Failed) {
                    complete = false;
                    ++it;
                } else
                    it = hidden.erase(it);
            } else
                ++it;
        return complete;
    }
    bool Empty() const {
        return masks.empty() && outlines.empty() && hidden.empty();
    }
};
ActorRenderingScopes::ActorRenderingScopes(Factory factory) : factory_(std::move(factory)) {
    static std::atomic<std::uint64_t> next{0};
    prefix_ = "HKSR.Rendering." + std::to_string(++next) + ".";
}
ActorRenderingScopes::~ActorRenderingScopes() = default;
bool ActorRenderingScopes::Apply(api::EntityOrigin origin, std::uint64_t scope, const VisualRenderingOperation& input) {
    if (!scope)
        throw std::invalid_argument("render operation requires execution scope");
    error_.clear();
    auto found =
        std::find_if(receivers_.begin(), receivers_.end(), [&](const auto& row) { return Same(row->origin, origin); });
    if (found != receivers_.end() && !(*found)->api->Current()) {
        // Old renderers are retired. Remove only our actor-level visibility keys
        // before replacing the client; never replay old Art values on new Art.
        if (!(*found)->api->Close())
            throw std::runtime_error("retired rendering roots could not drain");
        found = receivers_.erase(found);
        found = receivers_.end();
    }
    if (found == receivers_.end()) {
        auto owned = factory_(origin);
        if (!owned)
            return false;
        auto row = std::make_unique<Receiver>();
        row->origin = origin;
        row->api = std::move(owned);
        receivers_.push_back(std::move(row));
        found = std::prev(receivers_.end());
    }
    auto& row = **found;
    return std::visit(
        [&](const auto& op) -> bool {
            using T = std::decay_t<decltype(op)>;
            if constexpr (std::is_same_v<T, VisualRendererMask>) {
                const auto targets = row.api->Renderers(op.node);
                if (!targets || targets->empty())
                    return false;
                for (const auto& target : *targets) {
                    auto at = row.masks.find(target.renderer);
                    if (at != row.masks.end() &&
                        (!(at->second.baseline.epoch == target.epoch) || at->second.last != target.value)) {
                        row.masks.erase(at);
                        at = row.masks.end();
                    }
                    if (at == row.masks.end())
                        at = row.masks.emplace(target.renderer, Receiver::Mask{target, target.value, {}}).first;
                    auto& slot = at->second;
                    slot.patches.erase(
                        std::remove_if(slot.patches.begin(), slot.patches.end(),
                                       [&](const auto& p) { return p.scope == scope && p.mask == op.mask; }),
                        slot.patches.end());
                    slot.patches.push_back({scope, op.mask, op.enabled});
                    slot.last = Receiver::Value(slot);
                    const auto result = row.api->Mask(target, slot.last);
                    Need(result);
                    if (result.state == api::RenderingWriteState::Retired)
                        return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, VisualOutlineTargets>) {
                const auto baseline = row.api->Outline();
                if (!baseline)
                    return false;
                if (row.outline_uncertain)
                    throw std::runtime_error("previous partial outline write is unobserved");
                if (!row.OutlineUnchanged())
                    throw std::runtime_error("outline target slot changed outside this consumer");
                std::vector<std::uintptr_t> add, remove;
                for (const auto& name : op.add) {
                    const auto value = row.api->NamedRenderer(name);
                    if (!value)
                        return false;
                    add.push_back(*value);
                }
                for (const auto& name : op.remove) {
                    const auto value = row.api->NamedRenderer(name);
                    if (!value)
                        return false;
                    remove.push_back(*value);
                }
                if (!row.baseline)
                    row.baseline = baseline;
                bool merged = false;
                if (!row.outlines.empty() && row.outlines.back().scope == scope) {
                    auto& last = row.outlines.back();
                    if (op.reset) {
                        last = {scope, true, std::move(add), std::move(remove)};
                        merged = true;
                    } else {
                        auto intersects = [](const auto& a, const auto& b) {
                            for (auto value : a)
                                if (std::find(b.begin(), b.end(), value) != b.end())
                                    return true;
                            return false;
                        };
                        if (!intersects(add, last.remove) && !intersects(remove, last.add)) {
                            for (auto value : add)
                                Add(last.add, value);
                            for (auto value : remove)
                                Add(last.remove, value);
                            merged = true;
                        }
                    }
                }
                if (!merged)
                    row.outlines.push_back({scope, op.reset, std::move(add), std::move(remove)});
                return row.ApplyOutline();
            } else {
                const auto key = std::make_pair(scope, op.key);
                const auto physical = prefix_ + std::to_string(scope) + "." + op.key;
                if (!op.visible)
                    row.hidden[key] = physical;
                const auto result = row.api->Visible(physical, op.visible);
                Need(result);
                if (op.visible)
                    row.hidden.erase(key);
                return result.state == api::RenderingWriteState::Applied;
            }
        },
        input.command);
}
bool ActorRenderingScopes::Release(std::uint64_t scope) {
    error_.clear();
    bool complete = true;
    for (auto it = receivers_.begin(); it != receivers_.end();)
        try {
            if (!(*it)->Release(scope)) {
                complete = false;
                ++it;
            } else if ((*it)->Empty() && (*it)->api->Close())
                it = receivers_.erase(it);
            else
                ++it;
        } catch (const std::exception& e) {
            error_ = e.what();
            complete = false;
            ++it;
        }
    if (!complete && error_.empty())
        error_ = "owned rendering restoration failed";
    return complete;
}
bool ActorRenderingScopes::Close() {
    std::set<std::uint64_t> scopes;
    for (const auto& row : receivers_) {
        for (const auto& [id, mask] : row->masks) {
            (void)id;
            for (const auto& p : mask.patches)
                scopes.insert(p.scope);
        }
        for (const auto& p : row->outlines)
            scopes.insert(p.scope);
        for (const auto& [key, value] : row->hidden) {
            (void)value;
            scopes.insert(key.first);
        }
    }
    bool complete = true;
    for (auto scope : scopes)
        complete = Release(scope) && complete;
    for (auto it = receivers_.begin(); it != receivers_.end();)
        if ((*it)->Empty() && (*it)->api->Close())
            it = receivers_.erase(it);
        else {
            complete = false;
            ++it;
        }
    return complete;
}
bool ActorRenderingScopes::ReleaseParticipant(api::EntityOrigin origin) {
    bool complete = true;
    for (auto it = receivers_.begin(); it != receivers_.end();) {
        auto& row = **it;
        if (!Same(row.origin, origin)) {
            ++it;
            continue;
        }
        std::set<std::uint64_t> scopes;
        for (const auto& [id, mask] : row.masks) {
            (void)id;
            for (const auto& p : mask.patches)
                scopes.insert(p.scope);
        }
        for (const auto& p : row.outlines)
            scopes.insert(p.scope);
        for (const auto& [key, value] : row.hidden) {
            (void)value;
            scopes.insert(key.first);
        }
        bool done = true;
        for (auto scope : scopes)
            done = row.Release(scope) && done;
        if (done && row.api->Close())
            it = receivers_.erase(it);
        else {
            complete = false;
            ++it;
        }
    }
    return complete;
}
std::size_t ActorRenderingScopes::RetainedContributions() const {
    std::size_t count = 0;
    for (const auto& row : receivers_) {
        for (const auto& [id, mask] : row->masks) {
            (void)id;
            count += mask.patches.size();
        }
        count += row->outlines.size() + row->hidden.size();
    }
    return count;
}
}
