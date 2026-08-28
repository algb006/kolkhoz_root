// Unit test of core_time: the subsystem contract shape.
// The calendar-advancing STUB arrives with task O0, the real system at
// stage 2; this test grows with them.

#include <type_traits>

#include "core_time/time_system.h"

static_assert(std::is_abstract_v<core::ITimeSystem>, "ITimeSystem is a contract");
static_assert(std::has_virtual_destructor_v<core::ITimeSystem>,
              "implementations are destroyed through the interface");

int main() {
  return 0;
}
