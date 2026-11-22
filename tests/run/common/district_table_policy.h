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

#include <span>
#include <string_view>

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

  void RunDay(core::ISimulation& simulation) const {
    if (!enabled_) {
      return;
    }
    const core::FreeStockView free = simulation.FreeStockToday();
    if (!free.measured) {
      return;
    }
    const core::WorldState& world = simulation.CompletedState();
    const std::uint8_t want_declined = free.days < kPlainBelowDays ? 1 : 0;
    if (want_declined == world.chairman.district_table_declined) {
      return;
    }
    core::OrderRow order;
    order.kind = core::OrderKind::kSetDistrictTable;
    order.enable = want_declined != 0 ? 0 : 1;
    simulation.StageOrders(std::span<const core::OrderRow>(&order, 1), {});
  }

 private:
  bool enabled_ = false;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_DISTRICT_TABLE_POLICY_H_
