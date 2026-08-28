// Unit test of core_residents: the subsystem contract shape.
// The STUB arrives with task O0, the real system at stage 3; this test grows
// with them.

#include <type_traits>

#include "core_residents/residents_system.h"

static_assert(std::is_abstract_v<core::IResidentsSystem>, "IResidentsSystem is a contract");
static_assert(std::has_virtual_destructor_v<core::IResidentsSystem>,
              "implementations are destroyed through the interface");

int main() {
  return 0;
}
