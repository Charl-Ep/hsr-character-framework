#pragma once
#include "SceneLayout.hpp"
#include <deque>
#include <mutex>

namespace hksr::framework {
enum class LayoutIoKind {ReadCurrent,WriteCurrent,List,Save,Load,Delete};
struct LayoutIoRequest {
    LayoutIoKind kind=LayoutIoKind::List;
    std::string scene,name;
    std::shared_ptr<const SceneLayout> layout;
    bool overwrite=false;
    std::uint64_t revision=0;
};
struct LayoutIoResult {
    std::uint64_t id=0;
    LayoutIoRequest request;
    std::optional<SceneLayout> layout;
    std::vector<std::string> names;
    std::string error;
};
struct LayoutIoStatus {
    bool sealed=false;
    std::size_t pending=0,active=0;
    std::uint64_t completed=0,failed=0;
    bool Drained() const {return sealed && pending==0 && active==0;}
};
// Value mailbox: Submit/Take are game-frame safe; Pump performs filesystem IO
// on ONE background owner. No game callbacks or managed objects cross this boundary.
class LayoutIo {
public:
    explicit LayoutIo(std::filesystem::path root):storage_(std::move(root)){}
    std::uint64_t Submit(LayoutIoRequest);
    std::vector<LayoutIoResult> Take();
    void Pump();
    // Producer calls after its final capture. Idempotent; accepted work remains
    // writable. Drained means completed OR failed, never necessarily saved.
    void Seal();
    LayoutIoStatus Status() const;
private:
    struct Work {std::uint64_t id;LayoutIoRequest request;};
    LayoutStorage storage_;
    mutable std::mutex mutex_;
    LayoutIoStatus status_;
    std::uint64_t next_=0;
    std::deque<Work> requests_;
    std::vector<LayoutIoResult> results_;
};
}
