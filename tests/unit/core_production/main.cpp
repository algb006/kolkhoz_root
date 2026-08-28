// Unit test of core_production: the subsystem contract shape.
// The STUB arrives with task O0, the real system at stage 4; this test grows
// with them.

#include <type_traits>

#include "core_production/production_system.h"

static_assert(std::is_abstract_v<core::IProductionSystem>, "IProductionSystem is a contract");
static_assert(std::has_virtual_destructor_v<core::IProductionSystem>,
              "implementations are destroyed through the interface");

int main() {
  return 0;
}
