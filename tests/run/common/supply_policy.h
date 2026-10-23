/// @file
/// @brief The run's chairman puts a works' supply «важнее полевых работ» when
/// the floor has not brought it anything for a week and more.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THE RUN ORDERS THIS (#734; transport design «Пол подвоза»). The floor
/// gives a starved supply a share of the team by itself; the chairman's lever
/// is for the works the floor still does not reach. The run plays the player
/// by the lamp, as he would: kSiteSupplyWaits names the works, its task and
/// the move (AlarmAdvice::kSupplyOverFields), and the door is
/// kSetLogisticsLevel on that task with the switch and the task's own level.
///
/// THE RULE IS THE RUN'S, NOT THE GAME'S (econ's, boss 13 October 2026): the
/// switch is set once the works has stood `kDaysOnFloor` working days ON THE
/// FLOOR with nothing delivered - the lamp's idle days at the floor's own
/// trigger plus eight - and only on a morning no load stands at level 0 (a
/// herd's feed and a spoiling heap go first, and the switch would not outrank
/// them anyway).

#ifndef TESTS_RUN_COMMON_SUPPLY_POLICY_H_
#define TESTS_RUN_COMMON_SUPPLY_POLICY_H_

#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/logistics_state.h"
#include "core_common/order_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/work_seam.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace run {

/// @brief Sets «важнее полевых работ» on a supply the floor does not reach.
class SupplyPolicy {
 public:
  explicit SupplyPolicy(const core::ITableSet& tables) {
    // The floor's own trigger, from the table the core reads it from.
    const core::ITable* const logistics = tables.FindTable("logistics");
    const std::uint32_t row = logistics == nullptr
                                  ? core::kNoTableRow
                                  : logistics->FindRowByKey("site_supply_floor_after_days");
    const std::uint32_t column =
        logistics == nullptr ? core::kNoTableColumn : logistics->FindColumn("value");
    if (row != core::kNoTableRow && column != core::kNoTableColumn) {
      const std::optional<float> days = logistics->CellReal(row, column);
      floor_after_days_ = days ? static_cast<std::int64_t>(*days) : floor_after_days_;
    }
    if (const core::ITable* const kinds = tables.FindTable("livestock")) {
      horse_kind_ = kinds->FindRowByKey("horse");
    }
  }

  /// @brief One day of the chairman's attention. Call once a day.
  void RunDay(core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    alarms_.clear();
    simulation.CollectAlarms(alarms_);
    bool urgent_load = false;
    for (const core::LogisticsTaskRow& task : world.logistics_tasks.rows) {
      urgent_load = urgent_load || (task.level == core::LogisticsLevel::kUrgent && !task.paused);
    }
    // The player's lamp, printed (its days lose the mowing's since save 165).
    for (const core::Alarm& alarm : alarms_) {
      lamp_days_ += alarm.kind == core::AlarmKind::kSiteSupplyWaits ? 1U : 0U;
    }
    // THE BALANCE BOT READS THE FLOOR'S OWN COUNT, NOT THE LAMP (boss, the В6
    // thread [12]: the bot measures the economy, the lamp is the player's):
    // the works whose supply the floor reaches - the lamp's set until save 165,
    // in its order - by ConstructionState::supply_idle_days whole.
    const core::LivestockKindId horse =
        horse_kind_ == core::kNoTableRow
            ? core::LivestockKindId{}
            : core::LivestockKindId{static_cast<std::uint16_t>(horse_kind_)};
    if (!core::SettlementHasCarts(world, horse)) {
      return;
    }
    for (std::uint32_t row = 0; row < world.logistics_tasks.rows.size(); ++row) {
      const core::LogisticsTaskRow& task = world.logistics_tasks.rows[row];
      if (task.load_kind != core::LogisticsLoadKind::kSiteSupply || task.paused) {
        continue;
      }
      const std::uint32_t unit_row = core::FindRow(world.units, task.unit);
      if (unit_row == core::kNoRow) {
        continue;
      }
      const core::ConstructionState& works = world.units.rows[unit_row].construction;
      const auto idle = static_cast<std::int64_t>(works.supply_idle_days);
      if (floor_after_days_ <= 0 || idle < floor_after_days_ || !core::AnythingAwaits(works)) {
        continue;
      }
      ++floor_days_;
      longest_wait_ = idle > longest_wait_ ? idle : longest_wait_;
      if (task.over_fields || idle < floor_after_days_ + kDaysOnFloor) {
        continue;
      }
      if (urgent_load) {
        ++held_by_level_zero_;
        continue;
      }
      core::OrderRow order;
      order.kind = core::OrderKind::kSetLogisticsLevel;
      order.logistics_task = world.logistics_tasks.row_ids[row];
      order.logistics_level = task.base_level;
      order.enable = 1;
      simulation.StageOrders(std::span<const core::OrderRow>(&order, 1), {});
      ++ordered_;
    }
  }

  /// @brief The fixture difference, in words, BEFORE the run measures.
  static void Declare(std::string_view run_name) {
    std::cout << run_name
              << ": FIXTURE DIFFERS FROM THE START CANON — the run's chairman puts a works' "
                 "supply «важнее полевых работ» (kSetLogisticsLevel with the switch) once the "
                 "works has stood on the supply's floor "
              << kDaysOnFloor
              << " working days past the floor's own trigger, on a morning no load stands at "
                 "level 0 (#734; econ's rule) - by the floor's own count, not the lamp "
                 "site_supply_waits (since save 165 the lamp leaves the mowing's days out; "
                 "boss, the В6 thread [12])\n";
  }

  /// @brief What the fixture did, for the run to print at the end.
  void Report(std::string_view run_name) const {
    std::cout << run_name << ": the supply's floor — on it " << floor_days_
              << " works-days, the lamp site_supply_waits lit " << lamp_days_
              << " works-days, the longest wait " << longest_wait_
              << " working days with nothing delivered; «важнее полевых работ» ordered " << ordered_
              << " times, held back by a load at level 0 on " << held_by_level_zero_
              << " works-days\n";
  }

  std::uint32_t ordered() const { return ordered_; }

  std::uint32_t lamp_days() const { return lamp_days_; }

  std::int64_t longest_wait() const { return longest_wait_; }

 private:
  /// Working days on the floor with nothing delivered before the chairman
  /// moves (econ's eight; STUB).
  static constexpr std::int64_t kDaysOnFloor = 8;

  std::vector<core::Alarm> alarms_;
  /// logistics.csv `site_supply_floor_after_days`: the lamp's own trigger.
  std::int64_t floor_after_days_ = 4;
  std::uint32_t ordered_ = 0;
  std::uint32_t lamp_days_ = 0;
  /// Works-days on the floor by its own count - the bot's set (until save 165
  /// the lamp's own, and `lamp_days_` with it).
  std::uint32_t floor_days_ = 0;
  /// livestock.csv's `horse` row: the floor gives carters with horses only.
  std::uint32_t horse_kind_ = core::kNoTableRow;
  std::uint32_t held_by_level_zero_ = 0;
  std::int64_t longest_wait_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_SUPPLY_POLICY_H_
