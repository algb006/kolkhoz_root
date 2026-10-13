/// @file advance_write_off_tally.h
/// @brief Every write-off of a new household's advance at the year-end
/// share-out (#170; FamilyRow::advance_turns, food.csv
/// `advance_write_off_turns`), printed by name - the year, the household, the
/// kilograms of grain equivalent, the household's age in game days, the
/// share of what the village advanced that year - and the village's advances
/// by year: advanced, repaid, written off, and the households behind each.
///
/// WHY IT IS PRINTED AND NOT ONLY BOOKED (boss, 13 October 2026): the rule
/// fired 0 times in the canon pair's nine seeds and four years, was withdrawn
/// as a stub, and came back when population_curve's seed 1941 showed it fire
/// inside 33 years. From year 12 about half of what a village advances is
/// written off, and a sum in a book names nobody.
///
/// THE HOUSEHOLD COMES FROM THE CORE'S OWN EVENT (EventKind::
/// kAdvanceWrittenOff, 0.37.251). Until then this tally GUESSED it from
/// outside - a debt that fell to nought on a share-out's morning - and could
/// not tell a write-off from a debt the same morning's issue repaid: its
/// first form printed 2 858 «firings», its second left 110 t of 314 on 88
/// days at nobody's door. The book's column stays the judge: the events of a
/// run must add up to it, and a run where they do not says so.
/// @threading SINGLE_THREADED

#ifndef TESTS_RUN_COMMON_ADVANCE_WRITE_OFF_TALLY_H_
#define TESTS_RUN_COMMON_ADVANCE_WRITE_OFF_TALLY_H_

#include <cstdint>
#include <iostream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/event_state.h"
#include "core_common/family_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"

namespace run {

class AdvanceWriteOffTally {
 public:
  /// @brief After EVERY step: the step's write-off events, each with its
  ///        household still standing in the world to be asked its age.
  void CountStep(const core::WorldState& now) {
    for (const core::SimEvent& event : now.step_events) {
      if (event.kind != core::EventKind::kAdvanceWrittenOff) {
        continue;
      }
      const std::uint32_t row = core::FindRow(now.families, event.family);
      Firing firing;
      // The share-out's morning is the first day of the next year: the debt
      // was the year's that just ended.
      firing.year = now.calendar.day / core::kDaysPerYear;
      firing.family = event.family.value;
      firing.grams = event.amount;
      firing.village_advance = advanced_this_year_ > 0 ? advanced_this_year_ : advanced_last_year_;
      firing.age_days = row != core::kNoRow
                            ? static_cast<std::int64_t>(now.calendar.day) -
                                  static_cast<std::int64_t>(now.families.rows[row].formed_day)
                            : -1;
      firings_.push_back(firing);
      named_ += event.amount;
      open_row_.written += event.amount;
      ++open_row_.written_households;
      written_today_ += event.amount;
      ++written_households_today_;
    }
  }

  /// @brief Once a day, after the day's steps: what was advanced and what
  ///        fell, and the year's row when the year turns.
  void CountDay(const core::WorldState& today) {
    const std::uint32_t year = today.calendar.day / core::kDaysPerYear;
    if (year != year_) {
      // THE YEAR'S ROW (econ's bands, 13 October 2026) - closed the morning
      // the year turns.
      YearRow row = open_row_;
      row.year = year_;
      row.advanced = advanced_this_year_;
      for (const auto& [id, seen] : seen_) {
        row.owing_at_end += seen.debt > 0 ? 1U : 0U;
      }
      row.advanced_households = static_cast<std::uint32_t>(advanced_ids_.size());
      years_.push_back(row);
      open_row_ = YearRow{};
      advanced_ids_.clear();
      advanced_last_year_ = advanced_this_year_;
      advanced_this_year_ = 0;
      year_ = year;
    }
    std::unordered_map<std::uint32_t, Seen> now;
    now.reserve(today.families.rows.size());
    core::Grams fell_today = 0;       // every fall of a debt today, repaid or written off
    std::uint32_t cleared_today = 0;  // debts that fell to nought today
    for (std::uint32_t row = 0; row < today.families.rows.size(); ++row) {
      const core::FamilyRow& family = today.families.rows[row];
      const std::uint32_t id = today.families.row_ids[row].value;
      const Seen seen{.debt = family.advance_debt, .given = family.advance_given};
      const auto before = seen_.find(id);
      if (before != seen_.end()) {
        if (seen.given > before->second.given) {
          advanced_this_year_ += seen.given - before->second.given;
          advanced_ids_.insert(id);
        }
        if (seen.debt < before->second.debt) {
          fell_today += before->second.debt - seen.debt;
          cleared_today += seen.debt == 0 ? 1U : 0U;
        }
      } else if (seen.given > 0) {
        advanced_this_year_ += seen.given;
        advanced_ids_.insert(id);
      }
      now.emplace(id, seen);
    }
    seen_.swap(now);
    // The book's column, summed over the run across each closing - the judge
    // the events are read against in Print.
    const core::YearLedger& open = today.ledger.current;
    const core::YearLedger& closed = today.ledger.closed;
    if (closed.year != closed_year_) {
      booked_ += (closed.advance_written_off_turns - open_before_) + open.advance_written_off_turns;
    } else {
      booked_ += open.advance_written_off_turns - open_before_;
    }
    closed_year_ = closed.year;
    open_before_ = open.advance_written_off_turns;
    // What fell today and was not written off was repaid. (A share-out's
    // morning is the first day of the year it opens: its falls stand in that
    // year's row, the year AFTER the advance was given.)
    open_row_.repaid += fell_today > written_today_ ? fell_today - written_today_ : 0;
    open_row_.cleared_by_repaying +=
        cleared_today > written_households_today_ ? cleared_today - written_households_today_ : 0U;
    written_today_ = 0;
    written_households_today_ = 0;
  }

  /// @brief The years, then one line a write-off; and one line saying there
  ///        was none, so a reader does not take silence for a print that did
  ///        not run.
  void Print(std::string_view run_name, std::uint64_t seed) const {
    std::cout << run_name << " (seed " << seed
              << "): the advance's write-off at the second share-out - the book wrote off "
              << static_cast<double>(booked_) / 1000.0 << " kg of grain equivalent in all; "
              << firings_.size() << " households named by the core's event, "
              << static_cast<double>(named_) / 1000.0 << " kg"
              << (named_ == booked_ ? " - the events add up to the book"
                                    : " - THE EVENTS DO NOT ADD UP TO THE BOOK")
              << '\n';
    // `year` is the calendar year the falls happened in, 1-based; the run's
    // last, unclosed year has no row.
    for (const YearRow& row : years_) {
      std::cout << "AY,seed=" << seed << ",year=" << row.year + 1U
                << ",advanced_kg=" << static_cast<double>(row.advanced) / 1000.0
                << ",repaid_kg=" << static_cast<double>(row.repaid) / 1000.0
                << ",written_kg=" << static_cast<double>(row.written) / 1000.0
                << ",advanced_households=" << row.advanced_households
                << ",cleared_by_repaying=" << row.cleared_by_repaying
                << ",written_households=" << row.written_households
                << ",owing_at_end=" << row.owing_at_end << '\n';
    }
    if (firings_.empty()) {
      std::cout << run_name << " (seed " << seed
                << "): the advance's write-off at the second share-out fired 0 times\n";
      return;
    }
    for (const Firing& firing : firings_) {
      const double kg = static_cast<double>(firing.grams) / 1000.0;
      const double village_kg = static_cast<double>(firing.village_advance) / 1000.0;
      std::cout << run_name << " (seed " << seed
                << "): ADVANCE WRITTEN OFF AT THE SECOND SHARE-OUT - at the turn into year "
                << firing.year + 1U << ", household " << firing.family << ", " << kg
                << " kg of grain equivalent, the household " << firing.age_days << " game days old";
      if (firing.village_advance > 0) {
        const double share = 100.0 * kg / village_kg;
        std::cout << " = " << share << " % of the " << village_kg
                  << " kg the village advanced in the year ending"
                  << (share > 10.0 ? " - OVER A TENTH: a household two years under the norm" : "");
      } else {
        std::cout << "; the village advanced nothing in the year ending";
      }
      std::cout << '\n';
    }
  }

  std::size_t Firings() const { return firings_.size(); }

 private:
  struct Seen {
    core::Grams debt = 0;
    core::Grams given = 0;
  };

  struct Firing {
    std::uint32_t year = 0;
    std::uint32_t family = 0;
    core::Grams grams = 0;
    core::Grams village_advance = 0;
    std::int64_t age_days =
        -1;  ///< The event's day less FamilyRow::formed_day; -1: the row was gone.
  };

  /// One closed year of the village's advances.
  struct YearRow {
    std::uint32_t year = 0;
    core::Grams advanced = 0;
    core::Grams repaid = 0;   ///< Debts' falls of the year less what the events wrote off.
    core::Grams written = 0;  ///< The events' grams of the year.
    std::uint32_t advanced_households = 0;  ///< Households given an advance in the year.
    std::uint32_t cleared_by_repaying = 0;  ///< Debts that fell to nought and were not written off.
    std::uint32_t written_households = 0;   ///< Households the events named.
    std::uint32_t owing_at_end = 0;  ///< Households with a debt open on the year's last evening.
  };

  std::unordered_map<std::uint32_t, Seen> seen_;
  std::vector<Firing> firings_;
  std::vector<YearRow> years_;
  YearRow open_row_;
  std::unordered_set<std::uint32_t> advanced_ids_;
  core::Grams named_ = 0;        ///< The events' grams, summed over the run.
  core::Grams booked_ = 0;       ///< The book's column, summed over the run.
  core::Grams open_before_ = 0;  ///< The open book's column last evening.
  core::Grams written_today_ = 0;
  std::uint32_t written_households_today_ = 0;
  std::uint16_t closed_year_ = 0;
  std::uint32_t year_ = 0;
  core::Grams advanced_this_year_ = 0;
  core::Grams advanced_last_year_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_ADVANCE_WRITE_OFF_TALLY_H_
