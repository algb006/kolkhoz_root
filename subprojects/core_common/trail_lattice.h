/// @file
/// @brief The trails' lattice and the ground's states (#2072 (A); roads.md §3): the pieces the
///        road index composes a walk from - a lattice edge's cost, the query door (the straight
///        line weighted by the edges it crosses), the walker's door (A* over the lattice), and
///        the ground's state by a bed's condition. Private to core_common (road_route.cpp).
/// @threading PARALLEL_READONLY
/// Pure functions of their arguments.

#ifndef CORE_COMMON_TRAIL_LATTICE_H_
#define CORE_COMMON_TRAIL_LATTICE_H_

#include <cstdint>
#include <memory>
#include <unordered_map>
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

/// @brief The day's trails made O(1) to look up (#2072 CPU: a binary search per crossed edge
///        was 72 % of a trodden run's extra time). Built once with the day's weights over the
///        day index's own copy of the table, which outlives it. An answer is the table's own.
struct TrailLookup {
  const TrailTable* table = nullptr;
  /// Every row by its edge's key (an edge between non-neighbouring cells lives only here).
  std::unordered_map<std::uint64_t, const TrailRow*> rows;
  /// THE DENSE GRID (#2072 CPU: a hash lookup per sampled cell was still ~24 us a leg): over
  /// the trails' box, for each cell its four edges to lesser-first neighbours ((0,+1), (+1,-1),
  /// (+1,0), (+1,+1)), the row or nullptr - an array index instead of a hash.
  TrailEnd origin;
  std::int32_t columns = 0;
  std::int32_t rows_of_cells = 0;
  std::vector<const TrailRow*> grid;
  /// THE NEAREST TRAIL, in cells (#2116 item 3): per grid cell the octile distance to the
  /// nearest cell any trail touches (0 on one) - the walker's search's heuristic reads it.
  std::vector<float> nearest_trail;

  /// @brief The row of the edge between two cells (either way), or nullptr.
  const TrailRow* Find(TrailEnd a, TrailEnd b) const;

  /// @brief A LOWER BOUND, in cells, on the octile distance from `cell` to the nearest cell a
  ///        trail touches: exact inside the trails' box, the distance to the box outside it,
  ///        infinity with no trail at all.
  float NearestTrailCells(TrailEnd cell) const;

  bool Empty() const { return table == nullptr || table->rows.empty(); }

  float WeightFull() const { return table != nullptr ? table->weight_full : 1.10F; }
};

/// @brief The lookup over `table` (which must outlive it).
TrailLookup MakeLookup(const TrailTable& table);

/// @brief A shared lookup over `table`, or null with no table or no row.
std::shared_ptr<const TrailLookup> LookupOf(const TrailTable* table);

/// @brief The empty lookup: open ground everywhere.
const TrailLookup& NoLookup();

/// @brief A lattice edge's cost a km for a walker: open ground's weight lowered by the edge's
///        trail toward weight_full, times the ground's cost (a snow-bearing edge as wet).
float StepCost(const TrailLookup& trails,
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
float StraightKm(const TrailLookup& trails,
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
float RouteKm(const TrailLookup& trails,
              Vec2 from,
              Vec2 to,
              float open_weight,
              float open_cost,
              float snow_trail_cost,
              std::vector<std::pair<TrailEnd, TrailEnd>>* edges);

/// @brief A new day's process-unique serial (never 0, never reused): what the memos key on.
std::uint64_t NextDaySerial();

/// @brief StraightKm through this thread's memo of the day `serial` (#2072 CPU: the same legs
///        are priced again and again all day). Exact: a hit returns the very value StraightKm
///        gave for the same day, ends and costs. Cleared when the day changes.
float StraightKmMemo(std::uint64_t serial,
                     const TrailLookup& trails,
                     Vec2 from,
                     Vec2 to,
                     float open_weight,
                     float open_cost,
                     float snow_trail_cost);

/// @brief RouteKm through this thread's memo of the day `serial` - the km and the steps; the
///        timing door and the tread ask the same home -> work pair. Exact, as StraightKmMemo.
///        Serial 0 (a neutral day: no trail) is keyed too: its route is pure geometry.
float RouteKmMemo(std::uint64_t serial,
                  const TrailLookup& trails,
                  Vec2 from,
                  Vec2 to,
                  float open_weight,
                  float open_cost,
                  float snow_trail_cost,
                  std::vector<std::pair<TrailEnd, TrailEnd>>* edges);

}  // namespace core::lattice

#endif  // CORE_COMMON_TRAIL_LATTICE_H_
