/// @file
/// @brief The run chairman's «Стол для района» rule (boss, 22 Nov 2026; N = 2 weeks for both
///        bots, host's novice and this canon chairman): receive the district plainly while the
///        free stock over the funds is under two weeks of the village's need, with a table
///        otherwise. Reads the core's one number (ISimulation::FreeStockToday) and only compares.
///        Acts only for an avatar with the path; the core refuses the order otherwise.
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.

#ifndef TESTS_RUN_COMMON_DISTRICT_TABLE_POLICY_H_
#define TESTS_RUN_COMMON_DISTRICT_TABLE_POLICY_H_

#include <array>
#include <cstdint>
#include <map>
#include <ostream>
#include <span>
#include <string_view>

#include "core_common/calendar.h"
#include "core_common/district_visit_state.h"
#include "core_common/free_stock_view.h"
#include "core_common/order_state.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "core_tables/tables.h"

namespace run {

class DistrictTablePolicy {
 public:
  /// The two weeks, in days (4 days a month: half a game month is 2 days; the design's «2
  /// weeks» is 14 days of need, counted in the need's own days).
  static constexpr float kPlainBelowDays = 14.0F;

  /// @brief On for the run's avatar only if avatars.csv gives it the path
  ///        (district_table_days > 0); a chairman without it would be refused every day.
  void SetAvatar(const core::ITableSet& tables, std::string_view avatar_key) {
    enabled_ = false;
    const core::ITable* const avatars = tables.FindTable("avatars");
    if (avatars == nullptr || avatar_key.empty()) {
      return;
    }
    const std::uint32_t row = avatars->FindRowByKey(avatar_key);
    const std::uint32_t days = avatars->FindColumn("district_table_days");
    if (row != core::kNoTableRow && days != core::kNoTableColumn) {
      enabled_ = avatars->CellReal(row, days).value_or(0.0F) > 0.0F;
    }
  }

  void RunDay(core::ISimulation& simulation) {
    if (!enabled_) {
      return;
    }
    const core::FreeStockView free = simulation.FreeStockToday();
    if (!free.measured) {
      return;
    }
    const core::WorldState& world = simulation.CompletedState();
    days_today_ = free.days;  // the free stock TABLE1953 bins the next visits by
    const std::uint8_t want_declined = free.days < kPlainBelowDays ? 1 : 0;
    if (want_declined == world.chairman.district_table_declined) {
      return;
    }
    core::OrderRow order;
    order.kind = core::OrderKind::kSetDistrictTable;
    order.enable = want_declined != 0 ? 0 : 1;
    simulation.StageOrders(std::span<const core::OrderRow>(&order, 1), {});
  }

  /// @brief Every step: each kDistrictTable the core raised (amount > 0 a table, 0 plain), binned
  ///        by the last day's free stock. A visit arrives at its day's first tick and leaves the
  ///        book at once, so a daily look at district_visits never saw one (the first pair's
  ///        empty TABLE1953, 22 Nov 2026) - the event is the visit's one trace.
  void WatchStep(const core::WorldState& world) {
    if (!enabled_) {
      return;
    }
    for (const core::SimEvent& event : world.step_events) {
      if (event.kind != core::EventKind::kDistrictTable) {
        continue;
      }
      const std::uint32_t year = (world.calendar.day / core::kDaysPerYear) + 1U;
      Visits& row = by_year_[year];
      const std::size_t bin = days_today_ < 14.0F ? 0 : (days_today_ < 28.0F ? 1 : 2);
      ++(event.amount > 0 ? row.table : row.plain)[bin];
    }
  }

  /// @brief TABLE1953: by year, the visits received with a table and plainly, by the free
  ///        stock's days at arrival (< 14, 14-28, >= 28). Nothing when the path is off.
  void Print(std::ostream& out, std::uint64_t seed) const {
    if (!enabled_) {
      return;
    }
    for (const auto& [year, row] : by_year_) {
      out << "population_curve: TABLE1953 seed " << seed << " year " << year
          << " ground=choice_standing_at_arrival_vs_free_stock_days(lt14,14to28,ge28) table "
          << row.table[0] << ' ' << row.table[1] << ' ' << row.table[2] << " plain " << row.plain[0]
          << ' ' << row.plain[1] << ' ' << row.plain[2] << '\n';
    }
  }

 private:
  struct Visits {
    std::array<std::uint32_t, 3> table{};
    std::array<std::uint32_t, 3> plain{};
  };

  bool enabled_ = false;
  float days_today_ = 0.0F;
  std::map<std::uint32_t, Visits> by_year_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_DISTRICT_TABLE_POLICY_H_
