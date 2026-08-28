// Unit test of core_world: the wiring config contract and the O0 world
// genesis STUB. CreateStandardSimulation coverage arrives with task O2.

#include <iostream>
#include <string_view>

#include "core_common/calendar.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

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

  // The wiring config must default to the deterministic verification setup:
  // no tables, seed 0, one worker (world.h).
  const core::StandardSimulationConfig config;
  failures += Expect(config.tables == nullptr, "config defaults to no tables");
  failures += Expect(config.world_seed == 0, "config defaults to seed 0");
  failures += Expect(config.worker_count == 1, "config defaults to the verification mode");

  // Genesis STUB: an empty world at day 0, deterministic from the seed.
  const EmptyTableSet tables;
  const core::WorldState world = core::CreateStartWorld(tables, 12345);
  failures += Expect(world.world_seed == 12345, "genesis stores the seed");
  failures += Expect(world.calendar.tick == 0, "genesis starts at tick 0");
  failures +=
      Expect(world.calendar.weekday == core::Weekday::kMonday, "stub campaign starts on Monday");
  failures += Expect(world.calendar.season == core::Season::kWinter, "day 0 is winter");
  failures += Expect(world.epoch == core::Epoch::kOne, "the campaign starts in Epoch I");
  failures += Expect((world.rng.stream & 1U) == 1U, "the world RNG is seeded (odd stream)");

  const core::WorldState same_seed = core::CreateStartWorld(tables, 12345);
  const core::WorldState other_seed = core::CreateStartWorld(tables, 54321);
  failures += Expect(same_seed.rng.state == world.rng.state, "same seed — same world RNG");
  failures +=
      Expect(other_seed.rng.state != world.rng.state, "different seed — different world RNG");

  // The stage-1 criterion: the empty world ticks 10 000 steps, and the
  // result is identical with one worker and with many.
  constexpr std::uint32_t kCriterionSteps = 10000;
  core::StandardSimulationConfig config_single;
  config_single.tables = &tables;
  config_single.world_seed = 7;
  config_single.worker_count = 1;
  const auto single = core::CreateStandardSimulation(config_single);
  for (std::uint32_t step = 0; step < kCriterionSteps; ++step) {
    single->AdvanceStep();
  }
  const core::WorldState& state = single->CompletedState();
  failures += Expect(state.calendar.tick == kCriterionSteps, "10 000 steps advance 10 000 ticks");
  const core::SimDay expected_day = kCriterionSteps / core::kTicksPerDay;
  failures += Expect(state.calendar.day == expected_day, "the day matches the tick count");
  const core::Date expected_date = core::DateFromDay(expected_day);
  failures += Expect(state.calendar.date.year == expected_date.year &&
                         state.calendar.date.month == expected_date.month,
                     "the date matches the calendar arithmetic");
  failures +=
      Expect(state.calendar.weekday == core::WeekdayFromDay(expected_day, core::Weekday::kMonday),
             "the weekday matches the calendar arithmetic");

  core::StandardSimulationConfig config_many = config_single;
  config_many.worker_count = 3;
  const auto many = core::CreateStandardSimulation(config_many);
  for (std::uint32_t step = 0; step < kCriterionSteps; ++step) {
    many->AdvanceStep();
  }
  const core::WorldState& many_state = many->CompletedState();
  failures += Expect(many_state.calendar.tick == state.calendar.tick &&
                         many_state.calendar.day == state.calendar.day &&
                         many_state.rng.state == state.rng.state &&
                         many_state.rng.stream == state.rng.stream &&
                         many_state.world_seed == state.world_seed,
                     "one worker and three workers agree after 10 000 steps");

  if (failures == 0) {
    std::cout << "unit_core_world: all checks passed\n";
  }
  return failures;
}
