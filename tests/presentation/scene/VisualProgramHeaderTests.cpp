#include <hksr/framework/presentation/scene/VisualProgram.hpp>
#include <hksr/framework/presentation/scene/VisualProgramLibrary.hpp>
#include <hksr/framework/presentation/partners/VisualPartners.hpp>
#include <hksr/framework/presentation/timeline/VisualTimelines.hpp>
#include <type_traits>

namespace hksr::framework {
class VisualProgramPlayer;
struct VisualProgramServices;
struct VisualProgramBindings;
struct VisualProgramProviders;
template <class T, class = void> struct CompleteVisualContract : std::false_type {};
template <class T>
struct CompleteVisualContract<T, std::void_t<decltype(sizeof(T))>> : std::true_type {};
static_assert(CompleteVisualContract<VisualProgram>::value);
static_assert(CompleteVisualContract<VisualProgramInput>::value);
static_assert(CompleteVisualContract<VisualPartners>::value);
static_assert(CompleteVisualContract<VisualTimelines>::value);
static_assert(!CompleteVisualContract<VisualProgramPlayer>::value,
              "Visual asset and leaf contracts must not include the concrete player");
static_assert(!CompleteVisualContract<VisualProgramServices>::value,
              "Visual asset and leaf contracts must not include player service assembly");
static_assert(!CompleteVisualContract<VisualProgramBindings>::value &&
                  !CompleteVisualContract<VisualProgramProviders>::value,
              "Visual definitions must not include runtime binding contracts");
}
