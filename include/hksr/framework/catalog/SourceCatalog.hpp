#pragma once
#include <hksr/framework/catalog/SourceTypes.hpp>
#include <hksr/framework/assets/CharacterPackage.hpp>
#include <hksr/model/ModelProfileInputs.hpp>
#include <hksr/animation/OfficialCatalog.hpp>
#include <map>
#include <optional>
#include <functional>
#include <variant>

namespace hksr::framework {
// Copied official rows and resource selections. No managed objects or config JSON.
class SourceCatalog {
  public:
    explicit SourceCatalog(api::CharacterCatalog rows) : rows_(std::move(rows)) {}
    const api::CharacterCatalog& Rows() const {
        return rows_;
    }
    BattleSourceChoice Battle(std::uint32_t avatar) const;
    // An external package can reuse official action sources only through its
    // exact declared config/prefab pair. IDs and filenames aren't substituted.
    std::vector<api::PlayerCatalogRow> PackageSources(const CharacterPackage& package) const;
    static CharacterPackage NpcPackage(const api::NpcCatalogRow& npc);
    static CharacterPackage PlayerPackage(const api::PlayerCatalogRow& player);
    static CharacterPackage SkinPackage(const api::CharacterSkinStatus&);
    static BattlePresentation SkinPresentation(BattlePresentation, const api::CharacterSkinStatus&);
    static BattlePresentation Presentation(const api::AvatarCatalogRow& avatar,
                                           const api::PresentationConfigInput& config);

  private:
    api::CharacterCatalog rows_;
};
struct CatalogQueries {
    std::function<api::PresentationConfigInput(const std::string&)> presentation;
};
// One domain-local value cache shared by preparation requests. Configs are
// loaded on demand once per distinct path; no player object or prefab loading.
// Replace this cache when the metadata/content domain changes, not on map travel.
class SourcePreparation {
  public:
    SourcePreparation(api::CharacterCatalog rows, api::OfficialCatalog& api);
    SourcePreparation(api::CharacterCatalog rows, CatalogQueries queries);
    void Player(PlayerKey key);
    void Skin(std::shared_ptr<api::CharacterSkin> skin);
    void Package(CharacterPackage package);
    // At most one presentation read per call, on the official thread.
    void Advance();
    void InvalidateFailures(const std::vector<std::string>& presentations);
    const PreparedSource& Status() const {
        return result_;
    }
    PreparedSource Take(); // move the result and clear the current selection
    const SourceCatalog& Catalog() const {
        return catalog_;
    }

  private:
    void SelectBattle(std::uint32_t avatar);
    SourceCatalog catalog_;
    CatalogQueries queries_;
    std::optional<api::PlayerCatalogRow> player_;
    std::optional<CharacterPackage> package_;
    std::map<std::string, api::PresentationConfigInput> presentations_;
    PreparedSource result_;
};
}
