#pragma once
#include <hksr/model/ActorVisualConfiguration.hpp>
#include <algorithm>
#include <functional>
#include <stdexcept>

namespace hksr::framework::detail {
// The caller observes the model and real event consumer before binding. A
// loading/replaced consumer is not an unsupported event-configuration contract.
inline bool PrepareSkinEventInputs(std::unique_ptr<api::ActorVisualConfiguration>& binding,
                                   const std::function<bool()>& ready, const std::vector<std::string>& files,
                                   const std::function<std::unique_ptr<api::ActorVisualConfiguration>()>& create) {
    auto retire = [&] {
        if (binding && !binding->Close())
            return false;
        binding.reset();
        return true;
    };
    auto current = [&] {
        return binding->Current(api::VisualConfigurationComponent::Data) &&
               binding->Current(api::VisualConfigurationComponent::Events);
    };
    if (binding && !current() && !retire())
        return false;
    if (!ready())
        return false;
    if (!binding)
        binding = create();
    if (!binding)
        return false;
    if (!current() || !ready()) {
        retire();
        return false;
    }
    api::ActorVisualConfigurationInput input;
    input.event_files = files;
    const auto result = binding->Apply(input);
    // Applying events may reenter model lifecycle work. Do not mark an old
    // consumer adopted if that work replaced the binding during the call.
    if (!current() || !ready()) {
        retire();
        return false;
    }
    if (std::find(result.unsupported_fields.begin(), result.unsupported_fields.end(), "event_files") !=
        result.unsupported_fields.end())
        throw std::runtime_error("skin animation event replacement unavailable");
    return true;
}
}
