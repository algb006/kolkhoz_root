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
#include <optional>
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
      const std::uint32_t key = resources->FindColumn("key");
      for (std::uint32_t row = 0; row < resources->RowCount(); ++row) {
        keys_.emplace_back(key != core::kNoTableColumn ? resources->CellText(row, key) : "?");
      }
      // The food's kcal by resource (food.csv), 0 for every row the table does not name.
      kcal_.assign(resources->RowCount(), 0.0);
      if (const core::ITable* const food = tables.FindTable("food")) {
        const std::uint32_t kcal = food->FindColumn("kcal_per_gram");
        for (std::uint32_t row = 0; row < resources->RowCount(); ++row) {
          const std::uint32_t at = food->FindRowByKey(keys_[row]);
          if (at != core::kNoTableRow && kcal != core::kNoTableColumn) {
            if (const std::optional<float> value = food->CellReal(at, kcal)) {
              kcal_[row] = static_cast<double>(*value);
            }
          }
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
    TrackDays(world);  // #2199b's timing (econ's T / M / N)
    if (day % core::kDaysPerMonth == 0) {
      ReadFundMonth(world);  // #2199b: the fund's month - held, free, given, the issue's draw
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
        // THE SPLIT (econ): for those short again with food left - the pantry by kind, and the
        // rest of it (every other food) - so A's limiting kind is named.
        if (eaten < kLine && pantry_g >= kPantryLeftG) {
          double named = 0.0;
          for (std::size_t kind = 0; kind < foods_.size() && kind < split_g_.size(); ++kind) {
            const std::uint32_t food = foods_[kind];
            const double grams =
                food < family.pantry.size() ? static_cast<double>(family.pantry[food]) : 0.0;
            split_g_[kind] += grams;
            named += grams;
          }
          double all = 0.0;
          for (const core::Grams grams : family.pantry) {
            all += static_cast<double>(grams);
          }
          split_other_g_ += all - named;
          // #2199b: every pantry row of them by resource, to name the «other».
          if (pantry_by_row_.size() < family.pantry.size()) {
            pantry_by_row_.resize(family.pantry.size(), 0.0);
          }
          for (std::size_t index = 0; index < family.pantry.size(); ++index) {
            pantry_by_row_[index] += static_cast<double>(family.pantry[index]);
          }
          // #2199b's timing: T - the short day's pantry could not cover that day's need (and
          // food came later); M - it could; N - no short day read (the month's table only).
          const Day& track = days_[id];
          if (track.short_day == 0) {
            ++timing_n_;
          } else if (track.short_day_pantry < track.short_day_need) {
            ++timing_t_;
          } else {
            ++timing_m_;
          }
          if (track.short_day > 0 && track.short_day <= short_day_hist_.size()) {
            ++short_day_hist_[track.short_day - 1U];
          }
          if (track.short_day_need > 0.0) {
            first_days_ += track.first_day_pantry / track.short_day_need;
            short_days_ += track.short_day_pantry / track.short_day_need;
            end_days_ += track.end_pantry / track.short_day_need;
            ++day_reads_;
          }
        }
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

  /// #2199b THE FUND'S MONTH (econ's print): on a month's first day, after its issue hour -
  /// what the fund holds, the free stock above the lower rungs it read, what it gave, and what
  /// the trudodni issue drew of its foods over the month that closed.
  void ReadFundMonth(const core::WorldState& world) {
    const auto sum = [this](const core::ResourceAmounts& amounts) {
      double grams = 0.0;
      for (const std::uint32_t food : foods_) {
        grams += food < amounts.size() ? static_cast<double>(amounts[food]) : 0.0;
      }
      return grams;
    };
    const double given_book = sum(world.ledger.current.help_issued);
    const double issued_book = sum(world.ledger.current.issued);
    if (world.calendar.day % core::kDaysPerYear == 0) {
      given_mark_ = 0.0;  // the book turned
      issued_mark_ = 0.0;
    }
    const double given = given_book - given_mark_;
    const double issued = issued_book - issued_mark_;
    given_mark_ = given_book;
    issued_mark_ = issued_book;
    if (world.calendar.day < core::kDaysPerYear) {
      return;  // year 1 has no fund
    }
    const double held = sum(world.help_fund.held);
    const auto free = static_cast<double>(world.help_fund.probe_free_g);
    ++fund_months_;
    fund_free0_ += free <= 0.0 ? 1U : 0U;
    fund_free_no_give_ += free > 0.0 && given <= 0.0 && held > 0.0 ? 1U : 0U;
    fund_held_ += held;
    fund_free_ += free;
    fund_given_ += given;
    fund_issued_ += issued;
  }

  /// #2199b THE TIMING (econ): each family's day - its eaten and need off the month's table's
  /// growth, its pantry's food kcal after the day (hay and every row of no kcal out) - the
  /// month's first day's pantry and the first day it ate under 0.9 of its day's need.
  void TrackDays(const core::WorldState& world) {
    const std::uint32_t day = world.calendar.day;
    const std::uint32_t day_in_month = day % core::kDaysPerMonth;
    for (std::uint32_t row = 0; row < world.families.rows.size(); ++row) {
      const core::FamilyRow& family = world.families.rows[row];
      Day& track = days_[world.families.row_ids[row].value];
      if (day_in_month == 0) {
        track = Day{};
      }
      const double need = static_cast<double>(family.month_need_kcal) - track.need_so_far;
      const double eaten = static_cast<double>(family.month_eaten_kcal) - track.eaten_so_far;
      track.need_so_far = static_cast<double>(family.month_need_kcal);
      track.eaten_so_far = static_cast<double>(family.month_eaten_kcal);
      double pantry_kcal = 0.0;
      for (std::size_t index = 0; index < family.pantry.size() && index < kcal_.size(); ++index) {
        pantry_kcal += static_cast<double>(family.pantry[index]) * kcal_[index];
      }
      if (day_in_month == 0) {
        track.first_day_pantry = pantry_kcal;
      }
      if (track.short_day == 0 && need > 0.0 && eaten < kLine * need) {
        track.short_day = day_in_month + 1U;
        track.short_day_pantry = pantry_kcal;
        track.short_day_need = need;
      }
      track.end_pantry = pantry_kcal;
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
         << (ration_months_ > 0 ? ration_eaten_ / ration_months_ : -1.0)
         << " after(short_last_month:followed/eaten_next/short_again/pantry_kg_next/"
            "short_with_pantry_ge_5kg) "
         << followed_ << ' ' << (followed_ > 0 ? followed_eaten_ / followed_ : -1.0) << ' '
         << short_again_ << ' ' << (followed_ > 0 ? followed_pantry_g_ / followed_ / 1000.0 : -1.0)
         << ' ' << pantry_left_with_short_ << " split_kg(rye/wheat/barley/oat/potato/other)";
    for (const double grams : split_g_) {
      line << ' ' << grams / 1000.0;
    }
    line << ' ' << split_other_g_ / 1000.0 << " pantry_by_resource_kg";
    for (std::size_t index = 0; index < pantry_by_row_.size(); ++index) {
      if (pantry_by_row_[index] > 0.0) {
        line << ' ' << (index < keys_.size() ? keys_[index] : "?") << '='
             << pantry_by_row_[index] / 1000.0;
      }
    }
    pantry_by_row_.clear();
    line << " timing(T/M/N) " << timing_t_ << ' ' << timing_m_ << ' ' << timing_n_
         << " short_day_1_4";
    for (const std::uint32_t count : short_day_hist_) {
      line << ' ' << count;
    }
    line << " pantry_in_days_of_need(first_hour/short_day/month_end) "
         << (day_reads_ > 0 ? first_days_ / day_reads_ : -1.0) << ' '
         << (day_reads_ > 0 ? short_days_ / day_reads_ : -1.0) << ' '
         << (day_reads_ > 0 ? end_days_ / day_reads_ : -1.0);
    line << " fund_month(months/free0/free_but_none_given/held_kg/free_kg/given_kg/issue_drawn_kg) "
         << fund_months_ << ' ' << fund_free0_ << ' ' << fund_free_no_give_ << ' '
         << (fund_months_ > 0 ? fund_held_ / fund_months_ / 1000.0 : -1.0) << ' '
         << (fund_months_ > 0 ? fund_free_ / fund_months_ / 1000.0 : -1.0) << ' '
         << (fund_months_ > 0 ? fund_given_ / fund_months_ / 1000.0 : -1.0) << ' '
         << (fund_months_ > 0 ? fund_issued_ / fund_months_ / 1000.0 : -1.0);
    fund_months_ = fund_free0_ = fund_free_no_give_ = 0;
    fund_held_ = fund_free_ = fund_given_ = fund_issued_ = 0.0;
    timing_t_ = timing_m_ = timing_n_ = day_reads_ = 0;
    short_day_hist_ = {};
    first_days_ = short_days_ = end_days_ = 0.0;
    split_g_ = {};
    split_other_g_ = 0.0;
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
  std::array<double, 5> split_g_{};
  double split_other_g_ = 0.0;
  std::vector<double> pantry_by_row_;

  struct Day {
    double need_so_far = 0.0;
    double eaten_so_far = 0.0;
    double first_day_pantry = 0.0;
    double short_day_pantry = 0.0;
    double short_day_need = 0.0;
    double end_pantry = 0.0;
    std::uint32_t short_day = 0;
  };

  std::map<std::uint32_t, Day> days_;
  double given_mark_ = 0.0;
  double issued_mark_ = 0.0;
  std::uint32_t fund_months_ = 0;
  std::uint32_t fund_free0_ = 0;
  std::uint32_t fund_free_no_give_ = 0;
  double fund_held_ = 0.0;
  double fund_free_ = 0.0;
  double fund_given_ = 0.0;
  double fund_issued_ = 0.0;
  std::vector<double> kcal_;
  std::uint32_t timing_t_ = 0;
  std::uint32_t timing_m_ = 0;
  std::uint32_t timing_n_ = 0;
  std::uint32_t day_reads_ = 0;
  std::array<std::uint32_t, 4> short_day_hist_{};
  double first_days_ = 0.0;
  double short_days_ = 0.0;
  double end_days_ = 0.0;
  std::vector<std::string> keys_;
  std::uint32_t ration_months_ = 0;
  double ration_eaten_ = 0.0;
  std::vector<std::string> lines_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_HELP_FUND_PROBE_H_
