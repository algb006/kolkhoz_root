/// @file advance_write_off_tally.h
/// @brief Every firing of the advance's write-off at the second year-end
/// share-out (#170; FamilyRow::advance_turns, food.csv
/// `advance_write_off_turns`), printed by name: the year, the household, the
/// kilograms of grain equivalent, and their share of what the village
/// advanced that year.
///
/// WHY IT IS PRINTED AND NOT ONLY BOOKED (boss, 13 October 2026): the rule
/// fired 0 times in the canon pair's nine seeds and four years, was withdrawn
/// as a stub, and came back when population_curve's seed 1941 showed it fire
/// inside 33 years. A write-off over a tenth of the village's yearly advance
/// is a finding about that family's earning - a household that could not be
/// lifted above the norm in two years - and a sum in a book names nobody.
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
#include "core_common/family_state.h"
#include "core_common/quantities.h"
#include "core_common/world_state.h"

namespace run {

class AdvanceWriteOffTally {
 public:
  /// @brief Once a day, after the day's steps: a household whose debt fell to
  ///        nought on the day its count of share-outs rose was written off.
  void CountDay(const core::WorldState& today) {
    const std::uint32_t year = today.calendar.day / core::kDaysPerYear;
    if (year != year_) {
      // THE YEAR'S ROW (econ's bands, 13 October 2026): what the village
      // advanced, what came back, what the book wrote off, and the
      // households behind each - closed the morning the year turns.
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
    std::vector<Firing> candidates;
    core::Grams fell_today = 0;       // every fall of a debt today, repaid or written off
    std::uint32_t cleared_today = 0;  // debts that fell to nought today
    for (std::uint32_t row = 0; row < today.families.rows.size(); ++row) {
      const core::FamilyRow& family = today.families.rows[row];
      const std::uint32_t id = today.families.row_ids[row].value;
      const Seen seen{.debt = family.advance_debt,
                      .given = family.advance_given,
                      .turns = family.advance_turns};
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
        // A CANDIDATE, NOT YET A FIRING: the debt fell to nought on the day
        // the count of share-outs rose TO THE RULE'S NUMBER OR PAST IT. The
        // same morning's issue may have repaid it instead - the book below
        // says which.
        if (before->second.debt > 0 && seen.debt == 0 && seen.turns > before->second.turns &&
            seen.turns >= kWriteOffTurns) {
          candidates.push_back({.year = year,
                                .family = id,
                                .grams = before->second.debt,
                                .village_advance = advanced_last_year_});
        }
      } else if (seen.given > 0) {
        advanced_this_year_ += seen.given;
        advanced_ids_.insert(id);
      }
      now.emplace(id, seen);
    }
    seen_.swap(now);
    // THE BOOK IS THE JUDGE (0.37.250). The first form of this tally called
    // every such fall a firing and printed 2 858 of them over sixteen
    // villages - among them a household at its FIRST share-out, which the
    // rule cannot touch: a debt the share-out's own issue repaid that morning
    // falls to nought on the same day. What the rule wrote off is the rise of
    // YearLedger::advance_written_off_turns today, across the book's closing.
    const core::YearLedger& open = today.ledger.current;
    const core::YearLedger& closed = today.ledger.closed;
    core::Grams written = 0;
    if (closed.year != closed_year_) {
      written = (closed.advance_written_off_turns - open_before_) + open.advance_written_off_turns;
    } else {
      written = open.advance_written_off_turns - open_before_;
    }
    closed_year_ = closed.year;
    open_before_ = open.advance_written_off_turns;
    written_ += written;
    core::Grams named = 0;
    for (const Firing& candidate : candidates) {
      named += candidate.grams;
    }
    std::uint32_t written_households = 0;
    if (written > 0 && named == written) {
      firings_.insert(firings_.end(), candidates.begin(), candidates.end());
      written_households = static_cast<std::uint32_t>(candidates.size());
    } else if (written > 0) {
      // The book wrote off a sum the candidates do not add up to: some of
      // them were repaid that morning. WHICH - by the sum itself: the one
      // subset of the candidates that adds up to the book's figure, when
      // there is exactly one (econ's 113 t on 91 days, 13 October 2026).
      // More than one, none, or too many candidates to try: said, not
      // guessed at.
      const std::vector<Firing> subset = TheOneSubsetAddingTo(candidates, written);
      if (!subset.empty()) {
        firings_.insert(firings_.end(), subset.begin(), subset.end());
        written_households = static_cast<std::uint32_t>(subset.size());
        ++attributed_by_sum_;
      } else {
        unnamed_.push_back({.year = year,
                            .family = static_cast<std::uint32_t>(candidates.size()),
                            .grams = written,
                            .village_advance = advanced_last_year_});
      }
    }
    // The year's row: a share-out's morning belongs to the year it CLOSES -
    // the row still open is that year's until the year's first evening is
    // counted, so the turn's falls are booked one row late on purpose and
    // said so in the print.
    open_row_.written += written;
    open_row_.repaid += fell_today > written ? fell_today - written : 0;
    open_row_.written_households += written_households;
    open_row_.cleared_by_repaying +=
        cleared_today > written_households ? cleared_today - written_households : 0U;
  }

  /// @brief One line a firing; and one line saying there was none, so a
  ///        reader does not take silence for a print that did not run.
  void Print(std::string_view run_name, std::uint64_t seed) const {
    // The book's own total first: the households named below add up to it,
    // less the days said apart.
    std::cout << run_name << " (seed " << seed
              << "): the advance's write-off at the second share-out - the book wrote off "
              << static_cast<double>(written_) / 1000.0 << " kg of grain equivalent in all; "
              << firings_.size() << " households named, " << unnamed_.size()
              << " days not attributed\n";
    for (const Firing& day : unnamed_) {
      std::cout << run_name << " (seed " << seed
                << "): WRITTEN OFF AND NOT ATTRIBUTED - at the turn "
                << "into year " << day.year + 1U << " the book wrote off "
                << static_cast<double>(day.grams) / 1000.0 << " kg, and the " << day.family
                << " households whose debt fell to nought that day do not add up to it (some "
                   "were repaid by the share-out's own issue)\n";
    }
    // THE YEARS (econ's bands): one line a closed year. A share-out's morning
    // is the FIRST day of the next year, so what it repaid or wrote off stands
    // in the row of the year it opens - `year` below is the calendar year the
    // falls happened in, 1-based.
    std::cout << run_name << " (seed " << seed << "): " << attributed_by_sum_
              << " days attributed by the one subset of households adding up to the book's sum\n";
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
    if (firings_.empty() && unnamed_.empty()) {
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
                << " kg of grain equivalent";
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
    std::uint8_t turns = 0;
  };

  struct Firing {
    std::uint32_t year = 0;
    std::uint32_t family = 0;
    core::Grams grams = 0;
    core::Grams village_advance = 0;
  };

  /// One closed year of the village's advances.
  struct YearRow {
    std::uint32_t year = 0;
    core::Grams advanced = 0;
    core::Grams repaid = 0;   ///< Debts' falls of the year less what the book wrote off.
    core::Grams written = 0;  ///< The book's column, as it rose in the year.
    std::uint32_t advanced_households = 0;  ///< Households given an advance in the year.
    std::uint32_t cleared_by_repaying = 0;  ///< Debts that fell to nought and were not written off.
    std::uint32_t written_households = 0;   ///< Households named as written off.
    std::uint32_t owing_at_end = 0;  ///< Households with a debt open on the year's last evening.
  };

  /// The one subset of `candidates` whose debts add up to `written`; empty
  /// when there is none, more than one, or more candidates than can be tried.
  static std::vector<Firing> TheOneSubsetAddingTo(const std::vector<Firing>& candidates,
                                                  core::Grams written) {
    constexpr std::size_t kMostTried = 20;
    std::vector<Firing> found;
    if (candidates.empty() || candidates.size() > kMostTried) {
      return found;
    }
    std::uint32_t matches = 0;
    std::uint32_t match_mask = 0;
    const std::uint32_t masks = 1U << candidates.size();
    for (std::uint32_t mask = 1; mask < masks && matches < 2; ++mask) {
      core::Grams sum = 0;
      for (std::size_t index = 0; index < candidates.size(); ++index) {
        sum += (mask >> index) & 1U ? candidates[index].grams : 0;
      }
      if (sum == written) {
        ++matches;
        match_mask = mask;
      }
    }
    if (matches != 1) {
      return found;
    }
    for (std::size_t index = 0; index < candidates.size(); ++index) {
      if ((match_mask >> index) & 1U) {
        found.push_back(candidates[index]);
      }
    }
    return found;
  }

  /// food.csv `advance_write_off_turns` as shipped: the share-out the rule
  /// fires at. A candidate below it was repaid, not written off.
  static constexpr std::uint8_t kWriteOffTurns = 2;

  std::vector<YearRow> years_;
  YearRow open_row_;
  std::unordered_set<std::uint32_t> advanced_ids_;
  std::uint32_t attributed_by_sum_ = 0;

  std::unordered_map<std::uint32_t, Seen> seen_;
  std::vector<Firing> firings_;
  /// Days the book wrote off a sum the candidates did not add up to: `family`
  /// holds the candidates' count, `grams` the book's sum.
  std::vector<Firing> unnamed_;
  core::Grams written_ = 0;      ///< The book's column, summed over the run.
  core::Grams open_before_ = 0;  ///< The open book's column last evening.
  std::uint16_t closed_year_ = 0;
  std::uint32_t year_ = 0;
  core::Grams advanced_this_year_ = 0;
  core::Grams advanced_last_year_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_ADVANCE_WRITE_OFF_TALLY_H_
