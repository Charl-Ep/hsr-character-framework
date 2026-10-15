#include <hksr/framework/application/ApplicationService.hpp>
#include <stdexcept>

namespace hksr::framework {
std::uint64_t ApplicationService::Post(CharacterCommand command,ReplySink reply) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(closing_)throw std::logic_error("application closing");
    const auto id=next_+1;
    commands_.push_back({id,std::move(command),std::move(reply)});next_=id;return id;
}
std::vector<CommandReply> ApplicationService::TakeReplies() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CommandReply> out;out.swap(replies_);return out;
}
ApplicationSnapshot ApplicationService::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);return snapshot_;
}
ApplicationUpdates ApplicationService::ReadUpdates(std::optional<ApplicationCursor> after) const {
    std::lock_guard<std::mutex> lock(mutex_);return {events_.Read(after),snapshot_};
}
ApplicationUpdates ApplicationService::WaitUpdates(ApplicationCursor after,std::chrono::milliseconds timeout) const {
    std::unique_lock<std::mutex> lock(mutex_);
    changed_.wait_for(lock,timeout,[&]{return !events_.Current(after) || snapshot_.closed;});
    return {events_.Read(after),snapshot_};
}
bool ApplicationService::WaitForChange(const ApplicationCursor& after,std::chrono::milliseconds timeout) const {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock,timeout,[&]{return !events_.Current(after) || snapshot_.closed;});
}
std::deque<ApplicationService::PendingCommand> ApplicationService::TakeCommands(bool closing) {
    std::deque<PendingCommand> out;bool changed=false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closing_=closing_ || closing;
        if(closing_ && !snapshot_.closing) {
            auto next=snapshot_;next.closing=true;
            changed=events_.Publish(snapshot_,next);snapshot_=std::move(next);
        }
        out.swap(commands_);
    }
    if(changed)changed_.notify_all();return out;
}
void ApplicationService::Publish(ApplicationSnapshot snapshot) {
    bool changed=false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closing_=closing_ || snapshot.closing || snapshot.closed;
        snapshot.closing=closing_;
        changed=events_.Publish(snapshot_,snapshot);snapshot_=std::move(snapshot);
    }
    if(changed)changed_.notify_all();
}
void ApplicationService::Deliver(const PendingCommand& pending,CommandReply reply) {
    if(pending.reply)pending.reply(reply);
    else {std::lock_guard<std::mutex> lock(mutex_);replies_.push_back(std::move(reply));}
}
}
