/// @file
/// @brief What water stands at a point, as the ice reads it (#1859): a river's
///        channel, still water, a backwater, a river's mouth in still water, or
///        dry land - from the map's areas and water lines.
/// @threading PARALLEL_READONLY
/// A pure function of the map data and the point.

#ifndef CORE_COMMON_ICE_WATER_H_
#define CORE_COMMON_ICE_WATER_H_

#include "core_common/geometry.h"
#include "core_common/ice_view.h"
#include "core_common/map_obstacles.h"

namespace core {

/// @brief The water at `point` on `map`.
///
/// THE ORDER OF THE ANSWERS: within a water line's channel (its half width at
/// the nearest segment, interpolated between the two points) - kRiver, a brook
/// as a river, both run; inside a lake or a pond - kInflow within
/// `inflow_disc_m` of a channel's edge, else kStill; inside a backwater or
/// shallows - kBackwater; else kNone.
/// STUB, the rapids: the map data carries none (the ice-order mask's B is the
/// layer's), so kRapids is never answered and the layer keeps the rapids open
/// by its mask.
/// @param inflow_disc_m Metres from a channel's edge that carry the river's
///        ice in still water (world_params `ice_inflow_disc_m`).
IceWater IceWaterAt(const MapObstacles& map, Vec2 point, float inflow_disc_m);

}  // namespace core

#endif  // CORE_COMMON_ICE_WATER_H_
