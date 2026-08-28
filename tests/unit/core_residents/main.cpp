// Unit test of core_residents: contract shape and the O0 no-op STUB.
// Grows with the real system at stage 3.

#include <iostream>
#include <string_view>
#include <type_traits>

#include "core_common/world_state.h"
#include "core_residents/residents_system.h"
#include "core_tables/tables.h"

static_assert(std::is_abstract_v<core::IResidentsSystem>, "IResidentsSystem is a contract");
static_assert(std::has_virtual_destructor_v<core::IResidentsSystem>,
              "implementations are destroyed through the interface");

namespace {

int Expect(bool condition, const char* label) {
  if (condition) {
    return 0;
  }
  std::cout << "FAIL: " << label << '\n';
  return 1;
}

class EmptyTableSet final : public core::ITableSet {
 public:
  const core::ITable* FindTable(std::string_view /*name*/) const override { return nullptr; }

  std::uint32_t TableCount() const override { return 0; }

  std::string_view TableName(std::uint32_t /*index*/) const override { return {}; }
};

}  // namespace

int main() {
  int failures = 0;
  const EmptyTableSet tables;
  const auto system = core::CreateResidentsSystem(tables);
  failures += Expect(system != nullptr, "factory yields a system");

  const core::WorldState previous;
  core::WorldState current = previous;

  // No family table exists yet: both parallel slots iterate nothing.
  failures +=
      Expect(system->NeedsPhase().ParallelItemCount(previous) == 0, "needs stub has zero items");
  failures += Expect(system->MetricsPhase().ParallelItemCount(previous) == 0,
                     "metrics stub has zero items");
  system->NeedsPhase().RunItemRange(previous, current, 0, 0);
  system->MetricsPhase().RunItemRange(previous, current, 0, 0);

  // The demography stub must not touch the world.
  system->RunDemographyDecisions(previous, current);
  failures +=
      Expect(current.calendar.tick == previous.calendar.tick && current.epoch == previous.epoch,
             "stubs leave the world unchanged");

  if (failures == 0) {
    std::cout << "unit_core_residents: all checks passed\n";
  }
  return failures;
}
