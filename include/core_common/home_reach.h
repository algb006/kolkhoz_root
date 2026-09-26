/// @file
/// @brief How far a place is from where people sleep: the road, one way,
///        from the nearest lived-in house, by the way a mode travels.
/// @threading PARALLEL_READONLY
/// A pure read of the world (units and the road index); called from the
/// sequential slots and between steps.
///
/// ONE HOME FOR TWO READERS (0.36.29): core_production's unreachable-stand
/// alarms and core_labor's felling offers ask the same question of the same
/// road, so the answer lives here rather than in one of them — the day they
/// asked it by two functions was the day the canon marked stands the core
/// then refused (0.36.12), from the other side.

#ifndef CORE_COMMON_HOME_REACH_H_
#define CORE_COMMON_HOME_REACH_H_

#include "core_common/geometry.h"
#include "core_common/road_route.h"

namespace core {

struct WorldState;

/// @brief Game hours of the road, one way, from the nearest lived-in house
///        to `place` at `speed_kmh`; negative when nobody lives anywhere or
///        the speed is not positive. BY THE WAY `mode` travels (road_route.h;
///        0.36.2): a team, a log cart off the roads, a walker.
float NearestHomeTravelHours(const WorldState& world, Vec2 place, float speed_kmh, TravelMode mode);

}  // namespace core

#endif  // CORE_COMMON_HOME_REACH_H_
