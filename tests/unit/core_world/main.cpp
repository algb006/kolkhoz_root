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

  if (failures == 0) {
    std::cout << "unit_core_world: all checks passed\n";
  }
  return failures;
}
