#include "../../support/LegacyScriptFixture.hpp"
#include <hksr/framework/presentation/model/ActorModelScopes.hpp>
#include <hksr/framework/presentation/scene/VisualProgram.hpp>
#include "../../../src/serialization/VisualModelCodec.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <iostream>
#include <map>
#include <set>

using namespace hksr;
using namespace hksr::framework;
namespace {
void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void Reject(F operation) {
    bool failed = false;
    try {
        operation();
    } catch (const std::exception&) {
        failed = true;
    }
    Check(failed, "operation accepted invalid input");
}
struct Fixture {
    bool current = true, face_fault = false, face_partial_fault = false, face_read_fault = false,
         face_partial_read_fault = false, unsupported = false;
    unsigned writes = 0, closed = 0;
    api::FaceMaterialValue face{.1f, .2f, .3f};
    float duration = 0;
    std::map<std::string, float> factors{{"official", .5f}};
    api::EntityOrigin target;
    std::set<std::uint64_t> live{1, 2, 3};
    std::unique_ptr<api::ActorModelPresentation> Create() {
        api::ActorModelPresentationServices s;
        s.current = [&] { return current; };
        s.face = [&] {
            if (face_read_fault)
                throw std::runtime_error("face read failed");
            return face;
        };
        s.set_face = [&](auto value) {
            if (unsupported)
                throw api::ModelPresentationUnsupported("unit unavailable");
            if (face_partial_fault) {
                face.cheek = value.value.cheek;
                ++writes;
                if (face_partial_read_fault)
                    face_read_fault = true;
                throw std::runtime_error("partial scalar face write");
            }
            face = value.value;
            duration = value.duration;
            ++writes;
            if (face_fault)
                throw std::runtime_error("partial face write");
        };
        s.set_scale_factor = [&](const auto& key, float value) {
            if (unsupported)
                throw api::ModelPresentationUnsupported("unit unavailable");
            if (value == 1)
                factors.erase(key);
            else
                factors[key] = value;
            ++writes;
        };
        s.root_scale = [&] {
            float value = 1;
            for (const auto& [key, factor] : factors) {
                (void)key;
                value *= factor;
            }
            return value;
        };
        s.follow_target = [&] { return target; };
        s.target_current = [&](auto value) { return live.count(value.token.generation) != 0; };
        s.set_follow = [&](auto value) {
            if (unsupported)
                throw api::ModelPresentationUnsupported("unit unavailable");
            target = value;
            ++writes;
        };
        s.close = [&] {
            ++closed;
            return true;
        };
        s.model_identity = 10;
        return std::make_unique<api::ActorModelPresentation>(std::move(s));
    }
    float Scale() const {
        float value = 1;
        for (const auto& [key, factor] : factors) {
            (void)key;
            value *= factor;
        }
        return value;
    }
};
api::EntityOrigin Target(std::uint64_t generation) {
    return {{reinterpret_cast<api::GameEntity*>(generation * 16), generation}, reinterpret_cast<api::GameWorld*>(1)};
}
void Contributions() {
    Fixture f, other;
    ActorModelScopes scopes(f.Create());
    const auto initial = f.face;
    scopes.Face(1, {{.3f, .4f, .5f}, .2f});
    scopes.Face(2, {{.7f, .8f, .9f}, .3f});
    Check(scopes.Release(1) && f.face.cheek == .7f, "older face scope displaced newer");
    Check(scopes.Release(2) && f.face == initial && f.duration == 0, "face target baseline not restored");
    Check(other.face == initial && other.writes == 0, "another actor changed");
    scopes.Scale(1, .6f);
    scopes.Scale(2, 1.3f);
    Check(std::abs(f.Scale() - .65f) < .0001f, "model scale did not preserve independent native factor");
    Check(scopes.Release(1) && std::abs(f.Scale() - .65f) < .0001f, "older scale withdrawal displaced newer");
    Check(scopes.Release(2) && f.factors.size() == 1 && f.Scale() == .5f, "scale cleanup removed another factor");
    scopes.Follow(1, Target(2));
    scopes.Follow(2, Target(3));
    Check(scopes.Release(1) && f.target.token.generation == 3, "older follow displaced newer");
    Check(scopes.Release(2) && !f.target.token.entity, "follow did not restore empty baseline");
    scopes.Follow(3, Target(2));
    f.live.erase(2);
    scopes.Service();
    Check(!f.target.token.entity, "retired target was rebound");
    Check(scopes.Release(3) && scopes.Close() && f.closed == 1, "model scopes did not close");
}
void ForeignAndFailures() {
    Fixture f;
    ActorModelScopes scopes(f.Create());
    const auto baseline = f.face;
    scopes.Face(1, {{.6f, 0, 0}, .1f});
    f.face = {.9f, .2f, .1f};
    Check(scopes.Release(1) && f.face.cheek == .9f, "withdrawal overwrote independent official face write");
    f.face = baseline;
    f.face_fault = true;
    Reject([&] { scopes.Face(2, {{.7f, 0, 0}, .2f}); });
    f.face_fault = false;
    Check(scopes.Release(2) && f.face == baseline, "uncertain face write lost restoration receipt");
    f.face_partial_fault = true;
    Reject([&] { scopes.Face(21, {{.7f, .8f, .9f}, .2f}); });
    Check(f.face.cheek == .7f && f.face.shy == baseline.shy && f.face.shadow == baseline.shadow,
          "partial fixture unexpectedly wrote every target");
    f.face_partial_fault = false;
    Check(scopes.Release(21) && f.face == baseline, "partial scalar write was mistaken for an independent writer");
    f.face_partial_fault = true;
    f.face_partial_read_fault = true;
    Reject([&] { scopes.Face(22, {{.9f, .8f, .7f}, .2f}); });
    f.face_partial_fault = false;
    f.face_partial_read_fault = false;
    Check(!scopes.Release(22), "failed post-write read released cleanup responsibility");
    f.face_read_fault = false;
    Check(scopes.Release(22) && f.face == baseline, "retry after unreadable partial write did not restore target");
    f.unsupported = true;
    Reject([&] { scopes.Scale(4, .8f); });
    Reject([&] { scopes.Follow(5, Target(3)); });
    Reject([&] { scopes.Face(6, {{.5f, 0, 0}, 0}); });
    f.unsupported = false;
    const auto calls = f.writes;
    Check(scopes.Close() && f.writes == calls, "unsupported unit fabricated an active contribution");
    Fixture retired;
    ActorModelScopes old(retired.Create());
    old.Face(7, {{.8f, 0, 0}, 0});
    old.Scale(7, .7f);
    retired.current = false;
    retired.face = {.2f, .3f, .4f};
    const auto before = retired.writes;
    Check(old.Close() && retired.writes == before && retired.face.cheek == .2f,
          "old-model baseline was written to replacement");
}
void Codec() {
    using Json = nlohmann::json;
    const auto changed = visual_model_codec::Parse(
        Json{{"op", "model.change"}, {"source", "fixture"}, {"key", "form"}, {"path", nullptr}});
    Check(changed && !std::get<VisualModelChange>(changed->command).path,
          "null model path lost official default restoration");
    const auto configuration = visual_model_codec::Parse(Json{{"op", "model.configuration"},
                                                              {"source", "fixture"},
                                                              {"radius", 1.4},
                                                              {"somato", 5},
                                                              {"graph_emotion", nullptr},
                                                              {"event_files", Json::array({"one.json", "two.json"})}});
    const auto& configured = std::get<VisualModelConfiguration>(configuration->command).input;
    Check(configured.radius == 1.4f && configured.somato == 5 && configured.graph_emotion == std::string{} &&
              configured.event_files->size() == 2,
          "visual configuration codec lost defaults or event input");
    const auto enable = visual_model_codec::Parse(Json{
        {"op", "expression.enable"}, {"source", "fixture"}, {"enabled", false}, {"transit_out", .1}, {"transit_in", .2}});
    Check(!std::get<VisualEmotionEnable>(enable->command).enabled, "emotion enable codec lost explicit off");
    Reject([&] { visual_model_codec::Parse(Json{{"op", "model.configuration"}, {"reset", true}, {"radius", 1}}); });
    const auto restored_events = visual_model_codec::Parse(
        Json{{"op", "model.configuration"}, {"reset", true}, {"event_files", {"original.json"}}});
    const auto& restore = std::get<VisualModelConfiguration>(restored_events->command).input;
    Check(restore.reset && restore.event_files && restore.event_files->at(0) == "original.json",
          "explicit event defaults survive reset codec");
    Reject([&] { visual_model_codec::Parse(Json{{"op", "model.configuration"}, {"event_files", nullptr}}); });
    auto face =
        Json::parse(R"({"op":"expression.material","source":"fixture","cheek":0.3,"shy":0.2,"shadow":0.1,"duration":0.2})");
    const auto decoded = visual_model_codec::Parse(face);
    Check(decoded && std::get<VisualFaceMaterial>(decoded->command).transition.value.cheek == .3f,
          "face codec differs");
    Check(std::get<VisualModelScale>(
              visual_model_codec::Parse(Json{{"op", "model.scale"}, {"source", "fixture"}, {"scale", .6}})->command)
                  .scale == .6f,
          "scale codec differs");
    Check(std::get<VisualModelScaleFollow>(
              visual_model_codec::Parse(Json{{"op", "model.follow_scale"}, {"source", "fixture"}, {"target", nullptr}})
                  ->command)
              .target.empty(),
          "follow stop codec differs");
    const auto batch = Json::parse(
        R"({"op":"model.character_scale","source":"fixture","targets":["owner"],"scale":0.8,"outline":0.3,"multiplier":[[2,0.6],[1,0.8]],"equal_ratio_by_min":true,"save_targets":"scaled"})");
    const auto parsed = std::get<VisualCharacterScale>(visual_model_codec::Parse(batch)->command);
    Check(parsed.targets.size() == 1 && parsed.input.base_scale == .8f && parsed.input.multiplier.front().height == 2 &&
              parsed.save_targets == "scaled",
          "character scale recipe/ordered multiplier differs");
    auto badbatch = batch;
    badbatch.erase("scale");
    Reject([&] { visual_model_codec::Parse(badbatch); });
    badbatch = batch;
    badbatch["read_targets"] = "old";
    Reject([&] { visual_model_codec::Parse(badbatch); });
    badbatch = batch;
    badbatch["multiplier"] = Json::array({Json::array({1, 2}), Json::array({1, 3})});
    Reject([&] { visual_model_codec::Parse(badbatch); });
    auto invalid = face;
    invalid["duration"] = -1;
    Reject([&] { visual_model_codec::Parse(invalid); });
    invalid = face;
    invalid["cheek"] = "0.2";
    Reject([&] { visual_model_codec::Parse(invalid); });
    invalid = face;
    invalid["extra"] = true;
    Reject([&] { visual_model_codec::Parse(invalid); });
    const auto program = ParseLegacyVisualProgram(
        Json{{"format", "hksr.visual-program"},
             {"id", "model"},
             {"source", "fixture"},
             {"scope", "owned"},
             {"effects", Json::object()},
             {"actions", {{"show", {{"state", "StandBy"}, {"restart", true}, {"enter", Json::array({face})}}}}}}
            .dump());
    Check(std::holds_alternative<VisualModelOperation>(program.actions.at("show").enter.front().operation),
          "shared visual codec did not dispatch model unit");
}
}
int main() try {
    Contributions();
    ForeignAndFailures();
    Codec();
    std::cout << "Model scopes: overlaps, target retirement, independent native writers, partial cleanup and typed "
                 "codecs passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
