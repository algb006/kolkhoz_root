// Unit test of core_time: contract shape and the O0 calendar-advance STUB.
// Grows with the real weather system at stage 2.

#include <iostream>
#include <string_view>
#include <type_traits>
#include <utility>

#include "core_common/calendar.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_time/time_system.h"

static_assert(std::is_abstract_v<core::ITimeSystem>, "ITimeSystem is a contract");
static_assert(std::has_virtual_destructor_v<core::ITimeSystem>,
              "implementations are destroyed through the interface");

namespace {

int Expect(bool condition, const char* label) {
  if (condition) {
    return 0;
  }
  std::cout << "FAIL: " << label << '\n';
  return 1;
}

/// Table set with no tables: what the O0 stubs receive until the loader (O3)
/// and real tables exist.
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
  const auto time_system = core::CreateTimeSystem(tables);
  failures += Expect(time_system != nullptr, "factory yields a system");

  // Drive the phase the way the engine will: copy forward, run, swap.
  core::WorldState previous;
  core::WorldState current;
  core::ISequentialPhase& phase = time_system->TimeAndWeatherPhase();
  for (std::uint32_t tick = 0; tick < core::kTicksPerDay; ++tick) {
    current = previous;
    phase.RunSequential(previous, current);
    std::swap(previous, current);
  }
  failures += Expect(previous.calendar.tick == core::kTicksPerDay, "24 runs advance 24 ticks");
  failures += Expect(previous.calendar.day == 1, "24 ticks open day 1");
  failures += Expect(previous.calendar.weekday == core::Weekday::kTuesday,
                     "stub campaign starts on Monday, day 1 is Tuesday");
  failures +=
      Expect(previous.calendar.date.month == core::Month::kJanuary, "day 1 is still January");
  failures += Expect(previous.calendar.season == core::Season::kWinter, "January is winter");

  // On to the first day of year 2.
  while (previous.calendar.tick < core::kTicksPerYear) {
    current = previous;
    phase.RunSequential(previous, current);
    std::swap(previous, current);
  }
  failures += Expect(previous.calendar.date.year == 2, "kTicksPerYear ticks reach year 2");
  failures +=
      Expect(previous.calendar.date.month == core::Month::kJanuary, "year 2 starts in January");

  // STUB boundary: weather passes through unchanged until stage 2.
  failures += Expect(previous.weather.precipitation == core::Precipitation::kNone,
                     "stub leaves weather at defaults");

  if (failures == 0) {
    std::cout << "unit_core_time: all checks passed\n";
  }
  return failures;
}
