#include <hksr/framework/catalog/SourceCatalog.hpp>
#include <algorithm>
#include <stdexcept>
namespace hksr::framework {
BattleSourceChoice SourceCatalog::Battle(std::uint32_t avatar) const {
    BattleSourceChoice out;
    for (const auto& row : rows_.avatars.rows)
        if (row.avatar == avatar) {
            const auto same = std::find_if(out.candidates.begin(), out.candidates.end(), [&](const auto& p) {
                return p.character_config == row.character_config && p.prefab == row.prefab;
            });
            if (same == out.candidates.end())
                out.candidates.push_back(row);
        }
    if (out.candidates.size() > 1)
        out.status = SourceResolution::Ambiguous;
    else if (!out.candidates.empty() && !out.candidates[0].character_config.empty() &&
             !out.candidates[0].prefab.empty())
        out.status = SourceResolution::Ready;
    else if (!rows_.avatars.enumerated || !rows_.avatars.issues.empty())
        out.status = SourceResolution::Incomplete;
    return out;
}
std::vector<api::PlayerCatalogRow> SourceCatalog::PackageSources(const CharacterPackage& package) const {
    std::vector<api::PlayerCatalogRow> out;
    if (package.appearance)
        for (const auto& row : rows_.players.rows)
            if (row.character_config == package.appearance->character_config &&
                row.prefab == package.appearance->template_prefab)
                out.push_back(row);
    return out;
}
CharacterPackage SourceCatalog::NpcPackage(const api::NpcCatalogRow& npc) {
    CharacterPackage out;
    out.id = "official-npc:" + std::to_string(npc.id);
    out.display_name = out.id + " | " + npc.character_config;
    out.npc_id = npc.id;
    return out;
}
CharacterPackage SourceCatalog::PlayerPackage(const api::PlayerCatalogRow& player) {
    CharacterPackage out;
    out.id = "official-player:" + std::to_string(player.id) + ":" + std::to_string(player.enhanced) + ":" +
             std::to_string(player.skin);
    out.display_name = out.id + " | " + player.character_config;
    out.avatar_id = player.avatar;
    out.construction = ConstructionKind::StoryActor;
    out.appearance = Appearance{player.prefab, {}, player.character_config, player.character_config, ShapeData::None};
    return out;
}
CharacterPackage SourceCatalog::SkinPackage(const api::CharacterSkinStatus& skin) {
    CharacterPackage package;
    package.id = "official-skin:" + std::to_string(skin.skin_id);
    package.display_name = package.id;
    package.avatar_id = skin.avatar_id;
    package.construction = ConstructionKind::StoryActor;
    package.appearance =
        Appearance{skin.adventure_model, {}, skin.adventure_config, skin.adventure_config, ShapeData::None};
    return package;
}
BattlePresentation SourceCatalog::SkinPresentation(BattlePresentation result, const api::CharacterSkinStatus& skin) {
    if (result.model.character_config != skin.battle_config)
        throw std::invalid_argument("skin battle configuration differs from its selected Avatar source");
    result.model.model_path = skin.battle_model;
    result.model.event_files = skin.battle_event_files;
    return result;
}
BattlePresentation SourceCatalog::Presentation(const api::AvatarCatalogRow& avatar,
                                               const api::PresentationConfigInput& config) {
    if (config.path != avatar.character_config || avatar.prefab.empty())
        throw std::invalid_argument("presentation input does not match the selected official source");
    if (!config.event_files.available || !config.disabled_event_layers.available)
        throw std::invalid_argument("official presentation event inputs were not read");
    return {{avatar.prefab, config.event_files.values, config.disabled_event_layers.values, config.path},
            config.initial_state};
}
SourcePreparation::SourcePreparation(api::CharacterCatalog rows, api::OfficialCatalog& api)
    : SourcePreparation(std::move(rows),
                        CatalogQueries{[&api](const auto& path) { return api.ReadPresentationConfig(path); }}) {}
SourcePreparation::SourcePreparation(api::CharacterCatalog rows, CatalogQueries queries)
    : catalog_(std::move(rows)), queries_(std::move(queries)) {}
void SourcePreparation::Player(PlayerKey key) {
    result_ = {};
    player_.reset();
    package_.reset();
    unsigned matches = 0;
    for (const auto& row : catalog_.Rows().players.rows)
        if (key.Matches(row)) {
            player_ = row;
            ++matches;
        }
    if (matches != 1) {
        player_.reset();
        result_.summary.finished = true;
        result_.summary.errors.push_back(matches ? "selected Player tuple is ambiguous"
                                                : "selected Player tuple is absent");
    }
}
void SourcePreparation::Package(CharacterPackage package) {
    result_ = {};
    player_.reset();
    package_ = std::move(package);
    result_.summary.character = package_;
}
void SourcePreparation::Skin(std::shared_ptr<api::CharacterSkin> skin) {
    result_ = {};
    player_.reset();
    package_.reset();
    if (!skin || !skin->Status().prepared || skin->Status().closed)
        throw std::invalid_argument("selected skin preparation failed");
    const auto& status = skin->Status();
    result_.skin = std::move(skin);
    result_.summary.character = SourceCatalog::SkinPackage(status);
    result_.summary.errors = status.errors;
}
void SourcePreparation::SelectBattle(std::uint32_t avatar) {
    result_.summary.battle = catalog_.Battle(avatar);
    if (result_.summary.battle.status != SourceResolution::Ready) {
        result_.summary.finished = true;
        return;
    }
    const auto& source = result_.summary.battle.candidates.front();
    auto found = presentations_.find(source.character_config);
    if (found == presentations_.end()) {
        api::PresentationConfigInput input;
        input.path = source.character_config;
        try {
            input = queries_.presentation(source.character_config);
        } catch (const std::exception& error) {
            input.event_files.error = input.disabled_event_layers.error = error.what();
        }
        found = presentations_.emplace(source.character_config, std::move(input)).first;
    }
    try {
        auto presentation = SourceCatalog::Presentation(source, found->second);
        if (result_.skin)
            presentation = SourceCatalog::SkinPresentation(std::move(presentation), result_.skin->Status());
        result_.summary.presentation = std::move(presentation);
    } catch (const std::exception& error) {
        result_.summary.errors.push_back(error.what());
    }
    result_.summary.finished = true; // exploration source remains usable on battle errors
}
void SourcePreparation::Advance() {
    if (result_.summary.finished)
        return;
    if (result_.skin) {
        SelectBattle(result_.skin->Status().avatar_id);
        return;
    }
    if (package_) {
        auto players = catalog_.PackageSources(*package_);
        if (players.empty()) {
            result_.summary.finished = true;
            return;
        } // official NPC or custom package without a matching Player source
        const auto avatar = players.front().avatar;
        if (std::any_of(players.begin(), players.end(), [&](const auto& p) { return p.avatar != avatar; })) {
            result_.summary.errors.push_back("package resource pair has multiple Avatar identities");
            result_.summary.finished = true;
            return;
        }
        SelectBattle(avatar);
        return;
    }
    if (!player_) {
        result_.summary.finished = true;
        return;
    }
    result_.summary.character = SourceCatalog::PlayerPackage(*player_);
    SelectBattle(player_->avatar);
}
void SourcePreparation::InvalidateFailures(const std::vector<std::string>& presentations) {
    for (const auto& path : presentations)
        presentations_.erase(path);
}
PreparedSource SourcePreparation::Take() {
    auto result = std::move(result_);
    result_ = {};
    player_.reset();
    package_.reset();
    return result;
}
}
