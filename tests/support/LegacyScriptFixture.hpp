#pragma once
#include <hksr/framework/tasks/TaskCatalog.hpp>
#include <hksr/framework/presentation/scene/VisualProgram.hpp>
#include <string>

// Offline conversion of retained test fixtures; never linked into product targets.
namespace hksr::framework::test {
TaskCatalog ParseLegacyTaskCatalog(const std::string&);
VisualProgram ParseLegacyVisualProgram(const std::string&);
}
using hksr::framework::test::ParseLegacyTaskCatalog;
using hksr::framework::test::ParseLegacyVisualProgram;
