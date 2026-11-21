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
#include <vector>

#include "core_common/calendar.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

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

/// @brief #1946 (econ econ-boss-1946-overfulfil [1]): OVER1946 a village-year y1-y16 - the
///        plan on the year's last day, the state the turn's StepReputationBeyondPlan reads
///        (district_reputation.cpp OwnOverfulfilGrainTonnes): every resource with a due, its
///        due and delivered (t); the district's lent goods this year (kg, gross); and, off the
///        closed book at the turn, the core's own over-plan figure and the tiers it reached.
///        The netting (world_params limit_overfulfil_shortfall_factor) is printed with it.
class PlanOverTally {
 public:
  explicit PlanOverTally(const core::ITableSet& tables) {
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      for (std::uint32_t row = 0; row < resources->RowCount(); ++row) {
        keys_.emplace_back(resources->CellText(row, 0));
      }
    }
    if (const core::ITable* const world = tables.FindTable("world_params")) {
      const std::uint32_t row = world->FindRowByKey("limit_overfulfil_shortfall_factor");
      const std::uint32_t column = world->FindColumn("value");
      if (row != core::kNoTableRow && column != core::kNoTableColumn) {
        factor_ = std::string(world->CellText(row, column));
      }
    }
  }

  /// @brief Once a day: the plan on the year's last day, the book at the turn.
  void CountDay(const core::WorldState& world) {
    const core::SimDay day = world.calendar.day;
    if (day == last_day_) {
      return;
    }
    last_day_ = day;
    const std::uint32_t year = (day / core::kDaysPerYear) + 1U;
    if (day % core::kDaysPerYear == core::kDaysPerYear - 1U && year <= kYears) {
      std::ostringstream plan;
      for (std::size_t r = 0; r < world.plan.due.size(); ++r) {
        if (world.plan.due[r] <= 0) {
          continue;
        }
        const core::Grams delivered = r < world.plan.delivered.size() ? world.plan.delivered[r] : 0;
        plan << ' ' << (r < keys_.size() ? keys_[r] : std::to_string(r)) << ':'
             << static_cast<double>(world.plan.due[r]) / 1.0e6 << ':'
             << static_cast<double>(delivered) / 1.0e6;
      }
      double lent_kg = 0.0;
      for (const core::Grams grams : world.ledger.current.goods_loan_taken) {
        lent_kg += static_cast<double>(grams) / 1000.0;
      }
      pending_ = "year " + std::to_string(year) + " due_t:delivered_t" +
                 (plan.str().empty() ? std::string(" -") : plan.str()) + " lent_kg " +
                 std::to_string(static_cast<long>(lent_kg)) + " shortfall_factor " + factor_;
      pending_year_ = year;
    }
    if (day % core::kDaysPerYear == 0 && pending_year_ != 0 && year == pending_year_ + 1U) {
      const core::YearLedger& book = world.ledger.closed;
      lines_ << pending_ << " own_over_t " << book.reputation_own_overfulfil_t << " tiers "
             << book.reputation_tiers_reached << '\n';
      pending_year_ = 0;
    }
  }

  /// @brief The village's lines.
  void Print(std::ostream& out, std::uint64_t seed) const {
    std::istringstream lines(lines_.str());
    std::string line;
    while (std::getline(lines, line)) {
      out << "population_curve: OVER1946 seed " << seed << ' ' << line << '\n';
    }
  }

 private:
  static constexpr std::uint32_t kYears = 16;
  std::vector<std::string> keys_;
  std::string factor_ = "?";
  core::SimDay last_day_ = static_cast<core::SimDay>(-1);
  std::string pending_;
  std::uint32_t pending_year_ = 0;
  std::ostringstream lines_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_REPUTATION_TALLY_H_
