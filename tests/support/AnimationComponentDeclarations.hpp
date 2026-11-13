#pragma once
#include <hksr/animation/AnimationResources.hpp>
#include <hksr/entity/LifecycleHooks.hpp>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

struct TestAnimationComponentDeclarations {
    using Identity = std::pair<std::string, std::string>;
    std::map<hksr::api::AnimationComponent, Identity> types;

    explicit TestAnimationComponentDeclarations(hksr::api::ReferenceMethods references) {
        struct Recorder final : hksr::api::ManagedObjects {
            std::vector<std::unique_ptr<Identity>> declarations;
            void* requested = nullptr;
            hksr::api::ObjectResult NewStringUtf8(const char*) override {
                throw std::logic_error("unexpected component declaration string allocation");
            }
            hksr::api::ObjectResult AllocateObject(const char*, const char*) override {
                throw std::logic_error("unexpected component declaration allocation");
            }
            hksr::api::ObjectResult ManagedType(const char* space, const char* name) override {
                declarations.push_back(std::make_unique<Identity>(space, name));
                return {{true, 0}, declarations.back().get()};
            }
            static void* __fastcall Component(void*, void* type, void* context) {
                static_cast<Recorder*>(context)->requested = type;
                return nullptr;
            }
        } recorder;
        int entity;
        auto& tracked = hksr::api::TrackedEntities();
        const auto token = tracked.Track(reinterpret_cast<hksr::api::GameEntity*>(&entity));
        hksr::api::EntityAccessMethods access;
        access.component = {reinterpret_cast<void*>(&Recorder::Component), &recorder};
        hksr::api::AnimationResources resources(token, references, hksr::api::EntityAccess(access));
        try {
            using Role = hksr::api::AnimationComponent;
            for (auto role : {Role::Model, Role::FreeStyle, Role::Emotion, Role::Motion, Role::Transform}) {
                recorder.requested = nullptr;
                const auto component = resources.Component(role, recorder);
                if (!component.call.returned || component.object || !recorder.requested)
                    throw std::runtime_error("SDK component declaration was not observed");
                types.emplace(role, *static_cast<Identity*>(recorder.requested));
            }
            if (!resources.Close())
                throw std::runtime_error("component declaration references did not drain");
        } catch (...) {
            resources.Close();
            tracked.Forget(token);
            throw;
        }
        tracked.Forget(token);
    }
    bool Is(hksr::api::AnimationComponent role, const char* space, const char* name) const {
        return types.at(role) == Identity{space, name};
    }
};
