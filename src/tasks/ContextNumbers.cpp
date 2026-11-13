#include <hksr/framework/tasks/ContextNumbers.hpp>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace hksr::framework {
void ValidateContextNumber(const ContextNumber& value) {
    if (const auto* number = std::get_if<double>(&value); number && !std::isfinite(*number))
        throw std::invalid_argument("nonfinite context number");
}
bool CompareContextNumbers(const ContextNumber& left, const ContextNumber& right, ContextNumberComparison comparison) {
    ValidateContextNumber(left);
    ValidateContextNumber(right);
    if (left.index() != right.index())
        throw std::invalid_argument("context number types differ");
    return std::visit(
        [&](const auto a) {
            const auto b = std::get<std::decay_t<decltype(a)>>(right);
            switch (comparison) {
            case ContextNumberComparison::Equal:
                return a == b;
            case ContextNumberComparison::NotEqual:
                return a != b;
            case ContextNumberComparison::Less:
                return a < b;
            case ContextNumberComparison::LessEqual:
                return a <= b;
            case ContextNumberComparison::Greater:
                return a > b;
            case ContextNumberComparison::GreaterEqual:
                return a >= b;
            }
            throw std::invalid_argument("unknown context number comparison");
        },
        left);
}
void ValidateContextNumberRule(const ContextNumberRule& rule) {
    ValidateContextNumber(rule.initial);
    if (rule.minimum && !CompareContextNumbers(rule.initial, *rule.minimum, ContextNumberComparison::GreaterEqual))
        throw std::invalid_argument("context initial value below minimum");
    if (rule.maximum && !CompareContextNumbers(rule.initial, *rule.maximum, ContextNumberComparison::LessEqual))
        throw std::invalid_argument("context initial value above maximum");
}
ContextNumber ChangeContextNumber(const ContextNumber& current, const ContextNumberChange& change,
                                  const ContextNumberRule& rule) {
    ValidateContextNumberRule(rule);
    ValidateContextNumber(current);
    ValidateContextNumber(change.operand);
    if (current.index() != rule.initial.index() || current.index() != change.operand.index())
        throw std::invalid_argument("context mutation types differ");
    auto result = std::visit(
        [&](const auto a) -> ContextNumber {
            using T = std::decay_t<decltype(a)>;
            const auto b = std::get<T>(change.operand);
            if (change.operation == ContextNumberOperation::Set)
                return b;
            if constexpr (std::is_same_v<T, std::int64_t>) {
                constexpr auto low = (std::numeric_limits<T>::min)(), high = (std::numeric_limits<T>::max)();
                if (change.operation == ContextNumberOperation::Add) {
                    if ((b > 0 && a > high - b) || (b < 0 && a < low - b))
                        throw std::overflow_error("context sum overflow");
                    return a + b;
                }
                if (change.operation == ContextNumberOperation::Multiply) {
                    if ((a > 0 && ((b > 0 && a > high / b) || (b < 0 && b < low / a))) ||
                        (a < 0 && ((b > 0 && a < low / b) || (b < 0 && a < high / b))))
                        throw std::overflow_error("context product overflow");
                    return a * b;
                }
            } else {
                if (change.operation == ContextNumberOperation::Add)
                    return a + b;
                if (change.operation == ContextNumberOperation::Multiply)
                    return a * b;
            }
            throw std::invalid_argument("unknown context number operation");
        },
        current);
    ValidateContextNumber(result);
    if (rule.minimum && CompareContextNumbers(result, *rule.minimum, ContextNumberComparison::Less))
        result = *rule.minimum;
    if (rule.maximum && CompareContextNumbers(result, *rule.maximum, ContextNumberComparison::Greater))
        result = *rule.maximum;
    return result;
}
}
