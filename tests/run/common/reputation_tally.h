/// @file
/// @brief #1897's ride-along (boss, econ): REP1897 a village-year y1-y16 - the district's
///        reputation at the year's close and its year by cause, read off the closed book
///        (ledger_state.h YearLedger): the plan's verdict (met or failed), the overfulfilment's
///        tiers, the clean visits' recovery, and every loss (the seizure, the plan trade, the
///        audit's remark, the write-off - booked together by both their doors). The core has
///        no other writer of the rating: every other «Растёт» line of district design §5 is
///        absent (grep, 21 Nov 2026).
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.

#ifndef TESTS_RUN_COMMON_REPUTATION_TALLY_H_
#define TESTS_RUN_COMMON_REPUTATION_TALLY_H_

#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>

#include "core_common/calendar.h"
#include "core_common/world_state.h"

namespace run {

class ReputationTally {
 public:
  static constexpr std::uint32_t kYears = 16;

  /// @brief Once a day; reads the book on the turn's first day.
  void CountDay(const core::WorldState& world) {
    const core::SimDay day = world.calendar.day;
    if (day == last_day_) {
      return;
    }
    last_day_ = day;
    if (!opened_) {  // the first day read: the rating the first year opens at
      opened_ = true;
      reputation_open_ = world.chairman.raikom_reputation;
    }
    const std::uint32_t year = (day / core::kDaysPerYear) + 1U;
    if (day % core::kDaysPerYear != 0 || year < 2 || year - 1U > kYears) {
      return;
    }
    const core::YearLedger& book = world.ledger.closed;
    lines_ << "year " << year - 1U << " reputation_at_close " << world.chairman.raikom_reputation
           << " plan_step " << book.reputation_plan_step << " tier_step "
           << book.reputation_tier_step << " tiers_reached " << book.reputation_tiers_reached
           << " visit_step " << book.reputation_visit_step << " clean_visits "
           << book.reputation_clean_visits << " audit_step " << book.reputation_audit_step
           << " clean_audits_low " << book.reputation_clean_audits_low << " losses "
           << book.reputation_losses
           << " (seizure+plan trade+audit remark+write-off, one bucket) unbooked "
           << (world.chairman.raikom_reputation - reputation_open_) -
                  (book.reputation_plan_step + book.reputation_tier_step +
                   book.reputation_visit_step + book.reputation_audit_step - book.reputation_losses)
           << '\n';
    reputation_open_ = world.chairman.raikom_reputation;
  }

  /// @brief The village's lines.
  void Print(std::ostream& out, std::uint64_t seed) const {
    std::istringstream lines(lines_.str());
    std::string line;
    while (std::getline(lines, line)) {
      out << "population_curve: REP1897 seed " << seed << ' ' << line << '\n';
    }
  }

 private:
  core::SimDay last_day_ = static_cast<core::SimDay>(-1);
  float reputation_open_ = 50.0F;
  bool opened_ = false;
  std::ostringstream lines_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_REPUTATION_TALLY_H_
