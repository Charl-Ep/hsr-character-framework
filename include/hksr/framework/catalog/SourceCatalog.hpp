#pragma once
#include "SourceTypes.hpp"
#include "../assets/CharacterPackage.hpp"
#include <hksr/model/ModelProfileInputs.hpp>
#include <hksr/animation/OfficialCatalog.hpp>
#include <map>
#include <optional>
#include <functional>
#include <variant>

namespace hksr::framework {
// Plain copied data. Safe to publish as a snapshot; only the world-thread
// coordinator updates identities. No managed objects or whole config JSON.
class SourceCatalog {
public:
    explicit SourceCatalog(api::CharacterCatalog rows):rows_(std::move(rows)){}
    const api::CharacterCatalog& Rows() const { return rows_; }
    void SetIdentity(std::string path,api::TextInput identity);
    void ForgetUnavailableIdentities();
    FoundationChoice Foundation(const api::PlayerCatalogRow& player,
        std::optional<std::uint32_t> explicit_npc={}) const;
    BattleSourceChoice Battle(std::uint32_t avatar) const;
    // An external package can reuse official action sources only through its
    // exact declared config/prefab pair. IDs and filenames aren't substituted.
    std::vector<api::PlayerCatalogRow> PackageSources(const CharacterPackage& package) const;
    static CharacterPackage NpcPackage(const api::NpcCatalogRow& npc);
    static CharacterPackage PlayerPackage(const api::PlayerCatalogRow& player,const api::NpcCatalogRow& npc);
    static CharacterPackage PlayerPackage(const api::PlayerCatalogRow& player);
    static BattlePresentation Presentation(const api::AvatarCatalogRow& avatar,const api::PresentationConfigInput& config);
private:
    api::CharacterCatalog rows_;
    std::map<std::string,api::TextInput> identities_;
};
struct CatalogQueries {
    std::function<api::TextInput(const std::string&)> identity;
    std::function<api::PresentationConfigInput(const std::string&)> presentation;
};
// One domain-local value cache shared by preparation requests. Configs are
// loaded on demand once per distinct path; no player object or prefab loading.
// Replace this cache when the metadata/content domain changes, not on map travel.
class SourcePreparation {
public:
    SourcePreparation(api::CharacterCatalog rows,api::OfficialCatalog& api);
    SourcePreparation(api::CharacterCatalog rows,CatalogQueries queries);
    void Player(PlayerKey key,std::optional<std::uint32_t> npc={});
    void Package(CharacterPackage package);
    // At most one configuration read, allowing the host to interleave jobs at
    // its world-thread scheduling boundary. This is not a source-count cap;
    // every requested identity remains pending until read or explicitly failed.
    void Advance();
    void Retry();
    void InvalidateFailures(const std::vector<std::string>& presentations);
    const PreparedSource& Status() const { return result_; }
    const SourceCatalog& Catalog() const { return catalog_; }
private:
    void SelectBattle(std::uint32_t avatar);
    SourceCatalog catalog_;
    CatalogQueries queries_;
    std::optional<api::PlayerCatalogRow> player_;
    std::optional<std::uint32_t> explicit_npc_;
    std::optional<CharacterPackage> package_;
    std::map<std::string,api::PresentationConfigInput> presentations_;
    PreparedSource result_;
};
}
