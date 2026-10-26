/// @file
/// @brief The run's chairman sets each kolkhoz herd's plan once a year, the
///        evening the meadows' cut is over (#1289; livestock.md «План стада»,
///        «Бот прогонов»; econ's herd-plan-against-the-meadows-2026-10-13).
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// THE RULE (the row, STUB econ): N = «сена на», not above the places and
/// not below the floor - the hay goes first to both floors, then to the
/// horses up to their places, the rest to the cows. As two evenings: the
/// horses on the hay with the others at their floor (a what-if of the door,
/// no plan set for it); the others on what the horses leave. Once a year. The bot reads the
/// office's own door (ISimulation::HerdPlans), as the player reads the window.
///
/// THE CUT IS OVER on the first day of the month after farming.csv
/// `meadow_cut_month_end` (the core keeps no «cut closed» flag; boss's
/// row reads «вечером дня, когда покос закрыт»).
#ifndef TESTS_RUN_COMMON_HERD_PLAN_POLICY_H_
#define TESTS_RUN_COMMON_HERD_PLAN_POLICY_H_

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/herd_plan_view.h"
#include "core_common/herd_state.h"
#include "core_common/order_state.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "core_tables/tables.h"

namespace run {

/// @brief Sets the herds' plans the evening the cut is over.
class HerdPlanPolicy {
 public:
  explicit HerdPlanPolicy(const core::ITableSet& tables) {
    if (const core::ITable* const farming = tables.FindTable("farming")) {
      const std::uint32_t row = farming->FindRowByKey("meadow_cut_month_end");
      const std::uint32_t column = farming->FindColumn("value");
      if (row != core::kNoTableRow && column != core::kNoTableColumn) {
        cut_end_month_ = static_cast<std::uint32_t>(farming->CellReal(row, column).value_or(7.0F));
      }
    }
    if (const core::ITable* const livestock = tables.FindTable("livestock")) {
      horse_ = livestock->FindRowByKey("horse");
    }
  }

  /// @brief The control arm: the rule does nothing (no plan is ever set).
  void Enable(bool on) { on_ = on; }

  /// @brief One day of the chairman's attention. Call once a day.
  void RunDay(core::ISimulation& simulation) {
    if (!on_) {
      return;
    }
    const core::WorldState& world = simulation.CompletedState();
    const core::SimDay day = world.calendar.day;
    const std::uint32_t year = day / core::kDaysPerYear;
    // The month after the cut's last (meadow_cut_month_end is 1-based).
    const bool after_cut = static_cast<std::uint32_t>(world.calendar.date.month) == cut_end_month_;
    if (!after_cut || year == set_year_) {
      return;
    }
    // TWO EVENINGS IN THE ORDER OF THE HAY (econ, core-boss-1289 [10] (2):
    // «the hay to the floors of both herds first, then to the horses up to
    // their places, the rest to the cows»): the horses on «сена на» with the
    // others at their floor (HerdPlanLine::hay_for_others_at_floor, a what-if
    // of the door); the others on what the horses leave. Its first form sized
    // the horses against the cows at their places (spring «no horse» +2-3
    // SE); its second set the cows' plan to their floor for a day - the hay's
    // answer then handed the cows down to it (milk -16…-18 %, the second pair).
    const std::uint8_t stage = horses_year_ != year ? 1 : 2;
    std::vector<core::OrderRow> orders;
    for (const core::HerdPlanLine& line : simulation.HerdPlans()) {
      const bool horse = line.kind.value == horse_;
      if (horse != (stage == 1) || !line.hay_for_known) {
        continue;
      }
      // «Сена на» - not above the places, not below the floor.
      const std::uint16_t hay = stage == 1 ? line.hay_for_others_at_floor : line.hay_for;
      const std::uint16_t n =
          std::min<std::uint16_t>(line.places, std::max<std::uint16_t>(hay, line.floor_heads));
      if (line.plan_set && line.plan == n) {
        continue;
      }
      core::OrderRow order;
      order.kind = core::OrderKind::kSetHerdPlan;
      order.herd = line.herd;
      order.amount = n;
      orders.push_back(order);
      ++set_this_year_;
    }
    if (!orders.empty()) {
      simulation.StageOrders(orders, {});
    }
    if (stage == 1) {
      horses_year_ = year;
    } else {
      set_year_ = year;
      std::printf("HP,y=%u,plans_set=%u\n", year + 1U, set_this_year_);
      set_this_year_ = 0;
    }
  }

 private:
  static constexpr std::uint32_t kNever = 0xFFFFFFFFU;
  std::uint32_t cut_end_month_ = 7;
  std::uint32_t horse_ = kNever;
  std::uint32_t horses_year_ = kNever;
  std::uint32_t set_year_ = kNever;
  std::uint32_t set_this_year_ = 0;
  bool on_ = true;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_HERD_PLAN_POLICY_H_
