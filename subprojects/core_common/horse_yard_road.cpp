#include "core_common/horse_yard_road.h"

#include <cstdint>

#include "core_common/herd_state.h"
#include "core_common/labor_state.h"
#include "core_common/road_route.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/work_seam.h"
#include "core_common/world_state.h"

namespace core {

bool HorseYardPositionOf(const WorldState& world, LivestockKindId horse_kind, Vec2& yard) {
  if (world.chairman.horses_stabled == 0 || horse_kind.value == kInvalidDefIdValue) {
    return false;
  }
  for (const HerdRow& herd : world.herds.rows) {
    // The kolkhoz team: no family's, and standing at a unit (StableHorses).
    if (herd.kind.value != horse_kind.value || herd.household_owned != 0 ||
        herd.household.value != kInvalidEntityIdValue || herd.unit.value == kInvalidEntityIdValue) {
      continue;
    }
    const std::uint32_t row = FindRow(world.units, herd.unit);
    if (row == kNoRow || world.units.rows[row].level == 0) {
      return false;  // the yard is gone, or is still pegs and string
    }
    yard = world.units.rows[row].position;
    return true;
  }
  return false;
}

bool DayStartsAtHorseYard(const WorldState& world, const WorkAssignment& work) {
  if (world.chairman.horses_stabled == 0) {
    return false;
  }
  return IsHorseWork(work.kind) || work.rides_horse != 0;
}

float WorkRoadHours(const WorldState& world,
                    const WorkAssignment& work,
                    LivestockKindId horse_kind,
                    Vec2 home,
                    Vec2 target,
                    float walk_hours_per_km,
                    float ride_hours_per_km) {
  const bool rides = WorkRidesOut(world, work);
  const TravelMode mode = WorkTravelMode(world, work);
  Vec2 yard{};
  if (rides && DayStartsAtHorseYard(world, work) && HorseYardPositionOf(world, horse_kind, yard)) {
    // HE WALKS TO THE HORSE (livestock design §5; the human's word of
    // 2 October 2026: «Возчик должен идти на конный двор»), and rides from
    // there. Until 0.37.158 the way was measured from his house, on the
    // horse the whole way, and the yard's place cost nobody an hour.
    const float walk = RoadKm(world, TravelMode::kWalk, home, yard) * walk_hours_per_km;
    if (work.kind == WorkKind::kHauling) {
      return walk;  // the ride to the load is the first trip's empty half
    }
    return walk + (RoadKm(world, mode, yard, target) * ride_hours_per_km);
  }
  return RoadKm(world, mode, home, target) * (rides ? ride_hours_per_km : walk_hours_per_km);
}

}  // namespace core
