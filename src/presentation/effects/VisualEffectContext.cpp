#include <hksr/framework/presentation/effects/VisualEffectContext.hpp>
#include <hksr/framework/presentation/movement/VisualEntityMotion.hpp>
#include <stdexcept>

namespace hksr::framework {
VisualContextWork::VisualContextWork(std::shared_ptr<VisualEffectContext> context) : context_(std::move(context)) {
    if (!context_ || context_->Closing() || context_->Finishing())
        throw std::logic_error("visual work context ended");
    ++context_->producers_;
}
VisualContextWork::~VisualContextWork() {
    --context_->producers_;
}
namespace {
struct Operation {
    unsigned& count;
    explicit Operation(unsigned& c) : count(c) {
        ++count;
    }
    ~Operation() {
        --count;
    }
};
}
VisualEffectContext::VisualEffectContext(std::shared_ptr<const api::PresentationEffects> services)
    : services_(std::move(services)) {
    if (!services_)
        throw std::invalid_argument("visual effect services required");
}
std::optional<VisualEffectAdmission> VisualEffectContext::CheckMutex(const std::string& key, void* target, void* path,
                                                                     const std::string& mapped_path,
                                                                     api::EffectMutexRule rule) {
    if (finishing_ || closing_)
        throw std::logic_error("visual context no longer accepts creation");
    Operation operation(operations_);
    // TriggerEffect consults the public override only when its own interval
    // is zero. ForceEntityMutex belongs to AttackData, not this path.
    if (rule.interval == 0) {
        const auto rules = services_->ReadHitRules(mapped_path);
        if (rules.configured)
            rule = *rules.configured;
    }
    if (finishing_ || closing_)
        throw std::logic_error("visual context ended during effect admission");
    // Both official query branches return None at zero effective interval.
    // No target or existing effect is needed in that case.
    if (rule.interval == 0)
        return {};
    std::map<VisualEffectId, api::EffectReference> owned;
    std::vector<api::EffectReference> candidates;
    for (const auto& [id, entry] : requests_) {
        const auto& status = entry.request->Status();
        if (entry.key != key || !entry.retained || !status.submitted || status.cancelled || status.closed ||
            status.request_ended)
            continue;
        owned.emplace(id, status.reference);
        candidates.push_back(status.reference);
    }
    const auto decision = services_->HitMutex(nullptr, target, path, rule, 0, candidates);
    if (!decision.kind.read.returned || !decision.error.empty())
        throw std::runtime_error("effect mutex: " + decision.error);
    if (finishing_ || closing_)
        throw std::logic_error("visual context ended during effect admission");
    if (decision.kind.value == api::EffectMutexKind::None)
        return {};
    if (decision.kind.value != api::EffectMutexKind::Default && decision.kind.value != api::EffectMutexKind::FadeOut)
        throw std::runtime_error("unknown effect mutex result");
    if (decision.replay)
        for (const auto& [id, ref] : owned) {
            if (ref.parameters != decision.replay->parameters || ref.id != decision.replay->id)
                continue;
            const auto found = requests_.find(id);
            if (found == requests_.end())
                break;
            const auto result = found->second.request->Replay(decision.kind.value);
            if (result.state == api::EffectControlState::Applied)
                return VisualEffectAdmission{VisualEffectAdmissionKind::Replayed, id};
            if (result.state != api::EffectControlState::NoMatch && result.state != api::EffectControlState::Ended &&
                result.state != api::EffectControlState::NotLoaded)
                throw std::runtime_error("effect replay: " + result.detail);
            break;
        }
    return VisualEffectAdmission{VisualEffectAdmissionKind::Suppressed};
}
VisualEffectId VisualEffectContext::Start(const std::string& key, void* entity, void* path) {
    if (finishing_ || closing_)
        throw std::logic_error("visual context no longer accepts creation");
    if (key.empty() || !entity || !path)
        throw std::invalid_argument("visual effect input absent");
    Operation operation(operations_);
    const auto id = ++next_;
    auto request = services_->CreateRequest("visual effect request=" + std::to_string(id) + " key=" + key + " ");
    auto* live = request.get();
    requests_.emplace(id, Entry{key, std::move(request)});
    // Register before a potentially synchronous load/completion. End called
    // from a callback cancels this same request, not the next action's output.
    live->Start(entity, path);
    return id;
}
void VisualEffectContext::Remove(const std::string& key, bool fade) {
    for (auto& item : requests_)
        if (item.second.key == key)
            item.second.request->Cancel(fade);
}
VisualEffectId VisualEffectContext::StartEntity(const std::string& key, void* caster, void* target, void* path,
                                                const api::EntityEffectOptions& options,
                                                const api::EffectSubmissionOptions& submission) {
    if (finishing_ || closing_)
        throw std::logic_error("visual context no longer accepts creation");
    if (key.empty() || !caster || !target || !path)
        throw std::invalid_argument("visual entity effect input absent");
    Operation operation(operations_);
    const auto id = ++next_;
    auto request = services_->CreateRequest("visual entity effect request=" + std::to_string(id) + " key=" + key + " ");
    auto* live = request.get();
    requests_.emplace(id, Entry{key, std::move(request)});
    live->StartEntity(caster, target, path, options, submission);
    return id;
}
VisualEffectId VisualEffectContext::Track(const std::string& key, void* manager, api::EffectReference reference) {
    if (key.empty() || !manager || !reference.parameters)
        throw std::invalid_argument("visual effect receipt absent");
    for (const auto& item : requests_) {
        const auto& current = item.second.request->Status().reference;
        if (current.parameters != reference.parameters || current.id != reference.id)
            continue;
        if (!item.second.retained)
            throw std::runtime_error("visual effect receipt retention incomplete");
        if (item.second.manager && item.second.manager != manager)
            throw std::logic_error("visual effect receipt has conflicting managers");
        if (item.second.key != key)
            throw std::logic_error("visual effect receipt already has a different key");
        return item.first; // e.g. async entry forwarding to the synchronous entry
    }
    Operation operation(operations_);
    const auto id = ++next_;
    auto request = services_->CreateRequest("visual effect receipt=" + std::to_string(id) + " key=" + key + " ");
    auto* live = request.get();
    requests_.emplace(id, Entry{key, std::move(request), false, manager});
    if (closing_)
        live->Cancel(end_fade_);
    if (!live->TrackSubmitted(manager, reference)) {
        throw std::runtime_error("visual effect receipt not retained; caller must preserve ownership");
    }
    requests_.at(id).retained = true;
    return id;
}
VisualEffectId VisualEffectContext::StartSpatial(const std::string& key, void* caster, void* path,
                                                 const api::SpatialEffectOptions& options,
                                                 const api::EffectSubmissionOptions& submission) {
    if (finishing_ || closing_)
        throw std::logic_error("visual context no longer accepts creation");
    if (key.empty() || !caster || !path)
        throw std::invalid_argument("visual spatial effect input absent");
    Operation operation(operations_);
    const auto id = ++next_;
    auto request =
        services_->CreateRequest("visual spatial effect request=" + std::to_string(id) + " key=" + key + " ");
    auto* live = request.get();
    requests_.emplace(id, Entry{key, std::move(request)});
    live->StartSpatial(caster, path, options, submission);
    return id;
}
void VisualEffectContext::Finish() {
    finishing_ = true;
}
void VisualEffectContext::End(bool fade) {
    closing_ = true;
    end_fade_ = fade;
    for (auto& item : requests_)
        item.second.request->Cancel(fade);
}
void VisualEffectContext::Service() {
    if (servicing_ || operations_)
        return;
    servicing_ = true;
    struct Exit {
        bool& flag;
        ~Exit() {
            flag = false;
        }
    } exit{servicing_};
    if (motion_)
        motion_->Collect();
    // New requests created reentrantly are serviced at the next boundary.
    const auto last = next_;
    for (auto it = requests_.begin(); it != requests_.end() && it->first <= last;) {
        const auto ref = it->second.request->Status().reference;
        if (motion_ && it->second.request->Status().cancelled)
            motion_->Release(ref);
        // Even if explicit detachment failed, preserve official request
        // cancellation/Dispose. That path also owns plugin cleanup.
        it->second.request->Service();
        if (motion_ && it->second.request->Status().request_ended)
            motion_->RequestEnded(ref);
        if (it->second.request->Status().closed)
            it = requests_.erase(it);
        else
            ++it;
    }
}
void VisualEffectContext::UseEntityMotion(std::shared_ptr<VisualEntityMotion> motion, api::EntityOrigin owner) {
    if (!motion || (motion_ && (motion_ != motion || owner_.token.entity != owner.token.entity ||
                                owner_.token.generation != owner.token.generation || owner_.world != owner.world)))
        throw std::logic_error("visual context motion owner changed");
    motion_ = std::move(motion);
    owner_ = owner;
}
api::EffectControlResult VisualEffectContext::BindTargets(VisualEffectId id,
                                                          const std::vector<api::EntityOrigin>& targets) {
    Operation operation(operations_);
    const auto found = requests_.find(id);
    if (!motion_)
        return {api::EffectControlState::Unsupported, {}, true, "visual entity motion service absent"};
    if (found == requests_.end() || !found->second.retained || found->second.request->Status().closed ||
        found->second.request->Status().request_ended)
        return {api::EffectControlState::Ended, {}, true, "visual receipt ended"};
    const auto& status = found->second.request->Status();
    if (targets.empty())
        return motion_->Release(status.reference);
    if (closing_ || status.cancelled)
        return {api::EffectControlState::Ended, {}, true, "visual receipt closing"};
    return motion_->Bind(status.reference, owner_, targets);
}
const api::EffectRequestStatus* VisualEffectContext::Status(VisualEffectId id) const {
    const auto it = requests_.find(id);
    return it == requests_.end() ? nullptr : &it->second.request->Status();
}
std::vector<VisualEffectId> VisualEffectContext::Matching(const std::string& key) const {
    std::vector<VisualEffectId> out;
    if (closing_)
        return out;
    for (const auto& item : requests_) {
        const auto& status = item.second.request->Status();
        if (item.second.retained && item.second.key == key && status.submitted && !status.request_ended &&
            !status.closed)
            out.push_back(item.first);
    }
    return out;
}
std::vector<VisualEffectResult> VisualEffectContext::SetProgress(const std::string& key, float value, float duration) {
    Operation operation(operations_);
    std::vector<VisualEffectResult> out;
    for (const auto id : Matching(key)) {
        if (closing_)
            break;
        out.push_back({id, services_->SetProgress(requests_.at(id).request->Status().reference, value, duration)});
    }
    return out;
}
std::vector<VisualEffectId> VisualEffectContext::Requests(const std::string& key) const {
    return Matching(key);
}
std::vector<std::string> VisualEffectContext::CaptureEffects(const std::set<std::string>& keys) const {
    std::vector<std::string> out;
    if (closing_)
        return out;
    for (const auto& [id, entry] : requests_) {
        const auto& status = entry.request->Status();
        if (keys.count(entry.key) && !entry.manager && entry.retained && status.submitted && status.submit.returned &&
            !status.cancelled && !status.request_ended && !status.closed)
            out.push_back(entry.key);
    }
    return out;
}
void VisualEffectContext::Remove(VisualEffectId id, api::EffectRemovalOptions options) {
    const auto found = requests_.find(id);
    if (found != requests_.end() && found->second.retained)
        found->second.request->Cancel(options);
}
api::EffectControlResult VisualEffectContext::SetAnimatorTrigger(VisualEffectId id, const std::string& name,
                                                                 bool reset) {
    Operation operation(operations_);
    const auto found = requests_.find(id);
    if (closing_ || found == requests_.end() || !found->second.retained || found->second.request->Status().closed ||
        found->second.request->Status().request_ended)
        return {api::EffectControlState::Ended, {}, true, "visual receipt ended"};
    return services_->SetAnimatorTrigger(found->second.request->Status().reference, name, reset);
}
api::EffectControlResult VisualEffectContext::SetAnimatorParameter(VisualEffectId id,
                                                                   const api::EffectAnimatorParameter& value) {
    Operation operation(operations_);
    const auto found = requests_.find(id);
    if (closing_ || found == requests_.end() || !found->second.retained || found->second.request->Status().closed ||
        found->second.request->Status().request_ended)
        return {api::EffectControlState::Ended, {}, true, "visual receipt ended"};
    return services_->SetAnimatorParameter(found->second.request->Status().reference, value);
}
api::EffectControlResult VisualEffectContext::SetProgress(VisualEffectId id, float value, float duration) {
    Operation operation(operations_);
    const auto found = requests_.find(id);
    if (closing_ || found == requests_.end() || !found->second.retained || found->second.request->Status().closed ||
        found->second.request->Status().request_ended)
        return {api::EffectControlState::Ended, {}, true, "visual receipt ended"};
    return services_->SetProgress(found->second.request->Status().reference, value, duration);
}
api::EffectControlResult VisualEffectContext::SetAnimatorState(VisualEffectId id, int hash, float duration,
                                                               bool restart) {
    Operation operation(operations_);
    const auto found = requests_.find(id);
    if (closing_ || found == requests_.end() || !found->second.retained || found->second.request->Status().closed ||
        found->second.request->Status().request_ended)
        return {api::EffectControlState::Ended, {}, true, "visual receipt ended"};
    return services_->SetAnimatorState(found->second.request->Status().reference, hash, duration, restart);
}
std::vector<VisualEffectResult> VisualEffectContext::SetAnimatorState(const std::string& key, int hash, float duration,
                                                                      bool restart) {
    Operation operation(operations_);
    std::vector<VisualEffectResult> out;
    for (const auto id : Matching(key)) {
        if (closing_)
            break;
        out.push_back(
            {id, services_->SetAnimatorState(requests_.at(id).request->Status().reference, hash, duration, restart)});
    }
    return out;
}
api::EffectControlResult VisualEffectContext::SetAnimatorState(VisualEffectId id, const std::string& name,
                                                               float duration, bool restart) {
    Operation operation(operations_);
    const auto found = requests_.find(id);
    if (closing_ || found == requests_.end() || !found->second.retained || found->second.request->Status().closed ||
        found->second.request->Status().request_ended)
        return {api::EffectControlState::Ended, {}, true, "visual receipt ended"};
    return services_->SetAnimatorState(found->second.request->Status().reference, name, duration, restart);
}
}
