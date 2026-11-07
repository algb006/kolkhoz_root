#ifndef TESTS_RUN_COMMON_DAY_ZERO_TALLY_H_
#define TESTS_RUN_COMMON_DAY_ZERO_TALLY_H_

/// @file day_zero_tally.h
/// @brief #1580's probe (boss, core-boss-1580-ledger-hour0 [2] (1)): what the step at hour 0 of a
/// year's first
///        day books into the CLOSING year. The ledger turns in that step's events slot (world.cpp,
///        RotateLedger), after every other slot has written `current`. So the closed book seen at
///        the end of day 0, minus the open book seen at the end of the year's last day, is exactly
///        that step's writes - member by member (ledger_sums.h, all 157). One line a village: «DAY0
///        seed S years N | member diff closed | ...», the members with a non-zero diff summed over
///        y2-y16 and their closed totals.
/// @threading SINGLE_THREADED - the run's own loop, after the day.

#include <cstdint>
#include <map>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/world_state.h"
#include "ledger_sums.h"

namespace run {

class DayZeroTally {
 public:
  static constexpr std::uint32_t kLastYear = 16;

  void CountDayEnd(const core::WorldState& world) {
    const std::uint32_t day_of_year = world.calendar.day % core::kDaysPerYear;
    const std::uint32_t year = (world.calendar.day / core::kDaysPerYear) + 1U;
    if (day_of_year == core::kDaysPerYear - 1U && year >= 1 && year < kLastYear) {
      before_ = LedgerSums(world.ledger.current);  // the closing year's book at its last day's end
      have_before_ = true;
      return;
    }
    if (day_of_year != 0 || !have_before_ || year < 2 || year > kLastYear) {
      return;
    }
    const std::vector<std::pair<std::string_view, double>> after = LedgerSums(world.ledger.closed);
    for (std::size_t index = 0; index < after.size() && index < before_.size(); ++index) {
      Entry& entry = by_member_[std::string(after[index].first)];
      const double diff = after[index].second - before_[index].second;
      entry.diff += diff;
      entry.closed += after[index].second;
      entry.years += diff != 0.0 ? 1U : 0U;
    }
    ++years_;
    have_before_ = false;
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    out << "population_curve: DAY0 seed " << seed << " years " << years_;
    for (const auto& [name, entry] : by_member_) {
      if (entry.diff != 0.0) {
        out << " | " << name << ' ' << entry.diff << ' ' << entry.closed << ' ' << entry.years;
      }
    }
    out << '\n';
  }

 private:
  struct Entry {
    double diff = 0.0;
    double closed = 0.0;
    std::uint32_t years = 0;
  };

  std::vector<std::pair<std::string_view, double>> before_;
  bool have_before_ = false;
  std::uint32_t years_ = 0;
  std::map<std::string, Entry> by_member_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_DAY_ZERO_TALLY_H_
