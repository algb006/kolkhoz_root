/// @file
/// @brief THE EDGE LEDGER (#2129, model B of the design, boss [2]): the groom plan's vehicles
///        cross the road's edges in global time order, and each directed edge remembers the
///        exit time of its last entrant - so on dirt and gravel nobody leaves an edge before the
///        one ahead of him (roads.md «Обгон только на асфальте»; on asphalt no clamp), and a
///        standing obstacle releases its queue in arrival order. No pair of vehicles is ever
///        enumerated: one compare per crossing, T crossings a plan build, O(T log V) for the
///        time-ordered merge of V vehicles' streams (a vehicle starting later can reach a far
///        edge earlier from a side road). T is linear in the plan's vehicles and independent of
///        the residents and the stretches. CONTRACT ONLY (core §12а): bodies in the next task.
///        Private to core_logistics.
/// @threading SINGLE_THREADED
/// Runs inside the groom plan's clock (logistics_plan.cpp RunTheClock), phase 3, sequential.
///
/// TIME IN WHOLE GAME SECONDS: the clock's own minutes are floats whose sums round by the
/// compiler; the ledger keeps integers so 1 and N workers, and two compilers, agree.
/// THE NULL CASE IS EXACT: the ledger only ever ADDS a delay, and only above
/// road_hold_min_seconds; with no obstacle and no speed mix nothing changes.

#ifndef CORE_LOGISTICS_ROAD_TRAFFIC_H_
#define CORE_LOGISTICS_ROAD_TRAFFIC_H_

#include <cstdint>
#include <span>
#include <vector>

#include "core_common/geometry.h"
#include "core_common/road_hold.h"
#include "core_common/road_route.h"
#include "core_common/world_state.h"

namespace core {

/// @brief The ledger's numbers (world_params `road_*`; STUB values until the implementation).
struct RoadTrafficRules {
  /// A delay below this is no hold (the null case's guard), game seconds.
  std::int32_t hold_min_seconds = 60;
  /// 1: standing obstacles are read (RoadObstaclesToday); 0 in the first slice (boss [2] q1).
  bool obstacles_on = false;
};

/// @brief A leg's delay the clock adds to its remaining time.
struct LegDelay {
  std::uint32_t cart = 0;
  std::uint32_t leg = 0;
  std::int32_t seconds = 0;
};

/// @brief The day's edge ledger: built per plan build, fed each leg as it is made.
class RoadTraffic {
 public:
  /// @brief A ledger over `world`'s road index, with today's `obstacles`.
  RoadTraffic(const WorldState& world,
              std::span<const RoadObstacle> obstacles,
              const RoadTrafficRules& rules);

  /// @brief Registers a leg beginning at `start_second` of the day: its edges are the
  ///        index's Way for `mode` from `from` to `to`, crossed at the free pace `km_per_hour`.
  void BeginLeg(std::uint32_t cart,
                std::uint32_t leg,
                TravelMode mode,
                float km_per_hour,
                Vec2 from,
                Vec2 to,
                std::int32_t start_second);

  /// @brief Processes every edge entry up to `now_second` in (time, creation sequence) order;
  ///        appends the holds it found and the delays the clock must add to the legs.
  void AdvanceTo(std::int32_t now_second,
                 std::vector<RoadHold>& holds,
                 std::vector<LegDelay>& delays);

  /// @brief The edge crossings registered so far (the CPU instrument's count).
  std::uint64_t Crossings() const;
};

}  // namespace core

#endif  // CORE_LOGISTICS_ROAD_TRAFFIC_H_
