/// @file
/// @brief HOLD2129 (#2129; boss's gates in core-boss-2129-traffic-design-2026-11-25 [2]): the
///        road's lost time by cause, per village-year - the holds of each day's last groom plan
///        (GroomPlan::holds, the edge ledger's), their count, cart-hours, the worst single hold,
///        and the plan's carts held at least once a day. NOT MEASURED when the year had no plan
///        with a cart. Named gaps, as the slice leaves them: teams, horsemen, brigade carts and
///        mowers are not in the plan; a dock queue on the road is the layout's; a people's
///        cart's hold is drawn and not charged to its riders.
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.

#ifndef TESTS_RUN_COMMON_ROAD_HOLD_TALLY_H_
#define TESTS_RUN_COMMON_ROAD_HOLD_TALLY_H_

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <ostream>
#include <set>

#include "core_common/calendar.h"
#include "core_common/road_hold.h"
#include "core_common/world_state.h"

namespace run {

class RoadHoldTally {
 public:
  /// @brief After every step: the carts of the plan as it stands - a re-plan drops a cart
  ///        whose work ended, so the evening's plan alone under-counts the day's carts (r3on:
  ///        every evening plan empty, 816 of 816 lines NOT MEASURED).
  void CountStep(const core::WorldState& world) {
    const core::GroomPlan& plan = world.groom_plan;
    std::map<std::uint32_t, CartDay>& drivers = drivers_by_day_[plan.day];
    for (const core::CartPlan& cart : plan.carts) {
      if (cart.on_foot) {
        continue;
      }
      CartDay day;
      for (const core::CartLeg& leg : cart.legs) {
        day.loaded_legs += leg.loaded != 0 ? 1U : 0U;
        // ON THE ROAD: a leg that goes somewhere (a load carted standing is not on the road).
        const bool moves = leg.from.x != leg.to.x || leg.from.y != leg.to.y;
        if (moves && leg.arrive_minute > leg.depart_minute) {
          day.road_minutes += static_cast<std::uint32_t>(leg.arrive_minute - leg.depart_minute);
        }
      }
      // A re-plan keeps the begun legs and draws the rest anew: the day's is the plan's that
      // gave the most (an upper bound when a later plan shortened the day).
      CartDay& most = drivers[cart.driver.value];
      most.loaded_legs = std::max(most.loaded_legs, day.loaded_legs);
      most.road_minutes = std::max(most.road_minutes, day.road_minutes);
    }
  }

  /// @brief Once a day, after the day's step: the day's holds as the last plan carries them
  ///        (a re-plan carries the day's earlier holds, road_traffic.h CarryHolds).
  void CountDay(const core::WorldState& world) {
    // EACH DAY'S PLAN ONCE, BY ITS OWN DAY: at the daily look the calendar has turned to the
    // next day, so the plan in the world is the day just ended's (the first read asked
    // plan.day == calendar.day and printed NOT MEASURED for every year). Its year is its day's.
    by_year_[(world.calendar.day / core::kDaysPerYear) + 1U];  // every year a line
    const core::GroomPlan& plan = world.groom_plan;
    if (counted_any_ && plan.day == last_counted_day_) {
      return;
    }
    counted_any_ = true;
    last_counted_day_ = plan.day;
    Year& row = by_year_[(plan.day / core::kDaysPerYear) + 1U];
    std::map<std::uint32_t, CartDay> drivers = std::move(drivers_by_day_[plan.day]);
    drivers_by_day_.erase(drivers_by_day_.begin(), drivers_by_day_.upper_bound(plan.day));
    for (const core::RoadHold& hold : plan.holds) {
      drivers.emplace(hold.driver.value, CartDay{});
    }
    const auto carts = static_cast<std::uint32_t>(drivers.size());
    row.cart_days += carts;
    row.plan_days += carts > 0 ? 1U : 0U;
    std::set<std::uint32_t> held_carts;
    for (const core::RoadHold& hold : plan.holds) {
      const auto cause = static_cast<std::size_t>(hold.cause);
      if (cause >= row.events.size()) {
        continue;
      }
      const std::uint32_t minutes = hold.to_minute - hold.from_minute;
      ++row.events[cause];
      row.minutes[cause] += minutes;
      row.worst_minutes = std::max(row.worst_minutes, minutes);
      held_carts.insert(hold.driver.value);
    }
    row.held_cart_days += static_cast<std::uint32_t>(held_carts.size());
    // econ's print (boss [2]): the loads carted on a held carter-day against an unheld one.
    for (const auto& [driver, day] : drivers) {
      const bool held = held_carts.contains(driver);
      (held ? row.loaded_legs_held : row.loaded_legs_unheld) += day.loaded_legs;
      row.road_minutes += day.road_minutes;
    }
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    for (const auto& [year, row] : by_year_) {
      out << "population_curve: HOLD2129 seed " << seed << " year " << year
          << " ground=last_groom_plan_of_the_day;plan_vehicles_only(gaps:teams,horsemen,"
             "brigade_carts,mowers,dock_queues,peoples_cart_riders)";
      if (row.plan_days == 0) {
        out << " NOT MEASURED (no plan with a cart)\n";
        continue;
      }
      out << " plan_days " << row.plan_days << " cart_days " << row.cart_days << " held_cart_days "
          << row.held_cart_days << " worst_hold_min " << row.worst_minutes
          << " loaded_legs_a_cart_day(held/unheld;proxy_for_norm_days:the_leg_not_its_price) "
          << Ratio(row.loaded_legs_held, row.held_cart_days) << '/'
          << Ratio(row.loaded_legs_unheld, row.cart_days - row.held_cart_days)
          << " road_cart_hours(moving_legs,depart_to_arrive,most_of_the_day's_plans) "
          << static_cast<double>(row.road_minutes) / 60.0 << " by_cause(events/cart_hours)";
      static constexpr std::array<const char*, core::kRoadHoldCauseCountValue> kNames = {
          "following", "pig_in_puddle", "herd_passing"};
      for (std::size_t cause = 0; cause < kNames.size(); ++cause) {
        out << ' ' << kNames[cause] << ' ' << row.events[cause] << '/'
            << static_cast<double>(row.minutes[cause]) / 60.0;
      }
      out << '\n';
    }
  }

 private:
  static double Ratio(std::uint64_t part, std::uint64_t whole) {
    return whole == 0 ? 0.0 : static_cast<double>(part) / static_cast<double>(whole);
  }

  struct CartDay {
    std::uint32_t loaded_legs = 0;
    std::uint32_t road_minutes = 0;
  };

  struct Year {
    std::uint64_t road_minutes = 0;
    std::uint64_t loaded_legs_held = 0;
    std::uint64_t loaded_legs_unheld = 0;
    std::uint32_t plan_days = 0;
    std::uint32_t cart_days = 0;
    std::uint32_t held_cart_days = 0;
    std::uint32_t worst_minutes = 0;
    std::array<std::uint32_t, core::kRoadHoldCauseCountValue> events{};
    std::array<std::uint64_t, core::kRoadHoldCauseCountValue> minutes{};
  };

  std::map<std::uint32_t, Year> by_year_;
  /// By day, each plan cart's driver and the most loaded legs any plan of the day gave him.
  std::map<core::SimDay, std::map<std::uint32_t, CartDay>> drivers_by_day_;
  bool counted_any_ = false;
  core::SimDay last_counted_day_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_ROAD_HOLD_TALLY_H_
