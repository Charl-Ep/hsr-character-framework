#pragma once
#include <hksr/framework/catalog/SourceTypes.hpp>
#include <hksr/framework/catalog/ActionCatalogTypes.hpp>
#include <hksr/framework/assets/ModelPayload.hpp>
#include <hksr/framework/appearance/AppearanceTypes.hpp>
#include <hksr/framework/presentation/scene/PresentationProfile.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hksr::framework {
struct VisualProgramInput;
enum class CharacterPersistence { Layout, Session };
// Source selection and retained preparation resources for an actor instance.
// A missing battle source does not invalidate exploration.
// Payload IO happens before this object reaches the official world thread.
struct CharacterPlan {
    CharacterPackage package;
    std::shared_ptr<api::CharacterSkin> skin;
    std::shared_ptr<const ModelAssetInput> payload;
    std::string npc_look_source;
    std::optional<BattlePresentation> alternate;
    VisualRoot visual_root = VisualRoot::UniqueAnimator;
    std::vector<ActionSourceRoot> action_sources;
    // Original user selection, retained independently of a preparation request.
    // Layouts persist this identity, never reverse-parse display/package names.
    std::optional<BrowseSource> source_intent;
    std::shared_ptr<const PresentationProfile> presentation_profile;
    std::shared_ptr<const VisualProgramInput> visual_program;
    CharacterPersistence persistence = CharacterPersistence::Layout;
    static CharacterPlan FromPrepared(const PreparedSource& source, std::shared_ptr<const ModelAssetInput> payload = {},
                                      std::string npc_look_source = {});
};
}
