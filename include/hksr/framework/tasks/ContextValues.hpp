#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <variant>

namespace hksr::framework {
using ContextValue = std::variant<bool, std::int64_t, double, std::string>;
using ContextValues = std::map<std::string, ContextValue>;
}
