/// @file
/// @brief #1580's tally: on every year turn, the YearLedger members that day 0's hour 0 wrote
///        into the closing book after the run's last-day reading of it (LedgerTurnDiff between
///        the book at the close of the year's last day and the same book once closed).
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY (#1580; core-boss-1568 crime links): food_theft's store audit acted at hour 0 of a
/// year's first day, booked into the CLOSING year after a run had printed that year's line from
/// the last day's `current`, and fell out of both years' lines. The rotation is by design
/// (world.cpp RotateLedger: the turn's own bookkeeping lands in the year it settles); every
/// OTHER writer acting in that hour is what this tally names - each member, how many turns.
///
/// THE PRINT, a village: «LEDGER1580 seed S members M turns T changed name:count ...».

#ifndef TESTS_RUN_COMMON_LEDGER_TURN_TALLY_H_
#define TESTS_RUN_COMMON_LEDGER_TURN_TALLY_H_

#include <cstdint>
#include <map>
#include <ostream>
#include <string>

#include "core_common/calendar.h"
#include "core_common/ledger_state.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "ledger_turn_diff.h"

namespace run {

class LedgerTurnTally {
 public:
  /// @brief Once a day after the day's steps.
  void CountDayEnd(const core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    const core::SimDay day = world.calendar.day;
    const core::SimDay of_year = day % core::kDaysPerYear;
    if (of_year == core::kDaysPerYear - 1U) {
      last_day_book_ = world.ledger.current;
      have_book_ = true;
      return;
    }
    if (of_year == 0 && day > 0 && have_book_) {
      ++turns_;
      for (const std::string& name : LedgerTurnDiff(last_day_book_, world.ledger.closed)) {
        ++changed_[name];
      }
      have_book_ = false;
    }
  }

  /// @brief The village's line.
  void Print(std::ostream& out, std::uint64_t seed) const {
    out << "population_curve: LEDGER1580 seed " << seed << " members " << LedgerTurnDiffMembers()
        << " turns " << turns_ << " changed";
    if (changed_.empty()) {
      out << " -";
    }
    for (const auto& [name, count] : changed_) {
      out << ' ' << name << ':' << count;
    }
    out << '\n';
  }

 private:
  core::YearLedger last_day_book_;
  bool have_book_ = false;
  std::uint32_t turns_ = 0;
  std::map<std::string, std::uint32_t> changed_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_LEDGER_TURN_TALLY_H_
