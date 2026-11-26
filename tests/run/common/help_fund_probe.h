/// @file
/// @brief HELPFUND2199 (#2199's base rate, econ's help-fund-low-earners §4 «base rate first»;
///        not for release): a VIRTUAL help fund on the canon's world, nothing drawn for real -
///        at each year's turn 2 % (and 3 %) of the closed year's grain and potato harvest; at
///        each month's last day every family that ate < 0.9 of its month's need draws, by the
///        grain-equivalent of its gap to 0.9, until the fund is dry. Printed a village-year:
///        the fund, the draws (family-months), the kilograms drawn, the month it ran dry (0 =
///        never), and the ration beside it (family-months on the ration, their eaten share).
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.
#ifndef TESTS_RUN_COMMON_HELP_FUND_PROBE_H_
#define TESTS_RUN_COMMON_HELP_FUND_PROBE_H_

#include <array>
#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

class HelpFundProbe {
 public:
  explicit HelpFundProbe(const core::ITableSet& tables) {
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      for (const char* key : {"rye", "wheat", "barley", "oat", "potato"}) {
        const std::uint32_t row = resources->FindRowByKey(key);
        if (row != core::kNoTableRow) {
          foods_.push_back(row);
        }
      }
    }
  }

  /// @brief Once a day, after the day's step.
  void CountDay(const core::WorldState& world) {
    const std::uint32_t day = world.calendar.day;
    if (day > 0 && day % core::kDaysPerYear == 0) {
      CloseYear(day / core::kDaysPerYear);
      double harvest_g = 0.0;
      for (const std::uint32_t food : foods_) {
        if (food < world.ledger.closed.harvest.size()) {
          harvest_g += static_cast<double>(world.ledger.closed.harvest[food]);
        }
      }
      for (std::size_t share = 0; share < kShares.size(); ++share) {
        funds_[share] = harvest_g * kShares[share];
        start_[share] = funds_[share];
      }
    }
    if (day % core::kDaysPerMonth != core::kDaysPerMonth - 1U) {
      return;  // the month's table is read on its last day, after the day's meals
    }
    const std::uint32_t month = (day % core::kDaysPerYear) / core::kDaysPerMonth + 1U;
    for (const core::FamilyRow& family : world.families.rows) {
      if (!(family.month_need_kcal > 0.0F)) {
        continue;
      }
      const double eaten = static_cast<double>(family.month_eaten_kcal) /
                           static_cast<double>(family.month_need_kcal);
      if (family.ration_granted != 0) {
        ++ration_months_;
        ration_eaten_ += eaten;
      }
      if (eaten >= kLine) {
        continue;
      }
      ++short_months_;
      const double gap_g = ((kLine * static_cast<double>(family.month_need_kcal)) -
                            static_cast<double>(family.month_eaten_kcal)) /
                           kGrainKcalPerGram;
      for (std::size_t share = 0; share < kShares.size(); ++share) {
        if (funds_[share] <= 0.0) {
          continue;
        }
        const double drawn = gap_g < funds_[share] ? gap_g : funds_[share];
        funds_[share] -= drawn;
        drawn_[share] += drawn;
        ++draws_[share];
        if (funds_[share] <= 0.0 && dry_month_[share] == 0) {
          dry_month_[share] = month;
        }
      }
    }
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    for (const std::string& line : lines_) {
      out << "population_curve: HELPFUND2199 seed " << seed << ' ' << line << '\n';
    }
  }

 private:
  static constexpr std::array<double, 2> kShares = {0.02, 0.03};
  static constexpr double kLine = 0.9;
  static constexpr double kGrainKcalPerGram = 3330.0 / 1000.0;  // authority.h's 3.3 kcal a gram

  void CloseYear(std::uint32_t year) {
    std::ostringstream line;
    line << "year " << year
         << " ground=virtual_fund_from_closed_harvest(grain+potato);line=0.9;gap_in_grain_g"
         << " short_family_months " << short_months_;
    for (std::size_t share = 0; share < kShares.size(); ++share) {
      line << " fund" << (share == 0 ? 2 : 3) << "pct_kg " << start_[share] / 1000.0 << " draws "
           << draws_[share] << " drawn_kg " << drawn_[share] / 1000.0 << " dry_month "
           << dry_month_[share];
    }
    line << " ration_family_months " << ration_months_ << " ration_eaten_share "
         << (ration_months_ > 0 ? ration_eaten_ / ration_months_ : -1.0);
    lines_.push_back(line.str());
    short_months_ = 0;
    ration_months_ = 0;
    ration_eaten_ = 0.0;
    draws_ = {};
    drawn_ = {};
    dry_month_ = {};
  }

  std::vector<std::uint32_t> foods_;
  std::array<double, 2> funds_{};
  std::array<double, 2> start_{};
  std::array<double, 2> drawn_{};
  std::array<std::uint32_t, 2> draws_{};
  std::array<std::uint32_t, 2> dry_month_{};
  std::uint32_t short_months_ = 0;
  std::uint32_t ration_months_ = 0;
  double ration_eaten_ = 0.0;
  std::vector<std::string> lines_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_HELP_FUND_PROBE_H_
