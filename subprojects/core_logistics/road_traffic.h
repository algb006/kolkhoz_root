/// @file
/// @brief THE EDGE LEDGER (#2129, model B of the design, boss [2]): the groom plan's vehicles
///        cross the road's edges in global time order, and each directed edge remembers the
///        exit time of its last entrant - so on dirt and gravel nobody leaves an edge before the
///        one ahead of him (roads.md «Обгон только на асфальте»; on asphalt no clamp). No pair
///        of vehicles is ever enumerated: one compare per crossing, T crossings a plan build,
///        O(T log V) for the time-ordered merge of V vehicles' streams (a vehicle starting later
///        can reach a far edge earlier from a side road). T is linear in the plan's vehicles
///        and independent of the residents and the stretches. Private to core_logistics.
/// @threading SINGLE_THREADED
/// Runs inside the groom plan's clock (logistics_plan.cpp RunTheClock), phase 3, sequential.
///
/// TIME IN WHOLE GAME SECONDS: the clock's own minutes are floats whose sums round by the
/// compiler; the ledger keeps integers so 1 and N workers, and two compilers, agree.
/// THE NULL CASE IS EXACT: the ledger only ever ADDS a delay, and only from hold_min_seconds
/// on; with no slower vehicle ahead nothing changes. THE FIRST SLICE: following only; a
/// standing obstacle (the pig) is off (boss [2] q1) and RoadObstaclesToday is empty.
/// APPROXIMATIONS, NAMED: a leg's off-road parts and its loading pauses carry no hold; a leg's
/// edges are crossed at its own free pace from its start, so its edge times are the ledger's,
/// not the clock's float sum; only a leg still on its way takes a delay.

#ifndef CORE_LOGISTICS_ROAD_TRAFFIC_H_
#define CORE_LOGISTICS_ROAD_TRAFFIC_H_

#include <array>
#include <cstdint>
#include <memory>
#include <queue>
#include <span>
#include <unordered_map>
#include <vector>

#include "core_common/geometry.h"
#include "core_common/road_hold.h"
#include "core_common/road_route.h"
#include "core_common/world_state.h"

namespace core {

/// @brief The ledger's numbers (world_params `road_*`).
struct RoadTrafficRules {
  /// The ledger on (world_params `road_following`, 1) or off (0: the plan as before 231).
  bool following_on = true;
  /// A delay below this is no hold (the null case's guard), game seconds.
  std::int32_t hold_min_seconds = 60;
  /// 1: standing obstacles are read (RoadObstaclesToday); off in the first slice (boss [2] q1).
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
  ///        index's Way for `mode` from `from` to `to`, crossed at `hours_per_km` game hours a
  ///        kilometre (the clock's own pace for the leg). A `replay` leg (one a re-plan kept,
  ///        begun before it) holds the edges for those behind it and waits behind those
  ///        ahead, but yields no hold and no delay: both are the earlier plan's.
  void BeginLeg(std::uint32_t cart,
                std::uint32_t leg,
                TravelMode mode,
                float hours_per_km,
                Vec2 from,
                Vec2 to,
                std::int32_t start_second,
                bool replay = false);

  /// @brief A re-plan's holds (#2129): the earlier plan's holds on the legs the re-plan
  ///        kept (each cart's first `kept_legs[cart]` legs, matched by driver) go into `plan`
  ///        re-addressed; those of a driver with no cart left in `plan` are kept with cart and
  ///        leg kHoldOffThePlan - they happened, and the day's record (pay, prints) is by
  ///        driver. A hold behind a cart no longer in the plan names kHoldOffThePlan behind.
  static void CarryHolds(const GroomPlan& earlier,
                         const std::vector<std::uint32_t>& kept_legs,
                         std::uint16_t now_minute,
                         GroomPlan& plan);

  /// @brief Processes every edge entry up to `now_second` in (time, creation sequence) order;
  ///        appends the holds it found and the delays the clock must add to the legs.
  void AdvanceTo(std::int32_t now_second,
                 std::vector<RoadHold>& holds,
                 std::vector<LegDelay>& delays);

  /// @brief The edge crossings processed so far (the CPU instrument's count).
  std::uint64_t Crossings() const { return crossings_; }

 private:
  /// One piece of a leg: an edge crossed in one direction, or open ground (edge kNoRoadEdge).
  struct Piece {
    RoadEdgeIndex edge = kNoRoadEdge;
    bool forward = true;
    bool clamps = false;  ///< dirt or gravel: no overtaking
    std::int32_t free_seconds = 0;
    RoadId road;
    float chainage_m = 0.0F;     ///< where the piece joins the edge, along the road's axis
    float to_chainage_m = 0.0F;  ///< where it leaves it
  };

  /// One vehicle's passage of a directed edge: where on it and when, the position counted in
  /// the direction of travel (so «ahead» is the larger number either way).
  struct Passage {
    std::uint32_t cart = 0;
    float from_m = 0.0F;
    float to_m = 0.0F;
    std::int32_t in_second = 0;
    std::int32_t out_second = 0;

    /// The game second it is at position `at` (from_m <= at <= to_m), at its own steady pace.
    double SecondAt(float at) const {
      const double span = static_cast<double>(to_m) - static_cast<double>(from_m);
      const double into = static_cast<double>(at) - static_cast<double>(from_m);
      const double took = static_cast<double>(out_second) - static_cast<double>(in_second);
      return static_cast<double>(in_second) + (into / span * took);
    }
  };

  /// THE RECENT PASSAGES OF AN EDGE (the four latest entrants): a cart joining the road
  /// mid-edge, or leaving it there, shares only part of it with the one ahead - the first
  /// ledger compared their exits from different points and held same-pace carts that never
  /// met (the PAIR2129 probe: 90 % of the held time between carts of one pace, 75 % on pieces
  /// of no length). Four, not one, so a short passage does not hide a slow one from those
  /// behind it.
  static constexpr std::size_t kRecentPassages = 4;

  struct Recent {
    std::array<Passage, kRecentPassages> passages{};
    std::size_t count = 0;
    std::size_t next = 0;
  };

  struct Stream {
    std::uint32_t cart = 0;
    std::uint32_t leg = 0;
    bool replay = false;
    std::vector<Piece> pieces;
    std::size_t next = 0;
  };

  struct Entry {
    std::int32_t second = 0;
    std::uint64_t sequence = 0;
    std::uint32_t stream = 0;

    bool operator>(const Entry& other) const {
      return second != other.second ? second > other.second : sequence > other.sequence;
    }
  };

  void Push(std::uint32_t stream, std::int32_t second);

  RoadTrafficRules rules_;
  std::shared_ptr<const RoadIndex> index_;
  const RoadTable* roads_ = nullptr;
  std::vector<Stream> streams_;
  std::priority_queue<Entry, std::vector<Entry>, std::greater<>> queue_;
  std::unordered_map<std::uint64_t, Recent> recent_;  ///< by edge x 2 + direction
  std::uint64_t sequence_ = 0;
  std::uint64_t crossings_ = 0;
};

}  // namespace core

#endif  // CORE_LOGISTICS_ROAD_TRAFFIC_H_
