/// @file
/// @brief #1928's rerun print (econ econ-boss-1928-cap-read [1], boss): CAP1928 a village-year
///        y1-y16 - the hands by work at the state's hour 13 (one hand-day each), by game month:
///        felling+hauling, construction, field work (plough, harrow, sow, harvest, the
///        meadows' mowing included) and herd care; the yards' plot hours at hour 22
///        (FamilyRow::household_hours, after the plot); the old households' mean satiety and
///        their family-days under the ration line (OLD as plan700's FamilyIsOld: formed at
///        genesis or a game year before); the kolkhoz cows' milk per head (the closed book's
///        herd_produce over the cows standing at the turn).
/// @threading SINGLE_THREADED
/// Test-side code, driven from the walker's hour-13 and hour-22 reads.

#ifndef TESTS_RUN_COMMON_CAP_RERUN_TALLY_H_
#define TESTS_RUN_COMMON_CAP_RERUN_TALLY_H_

#include <array>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/labor_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

class CapRerunTally {
 public:
  static constexpr std::uint32_t kYears = 16;
  static constexpr float kRationLine = 40.0F;

  explicit CapRerunTally(const core::ITableSet& tables) {
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      milk_ = resources->FindRowByKey("milk");
    }
    if (const core::ITable* const livestock = tables.FindTable("livestock")) {
      cow_ = livestock->FindRowByKey("cow");
    }
  }

  /// @brief At the state's hour 13: one hand-day for each hand at a work.
  void CountNoon(const core::WorldState& world) {
    CloseIfTurned(world);
    const std::uint32_t year = YearOf(world);
    if (year > kYears) {
      return;
    }
    const std::size_t month = MonthOf(world);
    for (const core::ResidentRow& resident : world.residents.rows) {
      switch (resident.work.kind) {
        case core::WorkKind::kFelling:
        case core::WorkKind::kHauling:
          ++timber_[month];
          break;
        case core::WorkKind::kConstruction:
          ++building_[month];
          break;
        case core::WorkKind::kPlowing:
        case core::WorkKind::kHarrowing:
        case core::WorkKind::kSowing:
        case core::WorkKind::kHarvest:
          ++field_[month];
          break;
        case core::WorkKind::kHerdCare:
          ++herd_[month];
          break;
        default:
          break;
      }
    }
  }

  /// @brief At the state's hour 22, after the plot: the yards' hours, the old households.
  void CountEvening(const core::WorldState& world) {
    CloseIfTurned(world);
    if (YearOf(world) > kYears) {
      return;
    }
    const std::size_t families = world.families.rows.size();
    std::vector<double> satiety(families, 0.0);
    std::vector<std::uint32_t> members(families, 0);
    for (const core::ResidentRow& resident : world.residents.rows) {
      const std::uint32_t row = core::FindRow(world.families, resident.family);
      if (row != core::kNoRow) {
        satiety[row] += static_cast<double>(resident.satiety);
        ++members[row];
      }
    }
    for (std::uint32_t row = 0; row < families; ++row) {
      const core::FamilyRow& family = world.families.rows[row];
      plot_hours_ += static_cast<double>(family.household_hours);
      const bool old = family.formed_day == core::kNotFormedInPlay ||
                       world.calendar.day >= family.formed_day + core::kDaysPerYear;
      if (!old || members[row] == 0) {
        continue;
      }
      const double mean = satiety[row] / members[row];
      old_satiety_sum_ += mean;
      ++old_family_days_;
      old_under_line_ += mean <= kRationLine ? 1U : 0U;
    }
  }

  /// @brief The village's lines.
  void Print(std::ostream& out, std::uint64_t seed) const {
    std::istringstream lines(lines_.str());
    std::string line;
    while (std::getline(lines, line)) {
      out << "population_curve: CAP1928 seed " << seed << ' ' << line << '\n';
    }
  }

 private:
  static std::uint32_t YearOf(const core::WorldState& world) {
    return (world.calendar.day / core::kDaysPerYear) + 1U;
  }

  static std::size_t MonthOf(const core::WorldState& world) {
    return (world.calendar.day % core::kDaysPerYear) / core::kDaysPerMonth;
  }

  void CloseIfTurned(const core::WorldState& world) {
    const std::uint32_t year = YearOf(world);
    if (open_year_ == 0) {
      open_year_ = year;
      return;
    }
    if (year == open_year_) {
      return;
    }
    if (open_year_ <= kYears) {
      Write(world, open_year_);
    }
    open_year_ = year;
  }

  void Write(const core::WorldState& world, std::uint32_t year) {
    std::uint32_t cows = 0;
    for (const core::HerdRow& herd : world.herds.rows) {
      if (herd.household_owned == 0 && herd.kind.value == cow_) {
        cows += herd.adult_count;
      }
    }
    const core::YearLedger& book = world.ledger.closed;
    const double milk_kg = milk_ < book.herd_produce.size()
                               ? static_cast<double>(book.herd_produce[milk_]) / 1000.0
                               : -1.0;
    lines_ << "year " << year;
    const auto months = [this](const char* name, const std::array<std::uint32_t, 12>& by) {
      lines_ << ' ' << name;
      for (const std::uint32_t one : by) {
        lines_ << ' ' << one;
      }
    };
    months("timber_hand_days_by_month", timber_);
    months("building_hand_days_by_month", building_);
    months("field_hand_days_by_month", field_);
    months("herd_hand_days_by_month", herd_);
    lines_ << " plot_hours " << static_cast<long>(plot_hours_) << " old_family_days "
           << old_family_days_ << " old_mean_satiety "
           << (old_family_days_ > 0 ? old_satiety_sum_ / old_family_days_ : -1.0)
           << " old_under_ration_line " << old_under_line_ << " kolkhoz_cows_at_turn " << cows
           << " milk_kg " << static_cast<long>(milk_kg) << " milk_kg_per_cow "
           << (cows > 0 && milk_kg >= 0.0 ? milk_kg / cows : -1.0) << '\n';
    timber_.fill(0);
    building_.fill(0);
    field_.fill(0);
    herd_.fill(0);
    plot_hours_ = 0.0;
    old_satiety_sum_ = 0.0;
    old_family_days_ = 0;
    old_under_line_ = 0;
  }

  std::uint32_t milk_ = core::kNoTableRow;
  std::uint32_t cow_ = core::kNoTableRow;
  std::uint32_t open_year_ = 0;
  std::array<std::uint32_t, 12> timber_{};
  std::array<std::uint32_t, 12> building_{};
  std::array<std::uint32_t, 12> field_{};
  std::array<std::uint32_t, 12> herd_{};
  double plot_hours_ = 0.0;
  double old_satiety_sum_ = 0.0;
  std::uint32_t old_family_days_ = 0;
  std::uint32_t old_under_line_ = 0;
  std::ostringstream lines_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_CAP_RERUN_TALLY_H_
