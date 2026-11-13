#pragma once
#include <hksr/framework/tasks/TaskCatalog.hpp>
#include <hksr/framework/tasks/TaskEvents.hpp>

namespace hksr::framework {
// A receiver's logical presentation state. No native battle actor is registered
// here. Epochs also change on domain reset without changing the character ID.
class TaskContextEndpoint {
  public:
    explicit TaskContextEndpoint(std::shared_ptr<const TaskCatalog>);
    TaskContext& Context() {
        return context_;
    }
    const TaskContext& Context() const {
        return context_;
    }
    std::uint64_t Epoch() const {
        return epoch_;
    }
    bool Active() const {
        return active_;
    }
    void Reset(bool close = false);
    TaskContextToken Apply(const std::string&, const TaskContextDefinition&, const std::string& source_catalog = {});
    void Remove(std::uint64_t epoch, TaskContextToken);
    void Subscribe(const std::shared_ptr<TaskEventSubscription>&);
    std::shared_ptr<const TaskEventReceipt> Dispatch(const TaskEventInput&);

  private:
    std::shared_ptr<const TaskCatalog> catalog_;
    TaskContext context_;
    std::uint64_t epoch_ = 1;
    bool active_ = true;
    std::vector<std::weak_ptr<TaskEventSubscription>> events_;
};
// Engine-thread registry owned by SceneCharacters, not a game-world search.
class TaskContextRegistry {
  public:
    void Register(std::uint64_t, const std::shared_ptr<TaskContextEndpoint>&);
    std::shared_ptr<TaskContextEndpoint> Find(std::uint64_t) const;
    std::shared_ptr<const TaskEventReceipt> Dispatch(TaskEventKind, std::uint64_t recipient,
                                                     std::uint64_t source) const;

  private:
    std::map<std::uint64_t, std::weak_ptr<TaskContextEndpoint>> receivers_;
};
}
