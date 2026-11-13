#pragma once
#include <hksr/entity/EntityLifecycle.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hksr::api {
class AttachmentVisibility;
class AttachmentScale;
class ActorVisualConfiguration;
class ActorTiming;
}
namespace hksr::framework {
class ActorModelScopes;
class ActorBgmScopes;
class ActorTimingScopes;
struct VisualModelOperation;
namespace detail {
enum class BodyDrain { Retire, ModelChange };
struct BodyReleaseResult {
    bool complete = true;
    std::string error;
};
// All per-model contributions held by one visual scene. Asynchronous Close
// retains its owner until complete; timing scopes drain before their provider.
struct VisualBodyResources {
    VisualBodyResources();
    ~VisualBodyResources(); // no engine calls
    std::unique_ptr<api::AttachmentVisibility> attachments;
    std::unique_ptr<api::AttachmentScale> attachment_scale;
    std::unique_ptr<ActorModelScopes> model_scopes;
    std::unique_ptr<api::ActorVisualConfiguration> visual_configuration;
    std::unique_ptr<ActorBgmScopes> bgm;
    std::unique_ptr<api::ActorTiming> timing;
    std::unique_ptr<ActorTimingScopes> timing_scopes;
    struct ForeignTiming {
        api::EntityOrigin origin;
        std::unique_ptr<api::ActorTiming> timing;
        std::unique_ptr<ActorTimingScopes> scopes;
    };
    std::vector<ForeignTiming> foreign_timings;
    bool ConfigurationCurrent(const VisualModelOperation&) const;
    BodyReleaseResult Release(std::uint64_t, bool natural);
    bool ReleaseTiming(std::uint64_t, bool natural);
    std::string TimingError() const;
    bool CloseForeignTiming();
    // Retirement preserves serial draining. A model handoff also advances
    // independent resources when a sibling is pending, without dropping it.
    bool Close(BodyDrain);
    void Reset(); // after Close succeeds and shared contributions have drained
};
}
}
