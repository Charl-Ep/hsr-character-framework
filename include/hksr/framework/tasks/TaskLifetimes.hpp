#pragma once
#include <hksr/framework/tasks/TaskCatalog.hpp>
#include <hksr/framework/tasks/TaskPresentation.hpp>

namespace hksr::framework {
// Owns callback executions, not a second task interpreter. Each callback is a
// TaskPresentation borrowing the named visual resource owner.
class TaskLifetimes {
  public:
    using Ports = std::function<TaskPresentationServices(const VisualProgramParameters&)>;
    using ValueWritten = std::function<void(const std::string&, std::uint64_t, const std::string&)>;
    TaskLifetimes(std::uint64_t, std::shared_ptr<const TaskCatalog>, std::shared_ptr<VisualProgramPlayer>, Ports,
                  std::function<TaskExecutionToken()>, std::function<void(std::string)>, ValueWritten = {});
    ~TaskLifetimes();
    std::shared_ptr<const TaskLifetimeReceipt> Submit(const TaskLifetimeCommand&, const VisualProgramParameters&,
                                                      bool interrupts_with_playback = true);
    void PrepareState(const std::string&); // local values only; no callbacks or visual service
    void Service(double, std::uint64_t, double playback_rate = 1);
    void InterruptCallbacks(); // keep admitted owners and their enter/exit transactions
    void Stop(TaskExitReason);
    bool Drained() const;
    bool Idle() const;      // active persistent owners may remain; no callback/drain work
    bool Restoring() const; // explicit saved-owner restore jobs only, never ordinary callbacks
    std::string RestoreError() const;
    std::vector<TaskRequestSnapshot> Requests() const;

  private:
    struct State;
    std::shared_ptr<State> state_;
};
}
