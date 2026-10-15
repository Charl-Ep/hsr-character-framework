#pragma once
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>
#include <string>

namespace hksr::framework {
struct ApplicationSnapshot;
struct ApplicationCursor {
    std::string stream;
    std::uint64_t sequence=0;
};
// Resource invalidations, not animation frames or a durable event log. Read
// changed values from the snapshot returned atomically with the event cursor.
// Missing IDs in that snapshot mean removal/expiry, not failed execution.
struct ApplicationChange {
    std::uint64_t sequence=0;
    bool runtime=false,catalog=false,inspection=false;
    std::vector<std::uint64_t> characters,operations;
    bool Empty() const {return !runtime && !catalog && !inspection && characters.empty() && operations.empty();}
};
struct ApplicationChangeLog {
    ApplicationCursor cursor;
    bool reset=false;
    std::vector<ApplicationChange> events;
};
// Protected by ApplicationService's snapshot mutex. Bounded replay affects
// only synchronization; never limits actors, commands or pending operations.
class ApplicationEvents {
public:
    explicit ApplicationEvents(std::size_t retained=256);
    bool Publish(const ApplicationSnapshot& previous,const ApplicationSnapshot& next);
    ApplicationChangeLog Read(const std::optional<ApplicationCursor>& after) const;
    bool Current(const ApplicationCursor& cursor) const {return cursor.stream==stream_ && cursor.sequence==sequence_;}
private:
    std::string stream_;
    std::uint64_t sequence_=0;
    std::size_t retained_;
    std::deque<ApplicationChange> events_;
};
}
