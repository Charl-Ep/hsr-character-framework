#include <hksr/framework/presentation/effects/VisualEffectContext.hpp>
#include <hksr/presentation/effects/EffectLayouts.hpp>
namespace hksr::framework {
api::EffectControlResult VisualEffectContext::OperateLayout(api::EffectLayouts& layouts, VisualEffectId source,
                                                            VisualEffectId target, api::EffectLayoutCommand command) {
    struct Guard {
        unsigned& count;
        explicit Guard(unsigned& value) : count(value) {
            ++count;
        }
        ~Guard() {
            --count;
        }
    } guard(operations_);
    auto receipt = [&](VisualEffectId id) -> const api::EffectRequestStatus* {
        const auto found = requests_.find(id);
        if (closing_ || found == requests_.end() || !found->second.retained)
            return nullptr;
        const auto& status = found->second.request->Status();
        return status.submitted && !status.cancelled && !status.request_ended && !status.closed ? &status : nullptr;
    };
    const auto* from = receipt(source);
    if (!from)
        return {api::EffectControlState::Ended, {}, true, "owned layout source receipt ended"};
    const auto ref = from->reference;
    if (command.action == api::EffectLayoutAction::Transfer) {
        const auto* to = receipt(target);
        if (!to)
            return {api::EffectControlState::Ended, {}, true, "owned layout target receipt ended"};
        command.target = to->reference;
    }
    return layouts.Execute(ref, command);
}
}
