#pragma once
#include "ActionCatalogTypes.hpp"
#include <filesystem>
#include <functional>
#include <mutex>

namespace hksr::framework {
struct ActionSourceIdentity {
    std::string build;
    std::uint32_t design=0;
    // Exact small official version-marker contents, not content hashes or
    // runtime pointers. Keys retain their paths relative to the game data root.
    std::map<std::string,std::string> markers;
    bool operator==(const ActionSourceIdentity& other) const {
        return build==other.build && design==other.design && markers==other.markers;
    }
};
struct ActionSourceData {
    ActionSourceIdentity identity;
    std::map<ActionSourceKey,std::shared_ptr<const api::ActionConfigInput>> documents;
    std::map<std::string,std::shared_ptr<const api::AbilitySources>> abilities;
    std::set<ActionSourceKey> invalid_documents;
    std::set<std::string> invalid_abilities;
};
std::string EncodeActionSources(const ActionSourceData&);
ActionSourceData DecodeActionSources(std::string_view);
bool ReusableActionSource(const api::ActionConfigInput&);
bool ReusableActionSource(const api::AbilitySources&);
std::map<std::string,std::string> ReadActionVersionMarkers(const std::filesystem::path& game_data);

// Worker loads/saves values. The engine thread binds the current identity and
// accesses copied source records. Failure to use the store never blocks reads
// from the official catalog. No discovery, projection or playback lives here.
class ActionSourceStore {
public:
    struct Stats {std::uint64_t document_hits=0,ability_hits=0,document_misses=0,ability_misses=0;};
    ActionSourceStore(std::filesystem::path packaged,std::filesystem::path local,
        std::function<void(const std::string&)> report={});
    void Load(); // worker, before binding
    void Bind(ActionSourceIdentity);
    std::shared_ptr<const api::ActionConfigInput> Document(const ActionSourceKey&);
    std::shared_ptr<const api::AbilitySources> Ability(const std::string&);
    void Put(const api::ActionConfigInput&);
    void Put(const std::string&,const api::AbilitySources&);
    void Erase(const ActionSourceKey&);
    void EraseAbility(const std::string&);
    void Flush(); // engine: publish a completed-batch checkpoint
    void Pump(); // worker: atomic file replacement
    Stats Statistics() const;
private:
    std::filesystem::path packaged_,local_;
    std::function<void(const std::string&)> report_;
    mutable std::mutex mutex_;
    std::vector<ActionSourceData> loaded_;
    ActionSourceData data_;
    bool bound_=false,dirty_=false,flush_=false;
    Stats stats_;
};
}
