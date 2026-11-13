#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>

namespace hksr::framework {
using ContextNumber = std::variant<std::int64_t, double>;
using ContextNumbers = std::map<std::string, ContextNumber>;
enum class ContextNumberOperation { Set, Add, Multiply };
enum class ContextNumberComparison { Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual };
struct ContextNumberRule {
    ContextNumber initial = std::int64_t(0);
    std::optional<ContextNumber> minimum, maximum;
};
struct ContextNumberChange {
    std::string name;
    ContextNumberOperation operation = ContextNumberOperation::Set;
    ContextNumber operand = std::int64_t(0);
};
void ValidateContextNumber(const ContextNumber&);
void ValidateContextNumberRule(const ContextNumberRule&);
bool CompareContextNumbers(const ContextNumber&, const ContextNumber&, ContextNumberComparison);
ContextNumber ChangeContextNumber(const ContextNumber&, const ContextNumberChange&, const ContextNumberRule&);
}
