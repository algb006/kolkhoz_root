// Implementation of the core_world boundary (include/core_world/world.h),
// part one: world genesis. CreateStandardSimulation — the wiring of the
// subsystem stubs into a running engine — is task O2 and lands next to this.

#include "core_world/world.h"

#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/random.h"
#include "core_common/world_state.h"

namespace core {

namespace {

/// Stream id of the world's sequential RNG. Other streams derived from the
/// same seed (per-subsystem, if ever needed) must pick distinct ids.
constexpr std::uint64_t kWorldRngStream = 0;

}  // namespace

WorldState CreateStartWorld(const ITableSet& /*tables*/, std::uint64_t world_seed) {
  // STUB until stage 3: no resident, family, field or unit rows — the empty
  // world of the stage-1 criterion. Tables are not read yet; the genesis of
  // the designed start (80 residents, 21 yards, 160 ha) is built here at
  // stage 3.
  WorldState world;
  world.world_seed = world_seed;
  world.rng = SeedRngState(world_seed, kWorldRngStream);
  RefreshCalendarCaches(world.calendar, Weekday::kMonday);
  return world;
}

}  // namespace core
