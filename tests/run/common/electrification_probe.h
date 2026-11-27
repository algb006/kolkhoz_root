/// @file
/// @brief ELEC2261 (econ for boss's street-lamp question): per village, the first day
///        EraEventState::electrification_unlocked is set, the first day points_granted_total
///        reaches 900, and the first day a farm_office stands at level ≥ 1 - so the binding
///        condition is named. «none» for a day that never came in the run's first 5 years.
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.
#ifndef TESTS_RUN_COMMON_ELECTRIFICATION_PROBE_H_
#define TESTS_RUN_COMMON_ELECTRIFICATION_PROBE_H_

#include <cstdint>
#include <ostream>
#include <string>

#include "core_common/calendar.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

class ElectrificationProbe {
 public:
  explicit ElectrificationProbe(const core::ITableSet& tables) {
    if (const core::ITable* const types = tables.FindTable("unit_types")) {
      office_ = types->FindRowByKey("farm_office");
    }
  }

  /// @brief Once a day after the step, through year 5.
  void CountDay(const core::WorldState& world) {
    const std::uint32_t day = world.calendar.day;
    if (day >= kYears * core::kDaysPerYear) {
      return;
    }
    if (unlocked_ < 0 && world.era_events.electrification_unlocked != 0) {
      unlocked_ = day;
    }
    if (points_ < 0 && world.limit.points_granted_total >= kPoints) {
      points_ = day;
    }
    if (office_day_ < 0 && office_ != core::kNoTableRow) {
      for (const core::UnitRow& unit : world.units.rows) {
        if (unit.dead == 0 && unit.level >= 1 && unit.type.value == office_) {
          office_day_ = day;
          break;
        }
      }
    }
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    out << "population_curve: ELEC2261 seed " << seed << " ground=first_day_y1-5"
        << " electrification_unlocked " << Day(unlocked_) << " points_granted_ge_900 "
        << Day(points_) << " farm_office_level1 " << Day(office_day_) << '\n';
  }

 private:
  static constexpr std::uint32_t kYears = 5;
  static constexpr std::int32_t kPoints = 900;

  static std::string Day(std::int64_t day) {
    if (day < 0) {
      return "none_by_y5";
    }
    return "y" + std::to_string((day / core::kDaysPerYear) + 1) + "d" +
           std::to_string(day % core::kDaysPerYear);
  }

  std::uint32_t office_ = core::kNoTableRow;
  std::int64_t unlocked_ = -1;
  std::int64_t points_ = -1;
  std::int64_t office_day_ = -1;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_ELECTRIFICATION_PROBE_H_
