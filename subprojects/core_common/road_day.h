/// @file
/// @brief The road index ON A DAY (#2072's trails, roads.md §3's ground): the day's weights and
///        the bare network's day-aware doors, so road_day.cpp can wrap the bare index without
///        seeing its class (road_route.cpp keeps the network; this file keeps both inside the
///        size rule, core CLAUDE.md §5). Private to core_common.
/// @threading PARALLEL_READONLY
/// The weights are immutable once made and shared by the worlds that copy the index.

#ifndef CORE_COMMON_ROAD_DAY_H_
#define CORE_COMMON_ROAD_DAY_H_

#include <array>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "core_common/road_route.h"
#include "core_common/trail_state.h"

namespace core::road_day {

/// @brief THE DAY'S WALKING WORLD (econ's «one composition»): a leg's cost is its weight ÷ the
///        ground's factor, read by every query of the index - walking time and every reach
///        rule see one world.
struct DayWeights {
  const TrailTable* trails = nullptr;
  /// Per mode, per edge: the cost multiplier (1 ÷ the factor of the edge's ground). Empty for
  /// a mode the ground does not touch (every factor 1).
  std::array<std::vector<float>, kTravelModeCountValue> edge_cost;
  /// Per mode: the open ground's cost multiplier, and on an edge whose trail carries snow (a
  /// snow trail walks as wet ground, roads.md §3).
  std::array<float, kTravelModeCountValue> open_cost{1.0F, 1.0F, 1.0F, 1.0F};
  std::array<float, kTravelModeCountValue> snow_trail_cost{1.0F, 1.0F, 1.0F, 1.0F};
  /// Per mode with edge costs: its own node-to-node tables (Dijkstra under the costs).
  std::array<std::shared_ptr<const std::vector<float>>, kTravelModeCountValue> distance;
  std::array<std::shared_ptr<const std::vector<RoadEdgeIndex>>, kTravelModeCountValue> arrival;
  /// The beds' states and factors the tables were built for: a day with the same key reuses
  /// them (the recompute only on a day the beds change).
  std::uint64_t key = 0;
};

// -- The bare network's doors (road_route.cpp). `bare` must be IsBare(bare). ------------------

/// @brief True for the bare network index BuildRoadIndex(roads) makes.
bool IsBare(const RoadIndex& index);

/// @brief The day's weights on `bare`: the trails and today's beds; a mode the ground touches
///        gets its own node tables, taken from `previous` while its key matches.
std::shared_ptr<const DayWeights> MakeDay(const RoadIndex& bare,
                                          const TrailTable* trails,
                                          const RoadBeds& beds,
                                          const DayWeights* previous);

/// @brief RoadIndex's queries on `bare` under `day` (nullptr: the bare network's own).
float EffectiveKm(
    const RoadIndex& bare, TravelMode mode, Vec2 from, Vec2 to, const DayWeights* day);
float EffectiveKm(const RoadIndex& bare,
                  const NetworkPlace& from,
                  const NetworkPlace& to,
                  const DayWeights* day);
RouteMeasure Measure(
    const RoadIndex& bare, TravelMode mode, Vec2 from, Vec2 to, const DayWeights* day);
Route Way(const RoadIndex& bare, TravelMode mode, Vec2 from, Vec2 to, const DayWeights* day);

/// @brief The walker's door on `bare` under `day` (core::WalkRouteKm's body).
float WalkRouteKm(const RoadIndex& bare,
                  Vec2 from,
                  Vec2 to,
                  const DayWeights* day,
                  std::vector<std::pair<TrailEnd, TrailEnd>>* edges);

/// @brief The ground's state at a point on `bare` (core::GroundStateAt's body).
GroundState GroundAt(const RoadIndex& bare, Vec2 point, const RoadBeds& beds);

/// @brief How many day node tables this process has computed.
std::uint64_t TableBuilds();

}  // namespace core::road_day

#endif  // CORE_COMMON_ROAD_DAY_H_
