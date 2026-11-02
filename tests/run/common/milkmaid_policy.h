/// @file
/// @brief The run's chairman posts the acting chairman's trained women as
/// milkmaids the day the first cattle yard stands - and prints where their
/// year-1 days went.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THE RUN DOES THIS (#1417; avatars.md l.184, as boss wrote it 2 Nov: «no
/// post before the yard»): genesis gives the first three adult women without a
/// post skill_agriculture_earned 75 (core_world/start_avatar.cpp). The start has
/// no cattle yard, the milkmaid post's only unit (unit_staff.csv), so the post
/// is the player's move. The run's chairman makes it the day a yard stands. A
/// post holder at a unit with herds takes their care first (labor_system.cpp,
/// AssignPostHolders). The trained women are found by that skill: nothing else
/// writes it (#1424).
///
/// THE PRINT (econ, econ-boss-1427-avatar-gates [1]): the trained women, the
/// first yard's standing day, the day the last of them was posted, and their
/// worker-days of year 1 on herd care and on everything else, read at the
/// «noon» step (the state's hour 13).

#ifndef TESTS_RUN_COMMON_MILKMAID_POLICY_H_
#define TESTS_RUN_COMMON_MILKMAID_POLICY_H_

#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#include "core_common/labor_state.h"
#include "core_common/order_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace run {

class MilkmaidPolicy {
 public:
  /// The trained skill genesis writes (start_avatar.cpp, kTrainedMilkmaidSkill).
  static constexpr float kTrainedSkill = 75.0F;

  explicit MilkmaidPolicy(const core::ITableSet& tables) {
    if (const core::ITable* const types = tables.FindTable("unit_types")) {
      const std::uint32_t row = types->FindRowByKey("cattle_yard");
      yard_ = row == core::kNoTableRow ? 0xFFFFU : static_cast<std::uint16_t>(row);
    }
    if (const core::ITable* const posts = tables.FindTable("professions")) {
      const std::uint32_t row = posts->FindRowByKey("milkmaid");
      milkmaid_ = row == core::kNoTableRow ? core::ProfessionId{}
                                           : core::DefIdFromIndex<core::ProfessionIdTag>(row);
    }
  }

  /// @brief One day of the chairman's attention. Call once a day.
  void RunDay(core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    // COUNTED ON THE FIRST DAY THE CHAIRMAN SEES, not on day 0: the run calls
    // RunDay after each day's steps, so day 0 is never seen here, and the first
    // draft counted nobody and posted nobody (a21acting, 15eb2922).
    if (!counted_) {
      counted_ = true;
      for (const core::ResidentRow& resident : world.residents.rows) {
        trained_ += resident.skill_agriculture_earned >= kTrainedSkill ? 1U : 0U;
      }
    }
    if (trained_ == 0 || yard_ == 0xFFFFU || milkmaid_.value == core::kInvalidDefIdValue) {
      return;
    }
    core::UnitId yard;
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      if (unit.type.value == yard_ && unit.level > 0 && unit.dead == 0) {
        yard = world.units.row_ids[row];
        break;
      }
    }
    if (yard.value == core::kInvalidEntityIdValue) {
      return;
    }
    if (stands_day_ < 0) {
      stands_day_ = static_cast<std::int64_t>(world.calendar.day);
    }
    std::vector<core::OrderRow> orders;
    std::uint32_t posted = 0;
    for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
      const core::ResidentRow& resident = world.residents.rows[row];
      if (resident.skill_agriculture_earned < kTrainedSkill) {
        continue;
      }
      if (resident.post.profession.value == milkmaid_.value) {
        ++posted;
        continue;
      }
      if (resident.post.profession.value != core::kInvalidDefIdValue) {
        continue;  // she holds another post: the player's to move, not the bot's
      }
      core::OrderRow order;
      order.kind = core::OrderKind::kAppoint;
      order.resident = world.residents.row_ids[row];
      order.unit = yard;
      order.profession = milkmaid_;
      orders.push_back(order);
    }
    if (posted == trained_ && posted_day_ < 0) {
      posted_day_ = static_cast<std::int64_t>(world.calendar.day);
    }
    if (!orders.empty()) {
      simulation.StageOrders(std::span<const core::OrderRow>(orders.data(), orders.size()), {});
    }
  }

  /// @brief Where the trained women's year-1 days went. At the «noon» step.
  void CountNoon(const core::WorldState& world) {
    if (world.calendar.date.year != 1) {  // one-based
      return;
    }
    for (const core::ResidentRow& resident : world.residents.rows) {
      if (resident.skill_agriculture_earned < kTrainedSkill) {
        continue;
      }
      if (resident.work.kind == core::WorkKind::kHerdCare) {
        ++care_days_;
      } else if (resident.work.kind != core::WorkKind::kNone) {
        ++other_days_;
      }
    }
  }

  /// @brief The line a village.
  void Report(std::string_view run_name, std::uint64_t seed) const {
    std::cout << run_name << ": M1417 seed " << seed << " trained " << trained_
              << " first_yard_stands " << stands_day_ << " all_posted " << posted_day_
              << " y1_herd_care_days " << care_days_ << " y1_other_work_days " << other_days_
              << (trained_ == 0 ? " (no trained women: no avatar or not the acting chairman)" : "")
              << '\n';
  }

 private:
  std::uint16_t yard_ = 0xFFFFU;
  core::ProfessionId milkmaid_;
  std::uint32_t trained_ = 0;
  bool counted_ = false;
  std::int64_t stands_day_ = -1;
  std::int64_t posted_day_ = -1;
  std::uint32_t care_days_ = 0;
  std::uint32_t other_days_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_MILKMAID_POLICY_H_
