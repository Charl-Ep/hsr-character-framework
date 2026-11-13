#pragma once
#include <hksr/framework/catalog/SourceTypes.hpp>
#include <hksr/framework/catalog/ActionSelection.hpp>

namespace hksr::framework {
enum class SourceRequestPhase { Queued, Preparing, Ready, Unresolved, Failed };
struct SourceRequestStatus {
    std::uint64_t id = 0;
    BrowseSource input;
    SourceRequestPhase phase = SourceRequestPhase::Queued;
    SourceSummary result;
    ActionCatalogStatus actions;
    std::string error;
    bool Pending() const {
        return phase == SourceRequestPhase::Queued || phase == SourceRequestPhase::Preparing;
    }
};
struct CatalogSnapshot {
    bool requested = false, attempted = false;
    // Display publication does not change source identities or action data.
    std::uint64_t rows_revision = 0;
    std::string text_language, localization_error;
    std::shared_ptr<const api::CharacterCatalog> rows;
    std::shared_ptr<const api::CharacterSkinCatalog> skins;
    std::map<std::uint64_t, std::shared_ptr<const SourceRequestStatus>> sources;
    const SourceRequestStatus* Source(std::uint64_t id) const {
        const auto found = sources.find(id);
        return found == sources.end() ? nullptr : found->second.get();
    }
    std::map<std::uint64_t, std::shared_ptr<const CharacterActionCatalog>> characters;
    const CharacterActionCatalog* Actions(std::uint64_t id) const {
        const auto found = characters.find(id);
        return found == characters.end() ? nullptr : found->second.get();
    }
    std::string error, table_error;
};
}
