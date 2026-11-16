/// @file
/// @brief #1737 option 1's instrument for EVERY run (boss, core-econ-1737-canon-pair [12]):
///        the milk position's cuts and the kolkhoz cows under the milk floor, read off the world
///        once a day whoever handed the cows - the novice's host rules as much as HayAnswer.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY NOT HAY1737M: its world reads live in HayAnswer::NoteTheDay, which runs only where the
/// limit policy drives HayAnswer. The novice hands its cows through its own rules (the same
/// core door, OrderHandStock), so HAY1737M read «-» there while DO1585M showed the hay's term
/// sizing 80 evenings - an instrument entering by a different door from the cut it measures.
///
/// THE PRINT, a village: «MILK1737 seed S years y:cut_kg:below_floor_days:deepest_below ...»
/// - per year, the milk position's drops between two days of one year (the turn re-sets it),
/// the days the kolkhoz adult cows stood below the milk floor (the herd plan's floor_heads),
/// and the most heads below it on one day; «-» when nothing happened.

#ifndef TESTS_RUN_COMMON_MILK_CUT_TALLY_H_
#define TESTS_RUN_COMMON_MILK_CUT_TALLY_H_

#include <algorithm>
#include <array>
#include <cstdint>
#include <ostream>
#include <string>

#include "core_common/calendar.h"
#include "core_common/herd_plan_view.h"
#include "core_common/quantities.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "core_tables/tables.h"

namespace run {

class MilkCutTally {
 public:
  explicit MilkCutTally(const core::ITableSet& tables) {
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      milk_ = resources->FindRowByKey("milk");
    }
    if (const core::ITable* const livestock = tables.FindTable("livestock")) {
      cow_ = livestock->FindRowByKey("cow");
    }
  }

  /// @brief Once a day after the day's steps.
  void CountDayEnd(const core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    const std::uint32_t year = (world.calendar.day / core::kDaysPerYear) + 1U;
    if (year > kYears) {
      return;
    }
    const core::Grams due = milk_ < world.plan.due.size() ? world.plan.due[milk_] : 0;
    if (year == last_year_ && due < last_due_) {
      cut_[year - 1U] += last_due_ - due;
    }
    last_due_ = due;
    last_year_ = year;
    std::int64_t floor = 0;
    for (const core::HerdPlanLine& line : simulation.HerdPlans()) {
      floor = line.kind.value == cow_ ? std::max<std::int64_t>(floor, line.floor_heads) : floor;
    }
    std::int64_t cows = 0;
    for (const core::HerdRow& herd : world.herds.rows) {
      cows += herd.household_owned == 0 && herd.kind.value == cow_ ? herd.adult_count : 0;
    }
    if (floor > 0 && cows < floor) {
      below_days_[year - 1U] += 1;
      deepest_[year - 1U] = std::max(deepest_[year - 1U], floor - cows);
    }
  }

  /// @brief The village's line.
  void Print(std::ostream& out, std::uint64_t seed) const {
    out << "population_curve: MILK1737 seed " << seed
        << " years(cut_kg:below_floor_days:deepest_below)";
    bool any = false;
    for (std::uint32_t index = 0; index < kYears; ++index) {
      if (cut_[index] == 0 && below_days_[index] == 0) {
        continue;
      }
      any = true;
      out << " y" << index + 1U << ':' << cut_[index] / 1000 << ':' << below_days_[index] << ':'
          << deepest_[index];
    }
    out << (any ? "" : " -") << '\n';
  }

 private:
  static constexpr std::uint32_t kYears = 16;
  std::uint32_t milk_ = core::kNoTableRow;
  std::uint32_t cow_ = core::kNoTableRow;
  std::uint32_t last_year_ = 0;
  core::Grams last_due_ = 0;
  std::array<core::Grams, kYears> cut_{};
  std::array<std::uint32_t, kYears> below_days_{};
  std::array<std::int64_t, kYears> deepest_{};
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_MILK_CUT_TALLY_H_
