// The checks of the edge ledger (road_traffic.h; #2129's following rule).

#include "road_traffic_checks.h"

#include <cstdint>
#include <iostream>
#include <vector>

#include "core_common/road_hold.h"
#include "core_common/road_route.h"
#include "core_common/road_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "road_traffic.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

/// A straight road 2 km east along y = 0 of `surface`.
core::WorldState RoadWorld(core::RoadSurface surface) {
  core::WorldState world;
  core::RoadRow road;
  road.surface = surface;
  road.axis.push_back(core::RoadPoint{.position = {.x = 0.0F, .y = 0.0F}});
  road.axis.push_back(core::RoadPoint{.position = {.x = 2000.0F, .y = 0.0F}});
  core::AppendRow(world.roads, road);
  world.road_index = core::BuildRoadIndex(world.roads, world.trails, world.weather.road_beds);
  return world;
}

/// What one pair of carts did on the road: cart 0 slow (2 game hours a km) leaving at 0, cart
/// 1 fast (1 a km) leaving `behind_seconds` later, the same way or (`oncoming`) the other.
struct Pair {
  std::vector<core::RoadHold> holds;
  std::int32_t delayed_seconds = 0;
};

Pair Run(core::RoadSurface surface, std::int32_t behind_seconds, bool oncoming) {
  const core::WorldState world = RoadWorld(surface);
  core::RoadTraffic traffic(world, {}, core::RoadTrafficRules{});
  const core::Vec2 west{.x = 0.0F, .y = 0.0F};
  const core::Vec2 east{.x = 2000.0F, .y = 0.0F};
  traffic.BeginLeg(0, 0, core::TravelMode::kCart, 2.0F, west, east, 0);
  traffic.BeginLeg(1,
                   0,
                   core::TravelMode::kCart,
                   1.0F,
                   oncoming ? east : west,
                   oncoming ? west : east,
                   behind_seconds);
  Pair pair;
  std::vector<core::LegDelay> delays;
  traffic.AdvanceTo(24 * 3600, pair.holds, delays);
  for (const core::LegDelay& delay : delays) {
    pair.delayed_seconds += delay.cart == 1 ? delay.seconds : 0;
  }
  return pair;
}

}  // namespace

int CheckTheEdgeLedger() {
  int failures = 0;
  // ON DIRT the fast cart, leaving 10 minutes after the slow one the same way, cannot leave the
  // road before it: the slow one is out at 4 h, the fast one would be at 2 h 10 min.
  const Pair dirt = Run(core::RoadSurface::kDirt, 600, false);
  failures +=
      Expect(!dirt.holds.empty() && dirt.holds[0].cart == 1 && dirt.holds[0].behind_cart == 0 &&
                 dirt.holds[0].cause == core::RoadHoldCause::kFollowing,
             "edge ledger: on dirt the fast cart is held behind the slow one");
  failures += Expect(dirt.delayed_seconds >= 6000,
                     "edge ledger: the fast cart's leg is lengthened by the wait (>= 100 min)");
  // ON ASPHALT it overtakes: no hold.
  const Pair asphalt = Run(core::RoadSurface::kAsphalt, 600, false);
  failures += Expect(asphalt.holds.empty() && asphalt.delayed_seconds == 0,
                     "edge ledger: on asphalt the fast cart overtakes, no hold");
  // ONCOMING carts keep their lanes: no hold.
  const Pair oncoming = Run(core::RoadSurface::kDirt, 600, true);
  failures += Expect(oncoming.holds.empty() && oncoming.delayed_seconds == 0,
                     "edge ledger: an oncoming cart is in its own lane, no hold");
  // THE SLOW ONE BEHIND THE FAST ONE: nobody waits.
  {
    const core::WorldState world = RoadWorld(core::RoadSurface::kDirt);
    core::RoadTraffic traffic(world, {}, core::RoadTrafficRules{});
    traffic.BeginLeg(0, 0, core::TravelMode::kCart, 1.0F, {0.0F, 0.0F}, {2000.0F, 0.0F}, 0);
    traffic.BeginLeg(1, 0, core::TravelMode::kCart, 2.0F, {0.0F, 0.0F}, {2000.0F, 0.0F}, 600);
    std::vector<core::RoadHold> holds;
    std::vector<core::LegDelay> delays;
    traffic.AdvanceTo(24 * 3600, holds, delays);
    failures += Expect(holds.empty() && delays.empty() && traffic.Crossings() >= 2,
                       "edge ledger: a slow cart behind a fast one waits for nobody");
  }
  // THE SWITCH OFF: nothing registered, nothing held.
  {
    const core::WorldState world = RoadWorld(core::RoadSurface::kDirt);
    core::RoadTrafficRules off;
    off.following_on = false;
    core::RoadTraffic traffic(world, {}, off);
    traffic.BeginLeg(0, 0, core::TravelMode::kCart, 2.0F, {0.0F, 0.0F}, {2000.0F, 0.0F}, 0);
    traffic.BeginLeg(1, 0, core::TravelMode::kCart, 1.0F, {0.0F, 0.0F}, {2000.0F, 0.0F}, 600);
    std::vector<core::RoadHold> holds;
    std::vector<core::LegDelay> delays;
    traffic.AdvanceTo(24 * 3600, holds, delays);
    failures += Expect(holds.empty() && delays.empty() && traffic.Crossings() == 0,
                       "edge ledger: switched off it crosses and holds nothing");
  }
  return failures;
}
