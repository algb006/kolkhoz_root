/// @file
/// @brief PLANY2072 (econ's #2072 gate, econ-boss-2072-trails-gates-2026-11-23 [13]; boss 25
///        Nov): the district's verdict on each year that closed, and for every position short of
///        its due the year's own book of that produce - so a miss names its cause. Read on each
///        year's first day: the closed book (YearLedger) and the verdict the turn just wrote
///        (PlanState::last_verdict), and the heaps still lying on the fields that morning.
///        A short position's CAUSE, the first that holds:
///          hauling  - its heaps still lying on the fields cover the shortfall;
///          lost     - the field's losses (no room, snow, flood) cover it;
///          harvest  - what was reaped (a herd's produce for milk) is below the due;
///          drawn    - it was reaped and carted, and the year's draws took it (issued, ration,
///                     advance, eaten from the stores, stolen, spoiled, samogon, seed, feed,
///                     bartered, seized, processed - each printed).
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.
///
/// GROUND, printed: the cause is this tally's reading of the book, by the order above; every
/// number it read is printed beside it, so another order can be read off the line.

#ifndef TESTS_RUN_COMMON_PLAN_YEAR_TALLY_H_
#define TESTS_RUN_COMMON_PLAN_YEAR_TALLY_H_

#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/land_state.h"
#include "core_common/ledger_state.h"
#include "core_common/quantities.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

class PlanYearTally {
 public:
  explicit PlanYearTally(const core::ITableSet& tables) {
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      const std::uint32_t key = resources->FindColumn("key");
      for (std::uint32_t row = 0; row < resources->RowCount(); ++row) {
        names_.emplace_back(resources->CellText(row, key));
      }
    }
  }

  /// @brief Once a day, after the day's step.
  void CountDay(const core::WorldState& world) {
    if (world.calendar.day == 0 || world.calendar.day % core::kDaysPerYear != 0) {
      return;
    }
    const std::uint32_t year = world.calendar.day / core::kDaysPerYear;  // the year that closed
    const core::YearLedger& book = world.ledger.closed;
    std::ostringstream line;
    line << "year " << year << " verdict "
         << (world.plan.last_verdict == core::PlanVerdict::kMet      ? "met"
             : world.plan.last_verdict == core::PlanVerdict::kFailed ? "failed"
                                                                     : "none");
    std::uint32_t short_positions = 0;
    for (std::size_t at = 0; at < book.plan_due.size(); ++at) {
      const core::Grams due = book.plan_due[at];
      const core::Grams delivered = Of(book.plan_delivered, at);
      if (due <= 0 || delivered >= due) {
        continue;
      }
      ++short_positions;
      const core::Grams shortfall = due - delivered;
      core::Grams lying = 0;
      for (const core::FieldRow& field : world.fields.rows) {
        if (field.reaped_grams > 0 && field.reaped_resource.value == at) {
          lying += field.reaped_grams;
        }
      }
      const core::Grams lost =
          Of(book.lost_no_room, at) + Of(book.lost_to_snow, at) + Of(book.lost_to_flood, at);
      const core::Grams reaped = Of(book.harvest, at) + Of(book.herd_produce, at);
      const char* cause = lying >= shortfall  ? "hauling"
                          : lost >= shortfall ? "lost"
                          : reaped < due      ? "harvest"
                                              : "drawn";
      line << " short " << NameOf(at) << "(due/delivered/short_t " << Tonnes(due) << '/'
           << Tonnes(delivered) << '/' << Tonnes(shortfall) << " cause " << cause << " reaped "
           << Tonnes(reaped) << " hauled " << Tonnes(Of(book.hauled_to_stores, at)) << " lying "
           << Tonnes(lying) << " lost " << Tonnes(lost) << " issued " << Tonnes(Of(book.issued, at))
           << " ration " << Tonnes(Of(book.ration, at)) << " advance "
           << Tonnes(Of(book.advance, at)) << " eaten " << Tonnes(Of(book.eaten, at)) << " stolen "
           << Tonnes(Of(book.stolen, at)) << " spoiled " << Tonnes(Of(book.spoiled, at))
           << " samogon " << Tonnes(Of(book.samogon_paid, at)) << " seed "
           << Tonnes(Of(book.seed, at)) << " feed " << Tonnes(Of(book.feed, at)) << " bartered "
           << Tonnes(Of(book.bartered, at)) << " seized " << Tonnes(Of(book.seized, at))
           << " processed " << Tonnes(Of(book.processed, at)) << ')';
    }
    line << " short_positions " << short_positions;
    lines_.push_back(line.str());
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    for (const std::string& line : lines_) {
      out << "population_curve: PLANY2072 seed " << seed
          << " ground=closed_book+verdict_at_the_turn;cause_order=hauling,lost,harvest,drawn "
          << line << '\n';
    }
  }

 private:
  static core::Grams Of(const core::ResourceAmounts& amounts, std::size_t at) {
    return at < amounts.size() ? amounts[at] : 0;
  }

  static double Tonnes(core::Grams grams) { return static_cast<double>(grams) / 1.0e6; }

  std::string NameOf(std::size_t at) const {
    return at < names_.size() ? names_[at] : "resource" + std::to_string(at);
  }

  std::vector<std::string> names_;
  std::vector<std::string> lines_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_PLAN_YEAR_TALLY_H_
