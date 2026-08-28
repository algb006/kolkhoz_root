// Unit test of core_world: the wiring config contract and the O0 world
// genesis STUB. CreateStandardSimulation coverage arrives with task O2.

#include <iostream>
#include <string_view>

#include "core_common/calendar.h"
#include "core_common/state_table_ops.h"
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

  // Stage-3 genesis: the designed start, deterministic from the seed.
  failures += Expect(world.residents.rows.size() == 80, "genesis seats 80 residents");
  failures += Expect(world.families.rows.size() == 21, "genesis builds 21 yards");
  failures +=
      Expect(same_seed.residents.rows.size() == world.residents.rows.size() &&
                 same_seed.residents.rows[10].birth_day == world.residents.rows[10].birth_day,
             "genesis is reproducible from the seed");
  std::uint32_t children = 0;
  std::uint32_t old_timers = 0;
  bool links_hold = true;
  for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
    const core::ResidentRow& resident = world.residents.rows[row];
    links_hold = links_hold && core::FindRow(world.families, resident.family) != core::kNoRow;
    // Life speedup 4: one biological year is 12 game days.
    const float age_years = static_cast<float>(-resident.birth_day) / 12.0F;
    children += age_years < 16.0F ? 1 : 0;
    old_timers += age_years >= 60.0F ? 1 : 0;
  }
  failures += Expect(links_hold, "every starting resident's family exists");
  failures += Expect(children >= 25 && children <= 35, "about 30 children at the start");
  failures += Expect(old_timers >= 8 && old_timers <= 14, "about 11 old-timers at the start");

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
  failures +=
      Expect(many_state.residents.rows.size() == state.residents.rows.size() &&
                 many_state.residents.next_id_value == state.residents.next_id_value &&
                 many_state.families.rows.size() == state.families.rows.size() &&
                 many_state.families.rows[0].satisfaction == state.families.rows[0].satisfaction,
             "the population and its metrics agree across worker counts");

  if (failures == 0) {
    std::cout << "unit_core_world: all checks passed\n";
  }
  return failures;
}
