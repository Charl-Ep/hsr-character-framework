#include "../../support/LegacyScriptFixture.hpp"
#include <hksr/framework/presentation/scene/VisualProgramPlayer.hpp>
#include <hksr/presentation/effects/EffectLayouts.hpp>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace hksr;
using namespace hksr::framework;
namespace {
void CheckAt(bool value, int line) {
    if (!value)
        throw std::runtime_error("layout consumer contract line " + std::to_string(line));
}
#define Check(value) CheckAt((value), __LINE__)
int manager = 0, instance = 0, next_root = 0;
std::map<int, void*> roots;
struct Params {
    unsigned id;
    bool released = false;
    int state = 3;
};
Params a{1}, b{2}, extra{3};
template <class T> api::Method M(T call) {
    return {reinterpret_cast<void*>(call), nullptr};
}
int __fastcall Keep(void* object, int, int, void*) {
    roots[++next_root] = object;
    return next_root;
}
void* __fastcall Target(int root, void*) {
    return roots.at(root);
}
void __fastcall Free(int root, void*) {
    roots.erase(root);
}
unsigned __fastcall Id(Params* p, void*) {
    return p->id;
}
bool __fastcall Released(Params* p, void*) {
    return p->released;
}
int __fastcall State(Params* p, void*) {
    return p->state;
}
void* __fastcall Instance(Params*, void*) {
    return &instance;
}
std::vector<unsigned> removal_order;
bool __fastcall Remove(void*, api::EffectReference ref, bool, void*) {
    auto* p = static_cast<Params*>(ref.parameters);
    p->released = true;
    removal_order.push_back(p->id);
    return true;
}
std::shared_ptr<const api::PresentationEffects> Effects() {
    api::EffectAccessMethods access;
    access.id = M(Id);
    access.released = M(Released);
    access.state = M(State);
    access.instance = M(Instance);
    access.remove = M(Remove);
    api::EffectRequestServices requests{{M(Keep), M(Target), M(Free), {}, {}}, api::EffectAccess(access), {}, {}, {}};
    api::EffectActionServices actions;
    actions.remove = [](void* service, api::EffectReference ref, const api::EffectRemovalOptions& options) {
        Check(!options.immediate_fade && !options.unbind);
        api::EffectRemovalResult result;
        result.removal.value = Remove(service, ref, options.fade, nullptr);
        result.removal.read = {true, 0};
        result.input_release = {true, 0};
        return result;
    };
    return std::make_shared<api::PresentationEffects>(std::move(requests), api::EffectControlServices{},
                                                      api::ConfigureEntityEffect{}, std::move(actions));
}
std::shared_ptr<const VisualInstructionStatus> Run(VisualProgramPlayer& player, std::uint64_t sequence,
                                                   const VisualInstruction& instruction) {
    const auto receipt = player.SubmitInstruction(sequence, instruction);
    for (int n = 0; n < 5 && !receipt->complete; ++n)
        player.Service();
    Check(receipt->complete);
    return receipt;
}
void Test() {
    const auto program = std::make_shared<const VisualProgram>(
        ParseLegacyVisualProgram(R"({"format":"hksr.visual-program","id":"layout","source":"fixture","scope":"owned",
 "effects":{"a":{"path":"a.prefab","target":"Caster"},"b":{"path":"b.prefab","target":"Caster"}},"actions":{"play":{"state":"Hold","enter":[
 {"op":"create","source":"create","effects":["a","b"]},
 {"op":"effect_layout","source":"transfer","effect":"a","command":"transfer","target":"b","fade":true},
 {"op":"effect_layout","source":"perform","effect":"a","command":"perform","perform":0},
 {"op":"remove","source":"target-exit","effects":["b"],"fade":false},
 {"op":"remove","source":"source-exit","effects":["a"],"fade":false}]}}})"));
    auto effects = Effects();
    unsigned calls = 0;
    bool unsupported = false;
    std::shared_ptr<VisualEffectContext> active;
    api::EffectLayouts layouts(
        {[&](api::EffectReference ref, const api::EffectLayoutCommand& command) {
             ++calls;
             Check(ref.parameters == &a);
             if (unsupported)
                 return api::EffectControlResult{api::EffectControlState::Unsupported, {}, true, "plugin absent"};
             Check(command.action == api::EffectLayoutAction::Transfer && command.target.parameters == &b &&
                   command.fade);
             if (calls == 1) {
                 active->Track("b", &manager, {3, 0, &extra});
                 return api::EffectControlResult{api::EffectControlState::NotLoaded, {}, true, "loading"};
             }
             return api::EffectControlResult{api::EffectControlState::Applied, {true, 0}, true, {}};
         },
         {}});
    VisualProgramServices services;
    services.effects = effects;
    // These fixture effects were already submitted by the external producer.
    // Adoption must not claim a new Start/Submit receipt (Track preserves it).
    services.create = [&](auto& context, const auto& key, const auto&) {
        auto* p = key == "a" ? &a : &b;
        return VisualEffectAdmission{VisualEffectAdmissionKind::Replayed, context.Track(key, &manager, {p->id, 0, p})};
    };
    services.prepare_inputs = [&](const auto& context, const auto&, bool, bool) { active = context; };
    services.layout = [&](auto& context, auto source, auto target, const VisualEffectLayout& op) {
        api::EffectLayoutCommand command;
        command.action = op.action;
        command.perform = op.perform;
        command.fade = op.fade;
        const auto result = context.OperateLayout(layouts, source, target, command);
        return VisualStepResult{result.state == api::EffectControlState::Applied       ? VisualStepState::Done
                                : result.state == api::EffectControlState::NotLoaded   ? VisualStepState::Pending
                                : result.state == api::EffectControlState::Unsupported ? VisualStepState::Skipped
                                                                                       : VisualStepState::Failed,
                                result.detail};
    };
    VisualProgramPlayer player(program, std::move(services));
    const auto sequence = player.BeginSequence({}, true, 0, false);
    const auto& steps = program->actions.at("play").enter;
    Check(Run(player, sequence, steps[0])->issues.empty());
    Check(Run(player, sequence, steps[1])->issues.empty() && calls == 2);
    // A second target was added while pending, but the original pair was fixed
    // at admission and was not rebound by unique-name or current-key lookup.
    Check(active->Requests("b").size() == 2);
    unsupported = true;
    const auto skipped = Run(player, sequence, steps[2]);
    Check(skipped->skipped && skipped->issues.empty());
    Check(Run(player, sequence, steps[3])->issues.empty());
    Check(Run(player, sequence, steps[4])->issues.empty());
    Check(removal_order.size() == 3 && removal_order[0] == 2 && removal_order[1] == 3 && removal_order[2] == 1);
    player.EndSequence(sequence, true);
    for (int n = 0; n < 4; ++n)
        player.Service();
    Check(player.SequenceDrained(sequence));
    Check(roots.empty() && layouts.Close());
    bool rejected = false;
    try {
        ParseLegacyVisualProgram(
            R"({"format":"hksr.visual-program","id":"bad","source":"fixture","scope":"owned","effects":{"a":{"path":"a","target":"Caster"}},"actions":{"x":{"state":"Hold","enter":[{"op":"effect_layout","source":"bad","effect":"a","command":"transfer","target":"a"}]}}})");
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    Check(rejected);
}
}
int main() try {
    Test();
    std::cout << "Layout typed codec, fixed owned receipt pair, pending/skip and declared removal order passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
