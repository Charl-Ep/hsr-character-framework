#pragma once
#include <hksr/model/ProfileInputs.hpp>
#include <hksr/presentation/events/AnimationEvents.hpp>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using TestManagedClass = std::pair<std::string, std::string>;

// Ask the installed SDK for its current input types without allocating engine objects.
struct TestModelInputDeclarations {
    TestManagedClass operation, data, events;
    TestModelInputDeclarations() {
        struct Recorder final : hksr::api::ManagedObjects {
            TestManagedClass operation;
            std::vector<std::unique_ptr<TestManagedClass>> types;
            hksr::api::ObjectResult NewStringUtf8(const char*) override {
                throw std::logic_error("unexpected declaration string allocation");
            }
            hksr::api::ObjectResult AllocateObject(const char* space, const char* name) override {
                operation = {space, name};
                return {{true, 0}, &operation};
            }
            hksr::api::ObjectResult ManagedType(const char* space, const char* name) override {
                types.push_back(std::make_unique<TestManagedClass>(space, name));
                return {{true, 0}, types.back().get()};
            }
        } recorder;
        hksr::api::ProfileInputs({}).AllocateOperation(recorder);
        operation = recorder.operation;
        const auto types = hksr::api::ResolveAnimationInputTypes(recorder);
        if (!types.data.call.returned || !types.data.object || !types.events.call.returned || !types.events.object)
            throw std::runtime_error("SDK input type declarations unavailable");
        data = *static_cast<TestManagedClass*>(types.data.object);
        events = *static_cast<TestManagedClass*>(types.events.object);
    }
};
