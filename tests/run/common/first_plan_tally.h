/// @file
/// @brief #1750's confirming print: the first plan's rye against the start's rye, a village
///        (econ, econ/manual/proposals/first-rye-plan-is-the-seed-2026-11-15.md: the start rye of
///        ~8 t is the seed for ~44 ha of September winter rye, so a village that sows rye for
///        year 2 cannot also deliver year 1's plan).
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// THE PRINT, a village, read from the CLOSED book on year 2's first day (#1580's ground):
/// «FIRSTPLAN1750 seed S y 1 rye_seed_t A rye_eaten_t B rye_at_turn_t C rye_due_t D
///  rye_delivered_t E failed F» - the year's rye sown as seed, eaten by the families, held in
///  every store at the turn (after the plan's delivery), the plan's rye due and delivered, and
///  whether the plan was failed (the verdict's rising edge on the turn).

#ifndef TESTS_RUN_COMMON_FIRST_PLAN_TALLY_H_
#define TESTS_RUN_COMMON_FIRST_PLAN_TALLY_H_

#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>

#include "core_common/calendar.h"
#include "core_common/ledger_state.h"
#include "core_common/quantities.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

class FirstPlanTally {
 public:
  explicit FirstPlanTally(const core::ITableSet& tables) {
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      rye_ = resources->FindRowByKey("rye");
    }
  }

  /// @brief Once a day after the day's steps.
  void CountDayEnd(const core::WorldState& world) {
    if (done_ || world.calendar.day != core::kDaysPerYear) {
      return;  // year 2's first day: the closed book is year 1's
    }
    done_ = true;
    const core::YearLedger& book = world.ledger.closed;
    core::Grams held = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      held += rye_ < unit.stock.size() ? unit.stock[rye_] : 0;
    }
    line_ << " y " << book.year << " rye_seed_t " << Tonnes(Of(book.seed)) << " rye_eaten_t "
          << Tonnes(Of(book.eaten)) << " rye_at_turn_t " << Tonnes(held) << " rye_due_t "
          << Tonnes(Of(book.plan_due)) << " rye_delivered_t " << Tonnes(Of(book.plan_delivered))
          << " failed " << (world.plan.failed_years_in_a_row > 0 ? 1 : 0);
  }

  /// @brief The village's line.
  void Print(std::ostream& out, std::uint64_t seed) const {
    const std::string text = line_.str();
    out << "population_curve: FIRSTPLAN1750 seed " << seed << (text.empty() ? " -" : text) << '\n';
  }

 private:
  core::Grams Of(const core::ResourceAmounts& amounts) const {
    return rye_ < amounts.size() ? amounts[rye_] : 0;
  }

  static double Tonnes(core::Grams grams) {
    return static_cast<double>(static_cast<std::int64_t>(grams / 1000)) / 1000.0;
  }

  std::uint32_t rye_ = core::kNoTableRow;
  bool done_ = false;
  std::ostringstream line_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_FIRST_PLAN_TALLY_H_
