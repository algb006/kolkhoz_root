/// @file
/// @brief HAUL2157 (econ's food-theft review [4], boss 25 Nov): the church haul in year 1, days
///        30-48 - per day the food lying in the church (kg) and what left it, the store-transfer
///        task (UnitRow::emptying's: level, drained by cart and on foot), the plan's carts that
///        day (every plan of the day: a re-plan drops carts) split by the kind of the task their
///        loaded legs serve, and the food yard's stock. Read only: the world is unchanged.
/// @threading SINGLE_THREADED
/// Test-side code, driven after every step and once a day from the thread that owns the
/// simulation.

#ifndef TESTS_RUN_COMMON_CHURCH_HAUL_TALLY_H_
#define TESTS_RUN_COMMON_CHURCH_HAUL_TALLY_H_

#include <array>
#include <cstdint>
#include <map>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/logistics_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

class ChurchHaulTally {
 public:
  explicit ChurchHaulTally(const core::ITableSet& tables) {
    if (const core::ITable* const types = tables.FindTable("unit_types")) {
      church_ = types->FindRowByKey("church_store");
      food_yard_ = types->FindRowByKey("food_yard");
    }
  }

  /// @brief After every step: which cart served which kind of load in the plan as it stands.
  void CountStep(const core::WorldState& world) {
    if (world.calendar.day >= core::kDaysPerYear) {
      return;
    }
    Day& row = days_[world.groom_plan.day];
    for (const core::CartPlan& cart : world.groom_plan.carts) {
      if (cart.on_foot || cart.people_cart) {
        continue;
      }
      std::uint32_t kind = kNoKind;
      for (const core::CartLeg& leg : cart.legs) {
        if (leg.loaded == 0 || leg.task.value == core::kInvalidEntityIdValue) {
          continue;
        }
        const std::uint32_t task_row = core::FindRow(world.logistics_tasks, leg.task);
        if (task_row != core::kNoRow) {
          kind = static_cast<std::uint32_t>(world.logistics_tasks.rows[task_row].load_kind);
          if (kind == static_cast<std::uint32_t>(core::LogisticsLoadKind::kStoreTransfer)) {
            break;  // a cart that carried the church's load at all counts as the church's
          }
        }
      }
      auto [at, fresh] = row.carts.emplace(cart.driver.value, kind);
      if (!fresh && kind == static_cast<std::uint32_t>(core::LogisticsLoadKind::kStoreTransfer)) {
        at->second = kind;
      }
    }
  }

  /// @brief Once a day, after the day's step.
  void CountDay(const core::WorldState& world) {
    const core::SimDay day = world.calendar.day;
    if (day >= core::kDaysPerYear || day + 1U < kFirstDay) {
      return;
    }
    core::Grams church = 0;
    core::Grams yard = 0;
    bool emptying = false;
    for (const core::UnitRow& unit : world.units.rows) {
      core::Grams lying = 0;
      for (const core::Grams grams : unit.stock) {
        lying += grams;
      }
      if (unit.type.value == church_) {
        church += lying;
        emptying = emptying || unit.emptying != 0;
      } else if (unit.type.value == food_yard_) {
        yard += lying;
      }
    }
    std::ostringstream line;
    line << " day " << (day + 1U) << " church_kg " << Kg(church) << " church_left_kg "
         << Kg(last_church_ > church ? last_church_ - church : 0) << " emptying " << emptying
         << " food_yard_kg " << Kg(yard);
    last_church_ = church;
    // The church's transfer task(s): level and what has been drained from it.
    std::uint32_t tasks = 0;
    for (const core::LogisticsTaskRow& task : world.logistics_tasks.rows) {
      if (task.load_kind != core::LogisticsLoadKind::kStoreTransfer) {
        continue;
      }
      ++tasks;
      line << " [transfer level " << static_cast<int>(task.level) << " base "
           << static_cast<int>(task.base_level) << " paused " << task.paused << " by_cart "
           << task.drained_by_cart << " on_foot " << task.drained_on_foot << "]";
    }
    line << " transfer_tasks " << tasks;
    // Yesterday's carts (the look comes after the turn): all, the church's, the rest by kind.
    const auto found = days_.find(day > 0 ? day - 1U : 0U);
    std::map<std::uint32_t, std::uint32_t> by_kind;
    std::uint32_t carts = 0;
    if (found != days_.end()) {
      for (const auto& [driver, kind] : found->second.carts) {
        ++carts;
        ++by_kind[kind];
      }
      days_.erase(days_.begin(), std::next(found));
    }
    line << " carts_yesterday " << carts << " by_load_kind";
    for (const auto& [kind, count] : by_kind) {
      line << ' ' << (kind == kNoKind ? std::string("none") : std::to_string(kind)) << ':' << count;
    }
    lines_.push_back(line.str());
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    for (const std::string& line : lines_) {
      out << "population_curve: HAUL2157 seed " << seed
          << " ground=year1;kg;load_kind=LogisticsLoadKind_value(store_transfer="
          << static_cast<int>(core::LogisticsLoadKind::kStoreTransfer) << ")" << line << '\n';
    }
  }

 private:
  static constexpr std::uint32_t kFirstDay = 30;
  static constexpr std::uint32_t kNoKind = 0xFFFFFFFFU;

  struct Day {
    std::map<std::uint32_t, std::uint32_t> carts;  ///< driver -> the load kind he served
  };

  static double Kg(core::Grams grams) { return static_cast<double>(grams) / 1000.0; }

  std::uint32_t church_ = core::kNoTableRow;
  std::uint32_t food_yard_ = core::kNoTableRow;
  core::Grams last_church_ = 0;
  std::map<core::SimDay, Day> days_;
  std::vector<std::string> lines_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_CHURCH_HAUL_TALLY_H_
