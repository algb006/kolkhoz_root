/// @file
/// @brief What opened Epoch II and when, village by village: the day and the
/// population it opened at, the social objects standing then, the first day
/// each gate stood met and which gate met LAST, and the two indices of years
/// 1-6 with their components.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY (boss-core-epoch1-queue [28]-[29]; econ, boss-econ-pop14-threshold
/// [2]-[4]): «население года 14 ≥ 380» was born a demography check with no
/// farm behind it, and no line of the design asks a population of a year.
/// The design's own pair is the day the era opens and the village it opens
/// with. None of the eight gates depends on the village's size; which one
/// holds the door is the question, and a print that says the year alone
/// cannot answer it.
///
/// THE GATES ARE ASKED AT THE CORE'S DOOR (TransitionPolicy::ConditionsMet,
/// TransitionRefusal with every other gate forced open): the seven the door
/// reads — the indices held together three years, then the six blocks. The
/// two indices are ALSO followed apart (econ's columns): each at its own
/// threshold (era_readiness.h) three turns in a row; the door itself reads
/// them together.

#ifndef TESTS_RUN_COMMON_TRANSITION_TALLY_H_
#define TESTS_RUN_COMMON_TRANSITION_TALLY_H_

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/era_readiness.h"
#include "transition_policy.h"

namespace run {

class TransitionTally {
 public:
  /// The years whose indices are printed with their components, by default
  /// (boss-core-epoch1-queue [29]: years 1-6); econ's branch asks all.
  static constexpr std::size_t kYearsDecomposedDefault = 6;

  /// One closed year as the turn scored it.
  struct YearScore {
    float economic = 0.0F;
    float social = 0.0F;
    /// plan, winter stocks, mechanisation (traction), funds; satisfaction,
    /// kolkhoz effort, social objects, demography (ReadinessState's order).
    std::array<float, 8> components = {};
    float satisfaction_stub_points = 0.0F;
    /// The residents on the turn's day, and the door's seven gates as met
    /// then (TransitionPolicy::ConditionsMet's order).
    std::uint32_t residents = 0;
    std::array<bool, TransitionPolicy::kConditions> gates = {};
    bool scored = false;
  };

  /// @param years_decomposed Closed years printed with their components.
  explicit TransitionTally(const core::ITableSet& tables,
                           std::size_t years_decomposed = kYearsDecomposedDefault)
      : catalog_(core::ReadReadinessCatalog(tables, core::Epoch::kOne)), years_(years_decomposed) {}

  /// @brief One day, after the day's steps and the chairman's orders.
  void CountDay(const TransitionPolicy& transition, const core::WorldState& world) {
    const core::SimDay day = world.calendar.day;
    ScoreTurn(transition, world, day);
    if (world.epoch != core::Epoch::kOne) {
      if (opened_day_ < 0) {
        opened_day_ = static_cast<std::int64_t>(day);
        opened_population_ = static_cast<std::uint32_t>(world.residents.rows.size());
        opened_social_standing_ = SocialStanding(world);
        // THE GATE THAT MET LAST: of the seven the door read, the one whose
        // latest turn to "met" came latest (ties all named).
        std::int64_t latest = -1;
        for (const std::int64_t became : became_met_day_) {
          latest = std::max(latest, became);
        }
        for (std::size_t gate = 0; gate < kGates; ++gate) {
          met_last_[gate] = became_met_day_[gate] == latest;
        }
      }
      return;
    }
    const auto met = transition.ConditionsMet(world);
    for (std::size_t gate = 0; gate < kGates; ++gate) {
      if (met[gate] && first_met_day_[gate] < 0) {
        first_met_day_[gate] = static_cast<std::int64_t>(day);
      }
      if (met[gate] && !met_yesterday_[gate]) {
        became_met_day_[gate] = static_cast<std::int64_t>(day);
      }
      met_yesterday_[gate] = met[gate];
    }
  }

  /// @brief The village's lines, headed by its seed.
  void Print(const char* run_name, std::uint64_t seed) const {
    const auto year_day = [](std::int64_t day) {
      if (day < 0) {
        return std::string("never");
      }
      return "y" + std::to_string(day / core::kDaysPerYear + 1) + " d" +
             std::to_string(day % core::kDaysPerYear);
    };
    std::cout << run_name << ": seed " << seed << " Epoch II ";
    if (opened_day_ < 0) {
      std::cout << "NEVER in the run";
    } else {
      std::cout << "opened " << year_day(opened_day_) << " at " << opened_population_
                << " residents, social objects standing " << opened_social_standing_ << " of "
                << catalog_.social_objects.size();
    }
    std::cout << "\n  gates, first met (campaign year, day of year)"
              << (opened_day_ < 0 ? "" : ", * met last before the opening") << ":";
    for (std::size_t gate = 0; gate < kGates; ++gate) {
      std::cout << "\n    " << kGateNames[gate] << ' ' << year_day(first_met_day_[gate])
                << (opened_day_ >= 0 && met_last_[gate] ? " *" : "");
    }
    std::cout << "\n    экономика ≥" << core::kEconomicThreshold << " ×"
              << static_cast<int>(core::kIndexYearsRequired) << " alone "
              << year_day(economic_met_day_) << "; общество ≥" << core::kSocialThreshold << " ×"
              << static_cast<int>(core::kIndexYearsRequired) << " alone "
              << year_day(social_met_day_) << '\n';
    std::cout << "  indices by closed year — economic (plan, stocks, traction, funds) / social "
                 "(satisfaction [stub points], effort, social objects, demography); residents "
                 "at the turn; gates met then, "
              << kGateLetters << ':';
    for (std::size_t year = 0; year < years_.size(); ++year) {
      const YearScore& score = years_[year];
      if (!score.scored) {
        continue;
      }
      const auto& c = score.components;
      std::cout << "\n    year " << year + 1 << ": " << score.economic << " (" << c[0] << ", "
                << c[1] << ", " << c[2] << ", " << c[3] << ") / " << score.social << " (" << c[4]
                << " [" << score.satisfaction_stub_points << "], " << c[5] << ", " << c[6] << ", "
                << c[7] << "); " << score.residents << " residents; gates ";
      for (const bool met : score.gates) {
        std::cout << (met ? '+' : '-');
      }
    }
    std::cout << '\n';
  }

  /// The day Epoch II opened, -1 never.
  std::int64_t OpenedDay() const { return opened_day_; }

  /// The residents on that day (0 when it never opened).
  std::uint32_t OpenedPopulation() const { return opened_population_; }

 private:
  static constexpr std::size_t kGates = TransitionPolicy::kConditions;

  /// TransitionPolicy::ConditionsMet's order.
  static constexpr std::array<const char*, kGates> kGateNames = {"индексы вместе ×3",
                                                                 "своя тяга/база   ",
                                                                 "зимовка 2 года   ",
                                                                 "правление ≤1%    ",
                                                                 "разнообразие пищи",
                                                                 "4 соцобъекта из 6",
                                                                 "юниты на уровне  ",
                                                                 "жителей ≥ 380    "};

  /// The gates' order in a year's line, one mark each.
  static constexpr const char* kGateLetters =
      "indices/traction/wintering/office/food/social/units/residents";

  /// The turn scores the closed year after the books rotate (world.cpp): on
  /// the new year's first day, the readiness judges the year just closed.
  void ScoreTurn(const TransitionPolicy& transition,
                 const core::WorldState& world,
                 core::SimDay day) {
    const std::uint32_t year = day / core::kDaysPerYear;
    if (year == 0 || day % core::kDaysPerYear != 0 || year == last_scored_year_) {
      return;
    }
    last_scored_year_ = year;
    const core::ReadinessState& readiness = world.readiness;
    economic_run_ = readiness.economic_index >= core::kEconomicThreshold ? economic_run_ + 1U : 0U;
    social_run_ = readiness.social_index >= core::kSocialThreshold ? social_run_ + 1U : 0U;
    if (economic_run_ >= core::kIndexYearsRequired && economic_met_day_ < 0) {
      economic_met_day_ = static_cast<std::int64_t>(day);
    }
    if (social_run_ >= core::kIndexYearsRequired && social_met_day_ < 0) {
      social_met_day_ = static_cast<std::int64_t>(day);
    }
    const std::size_t closed = year - 1U;
    if (closed < years_.size()) {
      YearScore& score = years_[closed];
      score.economic = readiness.economic_index;
      score.social = readiness.social_index;
      score.components = {readiness.economy.plan.score,
                          readiness.economy.winter_stocks.score,
                          readiness.economy.mechanisation.score,
                          readiness.economy.funds.score,
                          readiness.society.satisfaction.score,
                          readiness.society.kolkhoz_effort.score,
                          readiness.society.social_objects.score,
                          readiness.society.demography.score};
      score.satisfaction_stub_points = readiness.satisfaction_stub_points;
      score.residents = static_cast<std::uint32_t>(world.residents.rows.size());
      // The gates as the door reads them in Epoch I; after the transition
      // the door answers every question "not eligible", so none stands met.
      if (world.epoch == core::Epoch::kOne) {
        score.gates = transition.ConditionsMet(world);
      }
      score.scored = true;
    }
  }

  /// The era's social types standing (level 1 or more, alive) — the block's
  /// own count (era_readiness.cpp, SocialObjectsStanding).
  std::uint32_t SocialStanding(const core::WorldState& world) const {
    std::uint32_t standing = 0;
    for (const core::UnitTypeId type : catalog_.social_objects) {
      bool stands = false;
      for (const core::UnitRow& unit : world.units.rows) {
        stands = stands || (unit.type.value == type.value && unit.level >= 1 && unit.dead == 0);
      }
      standing += stands ? 1U : 0U;
    }
    return standing;
  }

  core::ReadinessCatalog catalog_;
  std::array<std::int64_t, kGates> first_met_day_ = {-1, -1, -1, -1, -1, -1, -1, -1};
  std::array<std::int64_t, kGates> became_met_day_ = {-1, -1, -1, -1, -1, -1, -1, -1};
  std::array<bool, kGates> met_yesterday_ = {};
  std::array<bool, kGates> met_last_ = {};
  std::int64_t opened_day_ = -1;
  std::uint32_t opened_population_ = 0;
  std::uint32_t opened_social_standing_ = 0;
  std::uint32_t last_scored_year_ = 0;
  std::uint32_t economic_run_ = 0;
  std::uint32_t social_run_ = 0;
  std::int64_t economic_met_day_ = -1;
  std::int64_t social_met_day_ = -1;
  std::vector<YearScore> years_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_TRANSITION_TALLY_H_
