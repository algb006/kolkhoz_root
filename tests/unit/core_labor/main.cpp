// Unit test of core_labor: the subsystem contract shape.
// The STUB arrives with task O0, the real system at stage 5; this test grows
// with them.

#include <type_traits>

#include "core_labor/labor_system.h"

static_assert(std::is_abstract_v<core::ILaborSystem>, "ILaborSystem is a contract");
static_assert(std::has_virtual_destructor_v<core::ILaborSystem>,
              "implementations are destroyed through the interface");

int main() {
  return 0;
}
