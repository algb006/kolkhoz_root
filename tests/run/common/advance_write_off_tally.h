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
      advanced_last_year_ = advanced_this_year_;
      advanced_this_year_ = 0;
      year_ = year;
    }
    std::unordered_map<std::uint32_t, Seen> now;
    now.reserve(today.families.rows.size());
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
        }
        if (before->second.debt > 0 && seen.debt == 0 && seen.turns > before->second.turns) {
          // The share-out is the ENDING year's last business: its village's
          // advance is last year's, counted to the evening before.
          firings_.push_back({.year = year,
                              .family = id,
                              .grams = before->second.debt,
                              .village_advance = advanced_last_year_});
        }
      } else if (seen.given > 0) {
        advanced_this_year_ += seen.given;
      }
      now.emplace(id, seen);
    }
    seen_.swap(now);
  }

  /// @brief One line a firing; and one line saying there was none, so a
  ///        reader does not take silence for a print that did not run.
  void Print(std::string_view run_name, std::uint64_t seed) const {
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
  std::unordered_map<std::uint32_t, Seen> seen_;
  std::vector<Firing> firings_;
  std::uint32_t year_ = 0;
  core::Grams advanced_this_year_ = 0;
  core::Grams advanced_last_year_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_ADVANCE_WRITE_OFF_TALLY_H_
