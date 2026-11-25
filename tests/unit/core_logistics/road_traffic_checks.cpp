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

/// A straight road 2 km east along y = 0 of `surface`; with `bridge` a bridge deck mark at its
/// middle vertex (the whole edge a no-pass stretch).
core::WorldState RoadWorld(core::RoadSurface surface, bool bridge = false) {
  core::WorldState world;
  core::RoadRow road;
  road.surface = surface;
  road.axis.push_back(core::RoadPoint{.position = {.x = 0.0F, .y = 0.0F}});
  if (bridge) {
    road.axis.push_back(
        core::RoadPoint{.position = {.x = 1000.0F, .y = 0.0F}, .mark = core::RoadMark::kBridge});
  }
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

Pair Run(core::RoadSurface surface,
         std::int32_t behind_seconds,
         bool oncoming,
         bool bridge = false,
         bool third_oncoming = false) {
  const core::WorldState world = RoadWorld(surface, bridge);
  core::RoadTraffic traffic(world, {}, core::RoadTrafficRules{});
  const core::Vec2 west{.x = 0.0F, .y = 0.0F};
  const core::Vec2 east{.x = 2000.0F, .y = 0.0F};
  traffic.BeginLeg(0, 0, core::TravelMode::kCart, 2.0F, west, east, 0);
  if (third_oncoming) {
    // Cart 2 comes the other way the whole morning: the oncoming lane is never free.
    traffic.BeginLeg(2, 0, core::TravelMode::kCart, 3.0F, east, west, 0);
  }
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
  // ON OPEN DIRT the fast cart, leaving 10 minutes after the slow one the same way, catches it
  // and OVERTAKES after the rule's 3 minutes behind it, the oncoming lane being free (Alex
  // «Разрешаем обгон», #2443; until then it waited the whole way, >= 100 min - that check
  // moved to the bridge below).
  const Pair dirt = Run(core::RoadSurface::kDirt, 600, false);
  failures +=
      Expect(!dirt.holds.empty() && dirt.holds[0].cart == 1 && dirt.holds[0].behind_cart == 0 &&
                 dirt.holds[0].cause == core::RoadHoldCause::kFollowing,
             "edge ledger: on dirt the fast cart is held behind the slow one");
  // It catches the slow one at 1 200 s and passes 3 minutes later; behind him at half its pace
  // for those 180 s it LOSES 90 (the delay is the time lost, not the time behind).
  failures += Expect(dirt.delayed_seconds >= 85 && dirt.delayed_seconds <= 95,
                     "edge ledger: on open dirt it overtakes after 3 minutes behind (90 s lost)");
  // ON A BRIDGE nobody overtakes: the fast cart waits the slow one out (4 h against 2 h 10 min).
  const Pair bridge = Run(core::RoadSurface::kDirt, 600, false, /*bridge=*/true);
  failures += Expect(bridge.delayed_seconds >= 6000,
                     "edge ledger: on a bridge the fast cart waits the whole way (>= 100 min)");
  // AN ONCOMING CART FAR OFF does not bar the pass (cart 2 enters the far end as the pair sets
  // out: it is 1.8 km away when the fast cart tries).
  const Pair far = Run(core::RoadSurface::kDirt, 600, false, false, /*third_oncoming=*/true);
  failures += Expect(far.delayed_seconds >= 85 && far.delayed_seconds <= 95,
                     "edge ledger: an oncoming cart far off does not bar the pass");
  // AN ONCOMING CART AT THE PLACE OF THE PASS makes it wait for the gap: the pair sets out at
  // 10 000 s, the fast cart tries at 11 380 s some 190 m in, and cart 2 (1 h a km from the far
  // end since 4 868 s) is there; it passes once cart 2 is 150 m behind them.
  {
    const core::WorldState world = RoadWorld(core::RoadSurface::kDirt);
    core::RoadTraffic traffic(world, {}, core::RoadTrafficRules{});
    const core::Vec2 west{.x = 0.0F, .y = 0.0F};
    const core::Vec2 east{.x = 2000.0F, .y = 0.0F};
    traffic.BeginLeg(2, 0, core::TravelMode::kCart, 1.0F, east, west, 4868);
    traffic.BeginLeg(0, 0, core::TravelMode::kCart, 2.0F, west, east, 10000);
    traffic.BeginLeg(1, 0, core::TravelMode::kCart, 1.0F, west, east, 10600);
    std::vector<core::RoadHold> holds;
    std::vector<core::LegDelay> delays;
    traffic.AdvanceTo(24 * 3600, holds, delays);
    std::int32_t lost = 0;
    for (const core::LegDelay& delay : delays) {
      lost += delay.cart == 1 ? delay.seconds : 0;
    }
    failures += Expect(lost > 95 && lost < 1200,
                       "edge ledger: an oncoming cart at the pass makes it wait for the gap");
  }
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
  // ONLY ON THE STRETCH BOTH TRAVEL (the PAIR2129 probe: 90 % of the held time between carts of
  // one pace). Cart 1 joins the edge at 1.5 km at minute 0 and goes to its end; cart 0 drives
  // the whole edge at the same pace from minute 0 - cart 1 is ahead where they meet, and
  // nobody waits. The first ledger held it, comparing its exit to cart 0's from another point.
  {
    const core::WorldState world = RoadWorld(core::RoadSurface::kDirt);
    core::RoadTraffic traffic(world, {}, core::RoadTrafficRules{});
    traffic.BeginLeg(0, 0, core::TravelMode::kCart, 1.0F, {0.0F, 0.0F}, {2000.0F, 0.0F}, 0);
    traffic.BeginLeg(1, 0, core::TravelMode::kCart, 1.0F, {1500.0F, 0.0F}, {2000.0F, 0.0F}, 0);
    std::vector<core::RoadHold> holds;
    std::vector<core::LegDelay> delays;
    traffic.AdvanceTo(24 * 3600, holds, delays);
    failures += Expect(holds.empty() && delays.empty(),
                       "edge ledger: a cart joining mid-edge ahead of the one on it is not held");
  }
  // AND ONE PACE BEHIND ONE PACE never closes up: nobody waits.
  {
    const core::WorldState world = RoadWorld(core::RoadSurface::kDirt);
    core::RoadTraffic traffic(world, {}, core::RoadTrafficRules{});
    traffic.BeginLeg(0, 0, core::TravelMode::kCart, 1.5F, {0.0F, 0.0F}, {2000.0F, 0.0F}, 0);
    traffic.BeginLeg(1, 0, core::TravelMode::kCart, 1.5F, {0.0F, 0.0F}, {2000.0F, 0.0F}, 120);
    std::vector<core::RoadHold> holds;
    std::vector<core::LegDelay> delays;
    traffic.AdvanceTo(24 * 3600, holds, delays);
    failures += Expect(holds.empty() && delays.empty(),
                       "edge ledger: a cart behind one of its own pace waits for nobody");
  }
  // A RE-PLAN CARRIES THE DAY'S HOLDS (r3on: every evening plan empty). The earlier plan:
  // cart 0 (driver 10) held on its leg 1 at 300 min behind cart 1; cart 1 (driver 11) held on
  // leg 0 at 200 and on leg 2 at 500. At 420 min the re-plan keeps driver 11 alone, now cart 0,
  // with his first two legs.
  {
    core::GroomPlan earlier;
    earlier.carts.resize(2);
    earlier.carts[0].driver = core::ResidentId{10};
    earlier.carts[1].driver = core::ResidentId{11};
    const auto hold = [](std::uint32_t cart,
                         std::uint32_t leg,
                         std::uint16_t from,
                         std::uint32_t driver,
                         std::uint32_t behind) {
      return core::RoadHold{.cart = cart,
                            .leg = leg,
                            .driver = core::ResidentId{driver},
                            .cause = core::RoadHoldCause::kFollowing,
                            .road = core::RoadId{1},
                            .chainage_m = 0.0F,
                            .from_minute = from,
                            .to_minute = static_cast<std::uint16_t>(from + 10),
                            .behind_cart = behind,
                            .rank = 1};
    };
    earlier.holds = {
        hold(0, 1, 300, 10, 1), hold(1, 0, 200, 11, core::kNoCartAhead), hold(1, 2, 500, 11, 0)};
    core::GroomPlan plan;
    plan.carts.resize(1);
    plan.carts[0].driver = core::ResidentId{11};
    plan.holds = {hold(0, 3, 600, 11, core::kNoCartAhead)};  // the re-plan's own
    core::RoadTraffic::CarryHolds(earlier, {2}, 420, plan);
    const bool shape = plan.holds.size() == 3;
    failures += Expect(shape && plan.holds[0].cart == core::kHoldOffThePlan &&
                           plan.holds[0].leg == core::kHoldOffThePlan &&
                           plan.holds[0].driver.value == 10 && plan.holds[0].behind_cart == 0,
                       "re-plan: a dropped cart's begun hold stays, off the plan, behind the "
                       "cart ahead's new number");
    failures += Expect(shape && plan.holds[1].cart == 0 && plan.holds[1].leg == 0 &&
                           plan.holds[1].driver.value == 11,
                       "re-plan: a kept leg's hold moves to its driver's new cart");
    failures += Expect(shape && plan.holds[2].leg == 3 && plan.holds[2].from_minute == 600,
                       "re-plan: a hold on a leg drawn anew is the new plan's, the old one gone");
  }
  return failures;
}
