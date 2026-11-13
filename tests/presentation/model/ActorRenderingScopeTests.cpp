#include "../../support/LegacyScriptFixture.hpp"
#include <hksr/framework/presentation/model/ActorRenderingScopes.hpp>
#include <hksr/framework/presentation/scene/VisualProgramPlayer.hpp>
#include <hksr/presentation/effects/PresentationEffects.hpp>
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
using namespace hksr;
using namespace hksr::framework;
namespace {
void Check(bool ok) {
    if (!ok)
        throw std::runtime_error("renderer scope contract");
}
api::EntityOrigin Origin(int id) {
    return {{reinterpret_cast<api::GameEntity*>(static_cast<std::uintptr_t>(id)), static_cast<std::uint64_t>(id)},
            reinterpret_cast<api::GameWorld*>(1)};
}
struct Actor {
    api::RenderingEpoch epoch{1, 2, 3};
    bool current = true, fail_mask = false, fail_outline = false, fail_read = false, filter_weapon_add = false,
         fail_observe = false;
    unsigned unread_outline = 0;
    std::map<std::uintptr_t, std::uint32_t> masks{{10, 525}, {11, 1}};
    std::vector<std::uintptr_t> add{10}, remove{11};
    std::set<std::string> hidden;
    unsigned writes = 0, closes = 0, bindings = 0;
    std::unique_ptr<api::ActorRendering> Bind() {
        ++bindings;
        api::ActorRenderingServices calls;
        calls.current = [&] { return current; };
        calls.close = [&] {
            ++closes;
            return true;
        };
        calls.renderers = [&](const auto& node) -> std::optional<std::vector<api::RendererMaskValue>> {
            if (fail_read)
                throw std::runtime_error("read failed");
            if (!current || node == "missing")
                return {};
            std::vector<api::RendererMaskValue> out;
            for (const auto& [id, value] : masks)
                if (node.empty() || id == 10)
                    out.push_back({epoch, id, value});
            return out;
        };
        calls.read_mask = [&](const auto& target) -> std::optional<std::uint32_t> {
            return current && target.epoch == epoch ? std::optional<std::uint32_t>{masks.at(target.renderer)}
                                                    : std::nullopt;
        };
        calls.mask = [&](const auto& target, auto value) {
            if (!current || !(target.epoch == epoch))
                return api::RenderingWriteResult{api::RenderingWriteState::Retired, {}};
            ++writes;
            masks.at(target.renderer) = value;
            const bool failed = fail_mask;
            fail_mask = false;
            return api::RenderingWriteResult{
                failed ? api::RenderingWriteState::Failed : api::RenderingWriteState::Applied, "mask partial"};
        };
        calls.outline = [&]() -> std::optional<api::OutlineTargets> {
            if (unread_outline) {
                --unread_outline;
                throw std::runtime_error("outline observation failed");
            }
            return current ? std::optional<api::OutlineTargets>{{epoch, 20, add, remove}} : std::nullopt;
        };
        calls.named_renderer = [](const auto& name) -> std::optional<std::uintptr_t> {
            return name == "Weapon" ? std::optional<std::uintptr_t>{12} : std::nullopt;
        };
        calls.set_outline = [&](const auto& target, const auto& added, const auto& removed) {
            Check(target.epoch == epoch && target.effect == 20);
            ++writes;
            const bool failed = fail_outline;
            fail_outline = false;
            if (failed) {
                add.clear();
                remove.clear();
                if (!added.empty())
                    add.push_back(added.front());
                if (fail_observe)
                    unread_outline = 1;
            } else {
                add = added;
                remove = removed;
                if (filter_weapon_add)
                    add.erase(std::remove(add.begin(), add.end(), 12), add.end());
            }
            return api::RenderingWriteResult{
                failed ? api::RenderingWriteState::Failed : api::RenderingWriteState::Applied, "outline partial"};
        };
        calls.visible = [&](const auto& key, bool visible) {
            ++writes;
            if (visible)
                hidden.erase(key);
            else
                hidden.insert(key);
            return api::RenderingWriteResult{api::RenderingWriteState::Applied, {}};
        };
        return std::make_unique<api::ActorRendering>(std::move(calls));
    }
};
VisualRenderingOperation Mask(bool enabled = true, const std::string& node = {}) {
    return {VisualRendererMask{node, 64, enabled}};
}
void Overlap() {
    Actor actor, other;
    ActorRenderingScopes scopes(
        [&](auto origin) { return origin.token.generation == 1 ? actor.Bind() : other.Bind(); });
    Check(scopes.Apply(Origin(1), 100, Mask()) && scopes.Apply(Origin(1), 200, Mask()));
    Check(actor.bindings == 1 && actor.masks[10] == (525 | 64));
    Check(scopes.Release(100) && actor.masks[10] == (525 | 64));
    Check(scopes.Release(200) && actor.masks[10] == 525 && actor.masks[11] == 1);
    Check(scopes.Apply(Origin(1), 300, Mask()) && scopes.Apply(Origin(2), 300, Mask()));
    Check(scopes.ReleaseParticipant(Origin(1)) && actor.masks[10] == 525 && other.masks[10] == (525 | 64));
    Check(scopes.Release(300) && other.masks[10] == 525);
    Check(scopes.Apply(Origin(1), 400, {VisualOutlineTargets{false, {}, {"Weapon"}}}));
    Check(actor.remove == std::vector<std::uintptr_t>({11, 12}));
    for (int n = 0; n < 200; ++n)
        Check(scopes.Apply(Origin(1), 400, {VisualOutlineTargets{false, {}, {"Weapon"}}}));
    Check(scopes.RetainedContributions() == 1);
    Check(scopes.Apply(Origin(1), 500, {VisualOutlineTargets{true, {"Weapon"}, {}}}));
    Check(actor.add == std::vector<std::uintptr_t>{12} && actor.remove.empty());
    Check(scopes.Release(400) && actor.add == std::vector<std::uintptr_t>{12});
    Check(scopes.Release(500) && actor.add == std::vector<std::uintptr_t>{10} &&
          actor.remove == std::vector<std::uintptr_t>{11});
    Check(scopes.Apply(Origin(1), 600, {VisualEntityVisible{"hide", false}}));
    Check(scopes.Apply(Origin(1), 700, {VisualEntityVisible{"hide", false}}) && actor.hidden.size() == 2);
    Check(scopes.Release(600) && actor.hidden.size() == 1);
    Check(scopes.Release(700) && actor.hidden.empty());
    Check(scopes.Close());
    actor.filter_weapon_add = true;
    Check(scopes.Apply(Origin(1), 800, {VisualOutlineTargets{true, {"Weapon"}, {}}}) && actor.add.empty() &&
          actor.remove.empty());
    Check(scopes.Release(800) && actor.add == std::vector<std::uintptr_t>{10} &&
          actor.remove == std::vector<std::uintptr_t>{11});
    Check(scopes.Close());
}
void Failures() {
    Actor actor;
    ActorRenderingScopes scopes([&](auto) { return actor.Bind(); });
    const auto before = actor.writes;
    Check(!scopes.Apply(Origin(1), 1, Mask(true, "missing")) && actor.writes == before);
    Check(!scopes.Apply(Origin(1), 1, {VisualOutlineTargets{false, {"unknown"}, {}}}) && actor.writes == before);
    actor.fail_read = true;
    bool failed = false;
    try {
        scopes.Apply(Origin(1), 1, Mask());
    } catch (const std::runtime_error&) {
        failed = true;
    }
    Check(failed);
    actor.fail_read = false;
    actor.fail_mask = true;
    failed = false;
    try {
        scopes.Apply(Origin(1), 1, Mask());
    } catch (const std::runtime_error&) {
        failed = true;
    }
    Check(failed && scopes.Release(1) && actor.masks[10] == 525);
    actor.fail_outline = true;
    failed = false;
    try {
        scopes.Apply(Origin(1), 2, {VisualOutlineTargets{false, {"Weapon"}, {}}});
    } catch (const std::runtime_error&) {
        failed = true;
    }
    Check(failed && scopes.Release(2) && actor.add == std::vector<std::uintptr_t>{10});
    Check(scopes.Apply(Origin(1), 3, Mask()));
    actor.masks[10] = 2048;
    Check(scopes.Release(3) && actor.masks[10] == 2048);
    Check(scopes.Apply(Origin(1), 4, {VisualOutlineTargets{false, {"Weapon"}, {}}}));
    actor.add = {11};
    Check(scopes.Release(4) && actor.add == std::vector<std::uintptr_t>{11});
    Check(scopes.Apply(Origin(1), 5, Mask()));
    actor.current = false;
    const auto writes = actor.writes;
    Check(scopes.Release(5) && actor.writes == writes);
    Check(scopes.Close());
    actor.current = true;
    actor.fail_outline = true;
    actor.fail_observe = true;
    failed = false;
    try {
        scopes.Apply(Origin(1), 6, {VisualOutlineTargets{true, {"Weapon"}, {}}});
    } catch (const std::runtime_error&) {
        failed = true;
    }
    Check(failed && !scopes.Release(6) && scopes.RetainedContributions() == 1 && !scopes.Error().empty());
    actor.current = false;
    Check(scopes.Release(6) && scopes.RetainedContributions() == 0 && scopes.Close());
}
void ParserConsumer() {
    auto program = std::make_shared<const VisualProgram>(ParseLegacyVisualProgram(
        R"({"format":"hksr.visual-program","id":"render","source":"fixture","scope":"owned","effects":{},"actions":{"hold":{"state":"Hold","enter":[
    {"op":"renderer_mask","mask":64,"enabled":true,"node":"Weapon","source":"mask"},
    {"op":"outline_targets","reset":false,"add":[],"remove":["Weapon"],"recipient":"Recipient","source":"outline"},
    {"op":"entity_visible","key":"fx.hide","visible":false,"source":"visible"}]}}})"));
    Check(std::get<VisualRenderingOperation>(program->actions.at("hold").enter[1].operation).recipient == "Recipient");
    Actor actor;
    ActorRenderingScopes scopes([&](auto) { return actor.Bind(); });
    VisualProgramServices calls;
    calls.effects =
        std::make_shared<api::PresentationEffects>(api::EffectRequestServices{{}, api::EffectAccess({}), {}, {}, {}});
    calls.create = [](auto&, const auto&, const auto&) -> VisualEffectId {
        throw std::runtime_error("unexpected effect");
    };
    calls.rendering = [&](auto, const auto& op) {
        return VisualStepResult{scopes.Apply(Origin(1), 99, op) ? VisualStepState::Done : VisualStepState::Skipped, {}};
    };
    VisualProgramPlayer player(program, std::move(calls));
    const auto sequence = player.BeginSequence({}, true, 0, false);
    for (const auto& command : program->actions.at("hold").enter) {
        const auto receipt = player.SubmitInstruction(sequence, command);
        player.Service();
        Check(receipt->complete && receipt->issues.empty());
    }
    Check(actor.masks[10] == (525 | 64) && actor.remove == std::vector<std::uintptr_t>({11, 12}) &&
          actor.hidden.size() == 1);
    player.EndSequence(sequence, true);
    Check(scopes.Release(99) && actor.masks[10] == 525 && actor.remove == std::vector<std::uintptr_t>{11} &&
          actor.hidden.empty());
    player.Service();
}
}
int main() try {
    Overlap();
    Failures();
    ParserConsumer();
    std::cout << "Renderer shared receiver scopes, failure withdrawal and parser/consumer passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
