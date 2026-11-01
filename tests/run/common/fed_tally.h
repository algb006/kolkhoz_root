/// @file
/// @brief The worker-days and their output by satiety band, years 1-5 - #1411's
/// print.
/// @threading SINGLE_THREADED
/// Test-side code, read from the thread that owns the simulation.
///
/// WHY IT EXISTS (#1411; econ, econ-boss-1411-y1-floor [1]): the first year's
/// satiety floor (labor.csv 0.85) never acted - both callers asked
/// `date.year == 0` of a one-based year. Its pair reads the y1 output per
/// hungry worker-day (P1) and the y1 total (P2), and the floor bites only below
/// satiety 70: when fewer than 1 % of y1 worker-days sit below 70, the cure has
/// nothing to act on and the print says NOT MEASURED.
///
/// A WORKER-DAY is a resident out at work that day (hours_away_today > 0),
/// read at hour 22, before the day's close clears the order; his OUTPUT is the
/// day's worked_norm_days_today, the norm-days he was paid. Bands by his
/// satiety at that hour: below 25 (the floor), 25-70 (the slope), 70+ (fed).

#ifndef TESTS_RUN_COMMON_FED_TALLY_H_
#define TESTS_RUN_COMMON_FED_TALLY_H_

#include <array>
#include <cstdint>
#include <ostream>

#include "core_common/world_state.h"

namespace run {

class FedTally {
 public:
  static constexpr std::uint32_t kYears = 5;
  static constexpr std::uint32_t kEveningHour = 22;
  static constexpr std::size_t kBands = 3;

  /// @brief The day's worker-days and output by band. At kEveningHour.
  void CountEvening(const core::WorldState& world) {
    const std::uint32_t year = world.calendar.date.year;  // one-based
    if (year < 1 || year > kYears) {
      return;
    }
    Year& book = years_[year - 1];
    for (const core::ResidentRow& resident : world.residents.rows) {
      if (!(resident.work.hours_away_today > 0.0F)) {
        continue;
      }
      const std::size_t band = resident.satiety < 25.0F ? 0 : (resident.satiety < 70.0F ? 1 : 2);
      ++book.days[band];
      book.output[band] += static_cast<double>(resident.work.worked_norm_days_today);
    }
  }

  /// @brief One line a village: per year, per band, worker-days and output.
  void Print(std::ostream& out, std::uint64_t seed) const {
    out << "population_curve: F1411 seed " << seed;
    for (std::uint32_t year = 0; year < kYears; ++year) {
      const Year& book = years_[year];
      out << " y" << (year + 1);
      for (std::size_t band = 0; band < kBands; ++band) {
        out << ' ' << book.days[band] << ':' << book.output[band];
      }
    }
    const Year& first = years_[0];
    const std::uint32_t all = first.days[0] + first.days[1] + first.days[2];
    const std::uint32_t hungry = first.days[0] + first.days[1];
    if (all == 0 || hungry * 100U < all) {
      out << " — NOT MEASURED: below 1 % of y1 worker-days under satiety 70 (" << hungry << " of "
          << all << "), the first year's floor has nothing to act on";
    }
    out << '\n';
  }

 private:
  struct Year {
    std::array<std::uint32_t, kBands> days = {};
    std::array<double, kBands> output = {};
  };

  std::array<Year, kYears> years_ = {};
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_FED_TALLY_H_
