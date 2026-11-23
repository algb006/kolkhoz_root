/// @file
/// @brief The trails' lattice and the ground's states (#2072 (A); roads.md §3): the pieces the
///        road index composes a walk from - a lattice edge's cost, the query door (the straight
///        line weighted by the edges it crosses), the walker's door (A* over the lattice), and
///        the ground's state by a bed's condition. Private to core_common (road_route.cpp).
/// @threading PARALLEL_READONLY
/// Pure functions of their arguments.

#ifndef CORE_COMMON_TRAIL_LATTICE_H_
#define CORE_COMMON_TRAIL_LATTICE_H_

#include <utility>
#include <vector>

#include "core_common/geometry.h"
#include "core_common/road_route.h"
#include "core_common/road_rules.h"
#include "core_common/trail_state.h"

namespace core::lattice {

/// @brief A road or path piece's ground by its bed's condition (roads.md §3): gravel in the mud
///        walks as wet; snow on a road or a laid path walks as wet - its sleigh track and its
///        feet keep it trodden (core's reading, 23 Nov 2026; «до расчистки» names open ground).
GroundState GroundOnRoad(RoadCondition condition, RoadBed bed);

/// @brief Open ground's state: the dirt bed's condition, snow off a trail as snow.
GroundState GroundOffRoad(RoadCondition dirt);

/// @brief A mode's factor in a state: the walker's and the horse team's; every other mode 1.
float GroundFactorOf(const GroundFactors& factors, TravelMode mode, GroundState state);

/// @brief A factor as a cost multiplier (a factor <= 0 is read as 1: no division by nought).
float CostOf(float factor);

/// @brief A lattice edge's cost a km for a walker: open ground's weight lowered by the edge's
///        trail toward weight_full, times the ground's cost (a snow-bearing edge as wet).
float StepCost(const TrailTable& trails,
               TrailEnd from_cell,
               TrailEnd to_cell,
               float open_weight,
               float open_cost,
               float snow_trail_cost);

/// @brief THE QUERY DOOR: the straight line's length times the MEAN cost of the lattice edges
///        it crosses (sampled every half cell from its lesser end, one canonical direction), so
///        a pair answers the same in either direction (static review of 23 Nov 2026: pricing each
///        piece by the edge last crossed gave the two directions different first and last cells).
///        Open ground where the line crosses no edge.
float StraightKm(const TrailTable& trails,
                 Vec2 from,
                 Vec2 to,
                 float open_weight,
                 float open_cost,
                 float snow_trail_cost);

/// @brief THE WALKER'S DOOR: A* over the lattice inside the box of `from`-`to` plus a margin,
///        8 neighbours, each step at StepCost; ties by the queue's (cost, cell index) order, so
///        every compiler walks the same way. The km is NORMALISED so an untrodden route costs
///        exactly the straight line (a lattice path runs up to 8.2 % over the line it follows:
///        the octile bound, about a full trail's whole discount) and a trodden one that times
///        its ratio to the untrodden. `edges` (nullable) gets the steps, in order.
float RouteKm(const TrailTable& trails,
              Vec2 from,
              Vec2 to,
              float open_weight,
              float open_cost,
              float snow_trail_cost,
              std::vector<std::pair<TrailEnd, TrailEnd>>* edges);

}  // namespace core::lattice

#endif  // CORE_COMMON_TRAIL_LATTICE_H_
