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
  /// @brief Once a day, after the day's step: the day's plan as it ended.
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
    std::uint32_t carts = 0;
    for (const core::CartPlan& cart : plan.carts) {
      carts += cart.on_foot ? 0U : 1U;
    }
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
      held_carts.insert(hold.cart);
    }
    row.held_cart_days += static_cast<std::uint32_t>(held_carts.size());
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
          << " by_cause(events/cart_hours)";
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
  struct Year {
    std::uint32_t plan_days = 0;
    std::uint32_t cart_days = 0;
    std::uint32_t held_cart_days = 0;
    std::uint32_t worst_minutes = 0;
    std::array<std::uint32_t, core::kRoadHoldCauseCountValue> events{};
    std::array<std::uint64_t, core::kRoadHoldCauseCountValue> minutes{};
  };

  std::map<std::uint32_t, Year> by_year_;
  bool counted_any_ = false;
  core::SimDay last_counted_day_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_ROAD_HOLD_TALLY_H_
