// Unit test of core_logistics: contract shape and the O0 instant-delivery
// STUB. Grows when logistics becomes real (project phase 2).

#include <iostream>
#include <string_view>
#include <type_traits>

#include "../../common/fake_tables.h"
#include "core_common/world_state.h"
#include "core_logistics/logistics_system.h"
#include "core_tables/tables.h"

static_assert(std::is_abstract_v<core::ILogisticsSystem>, "ILogisticsSystem is a contract");
static_assert(std::has_virtual_destructor_v<core::ILogisticsSystem>,
              "implementations are destroyed through the interface");

namespace {

int Expect(bool condition, const char* label) {
  if (condition) {
    return 0;
  }
  std::cout << "FAIL: " << label << '\n';
  return 1;
}

}  // namespace

int main() {
  int failures = 0;
  const test::FakeTableSet tables;
  const auto system = core::CreateLogisticsSystem(tables);
  failures += Expect(system != nullptr, "factory yields a system");

  const core::WorldState previous;
  core::WorldState current = previous;
  failures += Expect(system->LogisticsPhase().ParallelItemCount(previous) == 0,
                     "instant delivery has nothing in flight");
  system->LogisticsPhase().RunItemRange(previous, current, 0, 0);
  failures += Expect(current.calendar.tick == previous.calendar.tick,
                     "the stub leaves the world unchanged");

  if (failures == 0) {
    std::cout << "unit_core_logistics: all checks passed\n";
  }
  return failures;
}
