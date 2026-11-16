/// @file
/// @brief #1726's B3 (econ, the gates set before rows; boss 17 Nov): the family hunger-days of
///        years 1-6, a village - a family-day counts when its members' mean satiety stands under
///        40, the rule host's plan700 counts by (hungry_family_days).
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// READ AT THE DAY'S END, not at host's mid-day: the satiety the day's meals left. The two are
/// one rule at two hours; a pair read by one of them compares like with like.
///
/// THE PRINT: «HUNGER1726 seed S hungry_family_days y1..y6 a b c d e f».

#ifndef TESTS_RUN_COMMON_FAMILY_HUNGER_TALLY_H_
#define TESTS_RUN_COMMON_FAMILY_HUNGER_TALLY_H_

#include <array>
#include <cstdint>
#include <map>
#include <ostream>
#include <utility>

#include "core_common/calendar.h"
#include "core_common/world_state.h"

namespace run {

class FamilyHungerTally {
 public:
  /// @brief Once a day after the day's steps.
  void CountDayEnd(const core::WorldState& world) {
    const std::uint32_t year = (world.calendar.day / core::kDaysPerYear) + 1U;
    if (year > kYears) {
      return;
    }
    std::map<std::uint32_t, std::pair<double, std::uint32_t>> families;
    for (const core::ResidentRow& resident : world.residents.rows) {
      auto& sum = families[resident.family.value];
      sum.first += static_cast<double>(resident.satiety);
      sum.second += 1;
    }
    for (const auto& [family, sum] : families) {
      if (sum.second > 0 && sum.first / static_cast<double>(sum.second) < kHungryBelow) {
        hungry_[year - 1U] += 1;
      }
    }
  }

  /// @brief The village's line.
  void Print(std::ostream& out, std::uint64_t seed) const {
    out << "population_curve: HUNGER1726 seed " << seed << " hungry_family_days";
    for (const std::uint32_t days : hungry_) {
      out << ' ' << days;
    }
    out << '\n';
  }

 private:
  static constexpr std::uint32_t kYears = 6;
  static constexpr double kHungryBelow = 40.0;  ///< host's plan700 rule
  std::array<std::uint32_t, kYears> hungry_{};
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_FAMILY_HUNGER_TALLY_H_
