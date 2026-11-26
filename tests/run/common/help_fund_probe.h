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
#include <map>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
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
    std::map<std::uint32_t, double> short_now;
    for (std::uint32_t row = 0; row < world.families.rows.size(); ++row) {
      const core::FamilyRow& family = world.families.rows[row];
      if (!(family.month_need_kcal > 0.0F)) {
        continue;
      }
      const double eaten = static_cast<double>(family.month_eaten_kcal) /
                           static_cast<double>(family.month_need_kcal);
      // THE MONTH AFTER (econ's reading rule A/B/C): a family short last month - topped on
      // this month's first hour - how much it ate this month, whether short again, and the
      // grain and potato left in its pantry now (any source: the fund's is not told apart).
      const std::uint32_t id = world.families.row_ids[row].value;
      if (previous_short_.contains(id)) {
        ++followed_;
        followed_eaten_ += eaten;
        short_again_ += eaten < kLine ? 1U : 0U;
        double pantry_g = 0.0;
        for (const std::uint32_t food : foods_) {
          pantry_g += food < family.pantry.size() ? static_cast<double>(family.pantry[food]) : 0.0;
        }
        followed_pantry_g_ += pantry_g;
        pantry_left_with_short_ += eaten < kLine && pantry_g >= kPantryLeftG ? 1U : 0U;
      }
      if (eaten < kLine) {
        short_now[id] = eaten;
      }
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
    previous_short_ = std::move(short_now);
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
         << (ration_months_ > 0 ? ration_eaten_ / ration_months_ : -1.0)
         << " after(short_last_month:followed/eaten_next/short_again/pantry_kg_next/"
            "short_with_pantry_ge_5kg) "
         << followed_ << ' ' << (followed_ > 0 ? followed_eaten_ / followed_ : -1.0) << ' '
         << short_again_ << ' ' << (followed_ > 0 ? followed_pantry_g_ / followed_ / 1000.0 : -1.0)
         << ' ' << pantry_left_with_short_;
    followed_ = 0;
    followed_eaten_ = 0.0;
    short_again_ = 0;
    followed_pantry_g_ = 0.0;
    pantry_left_with_short_ = 0;
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
  static constexpr double kPantryLeftG = 5000.0;  // «grain still in the pantry»: 5 kg or more
  std::map<std::uint32_t, double> previous_short_;
  std::uint32_t followed_ = 0;
  double followed_eaten_ = 0.0;
  std::uint32_t short_again_ = 0;
  double followed_pantry_g_ = 0.0;
  std::uint32_t pantry_left_with_short_ = 0;
  std::uint32_t ration_months_ = 0;
  double ration_eaten_ = 0.0;
  std::vector<std::string> lines_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_HELP_FUND_PROBE_H_
