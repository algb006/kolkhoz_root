/// @file
/// @brief The reaping brigade's road from the village to a field, in game
/// hours: the one home of the road the count by fields takes
/// (core_common/reaping_pace.h) — the gathering alarm and labor's last days
/// before the snow both ask it.
/// @threading PARALLEL_READONLY
/// A pure read of a completed state.
///
/// WHY NOT EACH MAN'S OWN ROAD. The count asks about the days to come, and
/// who reaps a field tomorrow is the morning's placement, not yet made. The
/// village's mean home stands in for the crew: a brigade goes out together
/// (the brigade's cart, 0.37.89), and the homes of a village lie within a few
/// hundred metres of one another while the fields lie kilometres off.

#ifndef CORE_COMMON_REAPING_ROAD_H_
#define CORE_COMMON_REAPING_ROAD_H_

#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/geometry.h"
#include "core_common/road_route.h"
#include "core_common/work_seam.h"
#include "core_common/world_state.h"

namespace core {

/// @brief One-way road from the mean home of the village's families to
/// `field`, game hours.
/// @param by_cart True when the brigade rides its cart — the kolkhoz has a
///        horse to give it; false, it walks.
/// @param walk_speed_kmh, harness_speed_kmh Real-world speeds (transport.csv),
///        divided by kClockScale for the game hour as everywhere.
/// @return 0 for a world with no family that has a home, or a speed of
///         nought: no road is counted rather than an endless one.
inline float ReapingRoadHours(const WorldState& world,
                              Vec2 field,
                              bool by_cart,
                              float walk_speed_kmh,
                              float harness_speed_kmh) {
  double sum_x = 0.0;
  double sum_y = 0.0;
  std::uint32_t homes = 0;
  for (std::uint32_t row = 0; row < world.families.rows.size(); ++row) {
    Vec2 home;
    if (HomePositionOf(world, world.families.row_ids[row], home)) {
      sum_x += static_cast<double>(home.x);
      sum_y += static_cast<double>(home.y);
      ++homes;
    }
  }
  const float speed_kmh = by_cart ? harness_speed_kmh : walk_speed_kmh;
  if (homes == 0 || !(speed_kmh > 0.0F)) {
    return 0.0F;
  }
  const Vec2 village{.x = static_cast<Meters>(sum_x / static_cast<double>(homes)),
                     .y = static_cast<Meters>(sum_y / static_cast<double>(homes))};
  // The mode labour measures a riding brigade's road by (work_seam.cpp,
  // WorkTravelMode: a work that rides out and is not hauling goes as a team).
  const float km = RoadKm(world, by_cart ? TravelMode::kTeam : TravelMode::kWalk, village, field);
  return km * static_cast<float>(kClockScale) / speed_kmh;
}

}  // namespace core

#endif  // CORE_COMMON_REAPING_ROAD_H_
