#include "../../support/LegacyScriptFixture.hpp"
#include <hksr/framework/presentation/projectiles/VisualProjectiles.hpp>
#include <hksr/framework/presentation/scene/VisualProgramPlayer.hpp>
#include <hksr/framework/tasks/TaskPresentation.hpp>
#include <hksr/framework/tasks/TaskCatalog.hpp>
#include <hksr/framework/presentation/effects/SpatialHitEffects.hpp>
#include <hksr/model/ActorModelChange.hpp>
#include "../../../src/presentation/scene/VisualSceneTransients.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
using namespace hksr;
using namespace hksr::framework;
void CheckAt(bool value, int line) {
    if (!value)
        throw std::runtime_error("projectile presentation line " + std::to_string(line));
}
#define Check(...) CheckAt((__VA_ARGS__), __LINE__)
struct Fixture {
    api::EntityOrigin owner{{reinterpret_cast<api::GameEntity*>(1), 10}, reinterpret_cast<api::GameWorld*>(2)};
    api::EntityOrigin target{{reinterpret_cast<api::GameEntity*>(3), 20}, owner.world};
    api::ProjectileGroupStatus native;
    int creates = 0, reads = 0, fires = 0, updates = 0, cancels = 0, closes = 0;
    bool delayed = false, fail_fire = false;
    float advanced = 0;
    std::vector<api::ProjectileLaunch> launches;
    ProjectileBatchServices Services() {
        return {[this] {
                    ++creates;
                    ProjectileGroupEndpoint g;
                    g.prepare = [this] {
                        native.prepared = true;
                        return true;
                    };
                    g.fire = [this](const auto& launch) -> api::ProjectileId {
                        ++fires;
                        launches.push_back(launch);
                        if (fail_fire) {
                            native.errors.push_back("partial native failure");
                            return 0;
                        }
                        ++native.submitted;
                        ++native.active;
                        return native.submitted;
                    };
                    g.service = [this](float dt) {
                        ++updates;
                        advanced += dt;
                        if (native.cancelled && !delayed) {
                            native.closed = true;
                            native.active = 0;
                        }
                    };
                    g.finish = [this] { native.finishing = true; };
                    g.cancel = [this] {
                        ++cancels;
                        native.cancelled = true;
                    };
                    g.status = [this] { return native; };
                    return g;
                },
                [this](const std::string& key) {
                    Check(key == "template");
                    ++reads;
                    return ProjectileTemplateEndpoint{[] { return reinterpret_cast<void*>(4); },
                                                      [this] {
                                                          ++closes;
                                                          return true;
                                                      }};
                },
                [this](const std::string& key) {
                    Check(key == "Target");
                    api::ProjectileLaunch result;
                    result.target = target;
                    result.target_current = [] { return true; };
                    return result;
                }};
    }
    api::WorldFrame Frame(std::uint64_t sequence, float dt = 0.1f) {
        api::WorldFrame f;
        f.world = owner.world;
        f.sequence = sequence;
        f.delta_time = dt;
        return f;
    }
    VisualProjectilePlan Plan() {
        return {{{0, "template", "Target", {{"FlyTime", 0.2f}}, true}, {0.2f, "template", "Target", {}, false}}};
    }
};
void Batches() {
    {
        Fixture f;
        VisualProjectileBatch b(f.owner, f.Plan(), f.Services());
        b.Service(f.Frame(1));
        Check(f.fires == 1 && f.reads == 1 && b.Status().started && !b.Status().emitted);
        b.Service(f.Frame(1));
        b.Service(f.Frame(1));
        Check(f.fires == 1 && f.updates == 0);
        b.Service(f.Frame(2));
        b.Service(f.Frame(3));
        Check(f.fires == 2 && f.reads == 1 && b.Status().emitted && f.native.finishing);
        Check(f.advanced > 0.19f && f.advanced < 0.21f && f.launches[0].values.at("FlyTime") == 0.2f &&
              !f.launches[1].ordered);
        f.native.arrived = 2;
        b.Service(f.Frame(4));
        Check(b.Status().arrived && !b.Status().closed);
        f.native.active = 0;
        f.native.closed = true;
        b.Service(f.Frame(5));
        Check(b.Status().closed && f.closes == 1 && f.cancels == 0);
    }
    {
        Fixture f;
        VisualProjectileBatch b(f.owner, f.Plan(), f.Services());
        b.Cancel();
        b.Service(f.Frame(1));
        Check(b.Status().closed && f.creates == 0);
    }
    {
        Fixture f;
        VisualProjectileBatch b(f.owner, f.Plan(), f.Services());
        b.Service(f.Frame(1));
        f.delayed = true;
        auto stale = f.target;
        ++stale.token.generation;
        Check(b.ReleaseParticipant(stale) && !b.Status().cancelled);
        Check(!b.ReleaseParticipant(f.target) && b.Status().cancelled && f.closes == 0);
        f.delayed = false;
        b.Service(f.Frame(1));
        Check(b.Status().closed && f.fires == 1 && f.closes == 1);
    }
    {
        Fixture f;
        VisualProjectileBatch b(f.owner, f.Plan(), f.Services());
        f.fail_fire = true;
        b.Service(f.Frame(1));
        Check(b.Status().cancelled && b.Status().error == "partial native failure");
        b.Service(f.Frame(1));
        Check(b.Status().closed && f.fires == 1 && f.closes == 1);
    }
    {
        Fixture f;
        VisualProjectileBatch b(f.owner, f.Plan(), f.Services());
        b.Service(f.Frame(1));
        auto frame = f.Frame(2);
        frame.world = nullptr;
        b.Service(frame);
        Check(b.Status().closed && b.Status().cancelled && f.fires == 1);
    }
    {
        Fixture f;
        auto plan = f.Plan();
        plan.shots[1].at = -1;
        bool bad = false;
        try {
            VisualProjectileBatch b(f.owner, plan, f.Services());
        } catch (const std::invalid_argument&) {
            bad = true;
        }
        Check(bad && f.creates == 0);
    }
}
void Tasks() {
    auto config = nlohmann::json::parse(R"({
      "format":"hksr.task-catalog","id":"projectile-test","domain":"battle",
      "visual":{"id":"scene","source":"fixture","scope":"owned","effects":{},"projectiles":{
        "template":{"root":{"kind":"ability","name":"Sample_Ability"},"path":[
          {"owner":"RPG.GameCore.AbilityConfig","field":"OnStart","index":0},
          {"owner":"RPG.GameCore.FireProjectile","field":"Projectile"}]}}},
      "sequences":{"fire":{"entry":{"op":"serial","children":[
        {"op":"projectiles","request":"shots","shots":[
          {"parameters":"template","target":"Target","values":{"FlyTime":0.2}},
          {"at":0.2,"parameters":"template","target":"Target","ordered":false}]},
        {"op":"wait_request","request":"shots","signal":"performance"}]}}},
      "tasks":{"fire":{"entry":"fire"}}})");
    auto catalog = ParseLegacyTaskCatalog(config.dump());
    const auto& source = catalog.visual->projectiles.at("template");
    Check(source.root == api::DefinitionRoot::Ability && source.path.size() == 2 && source.path[0].index == 0);
    const auto plan = std::get<VisualProjectilePlan>(
        std::get<TaskSubmit>(catalog.sequences.at("fire")->nodes[0].operation).operation);
    Check(plan.shots.size() == 2 && !plan.shots[1].ordered);
    auto bad = config;
    bad["sequences"]["fire"]["entry"]["children"][0]["shots"][0]["parameters"] = "unknown";
    bool rejected = false;
    try {
        ParseLegacyTaskCatalog(bad.dump());
    } catch (const std::exception&) {
        rejected = true;
    }
    Check(rejected);
    Fixture f;
    auto batch = std::make_shared<VisualProjectileBatch>(f.owner, plan, f.Services());
    auto frame = f.Frame(1);
    VisualProgramServices services;
    services.effects =
        std::make_shared<api::PresentationEffects>(api::EffectRequestServices{{}, api::EffectAccess({}), {}, {}, {}});
    services.create = [](auto&, const auto&, const auto&) -> VisualEffectId {
        throw std::runtime_error("unexpected explicit effect");
    };
    int endpoint_requests = 0;
    services.projectiles = [&](auto, const auto&) -> ProjectileRequestEndpoint {
        ++endpoint_requests;
        return {10, [&] { batch->Service(frame); }, [&] { batch->Cancel(); }, [&] { return batch->Status(); }};
    };
    auto player = std::make_shared<VisualProgramPlayer>(catalog.visual, std::move(services));
    TaskPresentation task({1}, 10, catalog.sequences.at("fire"), player, {}, {});
    task.Service(0.1, 1);
    Check(endpoint_requests == 1 && f.fires == 1 && !task.Drained());
    frame = f.Frame(2, 0.2f);
    task.Service(0.2, 2);
    Check(f.fires == 2 && f.updates == 1);
    f.native.arrived = 2;
    frame = f.Frame(3);
    task.Service(0.1, 3);
    Check(!task.Drained() && f.cancels == 0);
    f.native.closed = true;
    f.native.active = 0;
    frame = f.Frame(4);
    task.Service(0.1, 4);
    task.Service(0, 4);
    Check(task.Drained() && f.closes == 1 && f.cancels == 0);
}
void TransientOwnership() {
    Fixture f;
    detail::VisualSceneTransients resources(f.owner);
    bool model_released = false;
    unsigned releases = 0, reports = 0;
    auto change = std::make_unique<api::ActorModelChange>(api::ActorModelChangeServices{
        [] { return true; }, [] { return api::ChangeResult{}; }, [] { return api::ChangeResult{}; }, [&] {
            ++releases;
            return model_released;
        }});
    resources.KeepModelChange(std::move(change));
    resources.CollectModelChanges([&](const auto&) { ++reports; });
    resources.CollectModelChanges([&](const auto&) { ++reports; });
    Check(!resources.Drained() && releases == 2 && reports == 1);
    model_released = true;
    resources.CollectModelChanges([&](const auto&) { ++reports; });
    Check(resources.Drained() && releases == 3 && reports == 1);

    auto batch = std::make_shared<VisualProjectileBatch>(f.owner, f.Plan(), f.Services());
    resources.KeepProjectile(batch);
    auto frame = f.Frame(1);
    resources.Service(&frame, false);
    Check(f.fires == 1 && !resources.Drained());
    auto stale = f.target;
    ++stale.token.generation;
    Check(resources.ReleaseParticipant(stale) && !batch->Status().cancelled);
    f.delayed = true;
    Check(!resources.ReleaseParticipant(f.target));
    resources.Service(&frame, false);
    Check(!resources.Drained() && batch->Status().cancelled);
    f.delayed = false;
    resources.Service(&frame, false);
    Check(resources.Drained() && batch->Status().closed && f.closes == 1);

    auto context = std::make_shared<VisualEffectContext>(
        std::make_shared<api::PresentationEffects>(api::EffectRequestServices{{}, api::EffectAccess({}), {}}));
    unsigned starts = 0;
    SpatialHitServices services;
    services.create = [&]() -> std::unique_ptr<api::OwnedEffectRequest> {
        ++starts;
        return {};
    };
    auto hits = std::make_shared<SpatialHitEffects>(VisualSpatialAnchor{}, std::move(services));
    HitInput hit;
    hit.effect = HitEffectInput{"point.prefab"};
    hit.effect->delay = .25f;
    auto receipt = hits->Submit(hit);
    resources.KeepPointHit(context, "point", hits, 1);
    Check(resources.FindPointHit(context, "point") == hits && !resources.FindPointHit(context, "other"));
    frame = f.Frame(2);
    frame.paused = {false, {true, 0}};
    resources.Service(&frame, false);
    resources.Service(&frame, false); // one clock advance per official frame
    Check(!starts && !receipt.status->closed);
    frame = f.Frame(3);
    frame.paused = {true, {true, 0}};
    resources.Service(&frame, false);
    Check(!starts && !receipt.status->closed);
    Check(resources.ReleaseParticipant(stale) && !receipt.status->cancelled);
    Check(!resources.ReleaseParticipant(f.owner) && receipt.status->cancelled);
    resources.Service(&frame, false);
    Check(!resources.Drained() && receipt.status->closed && !starts);
    context->End();
    context->Service();
    resources.Service(&frame, false);
    Check(context->Drained() && resources.Drained());
}
}
void TestVisualProjectiles() {
    Batches();
    Tasks();
    TransientOwnership();
}
