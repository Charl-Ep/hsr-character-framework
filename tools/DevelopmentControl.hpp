#pragma once
#include <hksr/framework/application/ApplicationService.hpp>
#include <hksr/framework/presentation/VisualProgramRuntime.hpp>
#include <set>

namespace hksr::research {
// Development-only transport of existing menu commands. No game pointers,
// direct calls, process launch/termination, or alternative action player.
class DevelopmentControl {
public:
    struct Services {
        std::function<std::uint64_t(framework::CharacterCommand,framework::ApplicationService::ReplySink)> post;
        std::function<framework::ApplicationSnapshot()> snapshot;
        std::function<void(std::uint64_t,framework::VisualProgramParameters)> visual_inputs;
    };
    explicit DevelopmentControl(unsigned process,Services services);
    void Submit(const std::string& text); // worker; parses each request id once
    std::vector<std::string> Drain(); // encoded transport records, worker only
private:
    struct Replies;
    std::shared_ptr<Replies> replies_;
    Services services_;
    unsigned process_;
    std::set<std::string> seen_;
};
}
