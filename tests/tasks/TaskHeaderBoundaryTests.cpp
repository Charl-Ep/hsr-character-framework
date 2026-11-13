#include <hksr/framework/tasks/TaskSequence.hpp>
#include <hksr/framework/tasks/TaskPresentation.hpp>
#include <type_traits>

namespace hksr::framework {
class ActionPlayback;
struct VisualProgram;
template <class T, class = void> struct Complete : std::false_type {};
template <class T> struct Complete<T, std::void_t<decltype(sizeof(T))>> : std::true_type {};
static_assert(!Complete<ActionPlayback>::value, "Task contracts must not include the action player implementation");
static_assert(!Complete<VisualProgramPlayer>::value,
              "Task contracts must not include the visual player implementation");
static_assert(!Complete<VisualProgram>::value,
              "Task instructions must remain independent of whole-program definitions");
static_assert(sizeof(TaskLeafOperation) > 0 && sizeof(TaskPresentationServices) > 0);
}
int main() {
    return 0;
}
