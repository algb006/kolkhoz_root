// Unit test of core_logistics: the subsystem contract shape.
// The whole subsystem is an instant-delivery STUB in phase 1 (task O0); this
// test grows when logistics becomes real.

#include <type_traits>

#include "core_logistics/logistics_system.h"

static_assert(std::is_abstract_v<core::ILogisticsSystem>, "ILogisticsSystem is a contract");
static_assert(std::has_virtual_destructor_v<core::ILogisticsSystem>,
              "implementations are destroyed through the interface");

int main() {
  return 0;
}
