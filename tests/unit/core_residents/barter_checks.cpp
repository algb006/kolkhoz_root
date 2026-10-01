// The checks of the yards' exchange at the barter counter (barter_checks.h).

#include "barter_checks.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "../../common/fake_tables.h"
#include "barter.h"
#include "core_common/calendar.h"
#include "core_common/event_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "family_meal.h"
#include "food_config.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

constexpr float kSpeedup = 4.0F;
constexpr std::int32_t kAdultBirth = -192;

/// Three foods, chosen so the grain equivalent is a round figure: grain 1,
/// milk 0.2 (and gone in two days), potato 0.5.
core::FoodConfig ThreeFoods() {
  core::FoodConfig food;
  food.consumption.grain_reference_kcal_per_gram = 3.3F;
  food.resources.resize(3);
  food.resources[0] = {.kcal_per_gram = 3.3F, .category = core::FoodCategory::kBread};
  food.resources[1] = {.kcal_per_gram = 0.66F, .category = core::FoodCategory::kDairy};
  food.resources[2] = {.kcal_per_gram = 1.65F, .category = core::FoodCategory::kPotato};
  food.spoil_days = {0.0F, 2.0F, 120.0F};
  return food;
}

/// A yard of one adult with the pantry given in DAYS OF HIS NEED of each
/// food, so the checks below read in the rule's own unit.
core::FamilyId YardOf(core::WorldState& world,
                      const core::FoodConfig& food,
                      double need,
                      double grain_days,
                      double milk_days,
                      double potato_days) {
  const auto grams = [&](double days, std::size_t index) {
    const double equivalent = static_cast<double>(food.resources[index].kcal_per_gram) /
                              static_cast<double>(food.consumption.grain_reference_kcal_per_gram);
    return static_cast<core::Grams>(std::llround(days * need / equivalent));
  };
  core::FamilyRow family;
  family.pantry = {grams(grain_days, 0), grams(milk_days, 1), grams(potato_days, 2)};
  const core::FamilyId id = AppendRow(world.families, family);
  core::ResidentRow person;
  person.birth_day = kAdultBirth;
  person.family = id;
  AppendRow(world.residents, person);
  return id;
}

std::uint32_t EventsOf(const core::WorldState& world, core::EventKind kind) {
  std::uint32_t count = 0;
  for (const core::SimEvent& event : world.step_events) {
    count += event.kind == kind ? 1U : 0U;
  }
  return count;
}

/// The header's rule on four yards, every number worked by hand. D is one
/// adult's daily need.
///   A: grain 6 D, milk 5 D. Milk keeps two days: 3 D of it goes bad — (a).
///      Lacks potato: 4 days at a third of D = 1.333 D. What is left of the
///      3 D, 1.667 D, takes anything that keeps longer: grain.
///   B: grain 20 D: 8 D above the twelve days — (b). Lacks dairy, of milk no
///      more than two days' third: 0.667 D; lacks potato: 1.333 D.
///   C: potato 15 D: 3 D above the twelve. Lacks bread 1.333 D, dairy 0.667 D.
///   D: grain 0.5 D, milk 3 D — 3.5 days in all, HUNGRY: gives the 1 D of
///      milk that goes bad and takes the richest on offer, grain, 1 D.
/// Offered: milk 4 D, grain 8 D, potato 3 D. Claimed: milk 1.333 D, grain
/// 4 D, potato 2.667 D. The pairs settle at the smaller side:
///   A-B 0.5 D (A's milk to B 3 x 0.667 / 4; B's grain to A 1.667);
///   A-C 0.5 D (milk 0.5; potato 1.333);   B-C 1.333 D (grain, potato);
///   B-D 0.1667 D (grain 1; D's milk 1 x 0.667 / 4);   A-D and C-D nothing.
/// Each gram once on the giving side: 2 x 2.5 D = 5 D.
int CheckTheDryCount() {
  int failures = 0;
  const core::FoodConfig food = ThreeFoods();
  const core::BarterConfig config;  // 12 days, a third, 4 ahead, 4 hungry; 3 days, 3 yards, 5 %
  const double need = static_cast<double>(core::DailyNeedKilograms(
                          food.consumption,
                          core::BiologicalAgeYears(kSpeedup, kAdultBirth, core::SimDay{0}),
                          false)) *
                      static_cast<double>(core::kGramsPerKilogram);
  failures += Expect(need > 0.0, "dry count: the fixture's adult eats");

  core::WorldState world;
  YardOf(world, food, need, 6.0, 5.0, 0.0);
  YardOf(world, food, need, 20.0, 0.0, 0.0);
  YardOf(world, food, need, 0.0, 0.0, 15.0);
  YardOf(world, food, need, 0.5, 3.0, 0.0);
  const std::vector<core::FamilyRow> before = world.families.rows;

  core::RunBarterDryCount(config, food, kSpeedup, world);
  const double volume = static_cast<double>(world.barter.dry_equivalent);
  // A gram's rounding of each pantry moves the figure by a few grams; a
  // pair settled at the larger side would move it by 1.4 D.
  failures += Expect(std::abs(volume - (5.0 * need)) < 0.001 * need,
                     "dry count: the four yards would pass 5 days of one man's need between them");
  failures += Expect(world.barter.dry_givers == 4 && world.barter.dry_takers == 4,
                     "dry count: four yards have something to offer, four something to take");
  failures += Expect(world.barter.dry_days_in_row == 1 && world.barter.worth_starting_raised == 0 &&
                         EventsOf(world, core::EventKind::kBarterWorthStarting) == 0,
                     "dry count: one day met is not the fact");
  bool untouched = true;
  for (std::size_t row = 0; row < before.size(); ++row) {
    untouched = untouched && world.families.rows[row].pantry == before[row].pantry;
  }
  failures += Expect(untouched, "dry count: it moves nothing — every pantry is what it was");

  core::RunBarterDryCount(config, food, kSpeedup, world);
  failures += Expect(world.barter.dry_days_in_row == 2 && world.barter.worth_starting_raised == 0,
                     "dry count: two days running are not the fact either");
  core::RunBarterDryCount(config, food, kSpeedup, world);
  failures += Expect(world.barter.dry_days_in_row == 3 && world.barter.worth_starting_raised == 1 &&
                         EventsOf(world, core::EventKind::kBarterWorthStarting) == 1,
                     "dry count: the third day running raises «жителям есть что менять», once");
  bool carries_the_volume = false;
  for (const core::SimEvent& event : world.step_events) {
    carries_the_volume =
        carries_the_volume || (event.kind == core::EventKind::kBarterWorthStarting &&
                               event.amount == world.barter.dry_equivalent);
  }
  failures +=
      Expect(carries_the_volume, "dry count: the fact's event carries the day's equivalent");
  core::RunBarterDryCount(config, food, kSpeedup, world);
  failures += Expect(EventsOf(world, core::EventKind::kBarterWorthStarting) == 1,
                     "dry count: a fourth day does not raise the fact again");
  return failures;
}

/// The thresholds hold the fact back, each by itself.
int CheckTheFactsThresholds() {
  int failures = 0;
  const core::FoodConfig food = ThreeFoods();
  const double need = static_cast<double>(core::DailyNeedKilograms(
                          food.consumption,
                          core::BiologicalAgeYears(kSpeedup, kAdultBirth, core::SimDay{0}),
                          false)) *
                      static_cast<double>(core::kGramsPerKilogram);
  // Two yards that would exchange — grain above the twelve days against
  // potato above them — and a third that neither gives nor lacks.
  const auto village = [&](core::WorldState& world) {
    YardOf(world, food, need, 20.0, 0.0, 0.0);
    YardOf(world, food, need, 0.0, 0.0, 15.0);
  };
  {
    core::WorldState world;
    village(world);
    const core::BarterConfig config;  // three yards a side
    for (int day = 0; day < 5; ++day) {
      core::RunBarterDryCount(config, food, kSpeedup, world);
    }
    failures +=
        Expect(world.barter.dry_givers == 2 && world.barter.dry_equivalent > 0 &&
                   world.barter.dry_days_in_row == 0 && world.barter.worth_starting_raised == 0,
               "fact: two yards exchanging are fewer than three a side — no day is met");
  }
  {
    core::WorldState world;
    village(world);
    core::BarterConfig config;
    config.fact_yards_each_side = 2;
    config.fact_share_of_village_need = 1.0F;  // 2 D: the two pass 2 x 1.333 D, enough
    for (int day = 0; day < 3; ++day) {
      core::RunBarterDryCount(config, food, kSpeedup, world);
    }
    failures += Expect(world.barter.worth_starting_raised == 1,
                       "fact: with two a side asked and the volume above the share, it rises");
  }
  {
    core::WorldState world;
    village(world);
    core::BarterConfig config;
    config.fact_yards_each_side = 2;
    config.fact_share_of_village_need = 1.0F;
    core::RunBarterDryCount(config, food, kSpeedup, world);
    core::RunBarterDryCount(config, food, kSpeedup, world);
    // The second yard eats its surplus: nothing to pass on the third day.
    world.families.rows[1].pantry[2] = 0;
    core::RunBarterDryCount(config, food, kSpeedup, world);
    failures += Expect(world.barter.dry_days_in_row == 0 && world.barter.worth_starting_raised == 0,
                       "fact: a day not met breaks the run — days IN A ROW");
  }
  {
    // A lone yard with milk going bad: it offers, nobody takes, nothing
    // passes, and the two counts say so apart from the volume.
    core::WorldState world;
    YardOf(world, food, need, 6.0, 5.0, 0.0);
    const core::BarterConfig config;
    core::RunBarterDryCount(config, food, kSpeedup, world);
    failures += Expect(world.barter.dry_givers == 1 && world.barter.dry_takers == 0 &&
                           world.barter.dry_equivalent == 0,
                       "dry count: a lone yard offers and exchanges nothing");
  }
  return failures;
}

int CheckTheKnobs() {
  int failures = 0;
  const auto parse = [](const char* key, const char* value, core::BarterConfig& read) {
    const test::FakeTable world_params({"key", "value", "reader"}, {{key, value, "core"}});
    const test::FakeTableSet set({{"world_params", &world_params}});
    std::string trouble;
    return core::ParseBarterConfig(set, read, trouble);
  };
  core::BarterConfig read;
  failures += Expect(parse("barter_hour", "21", read) && read.hour == 21,
                     "knobs: the settlement's hour may be 21");
  failures += Expect(!parse("barter_hour", "22", read) && !parse("barter_hour", "23", read),
                     "knobs: 22 and 23 are refused — the hours the pantry flows are booked in");
  failures +=
      Expect(!parse("barter_lack_share_of_need", "1.5", read) &&
                 !parse("barter_fact_share_of_village_need", "-0.1", read) &&
                 !parse("barter_fact_days_in_row", "0", read) &&
                 !parse("barter_surplus_keep_days", "-1", read),
             "knobs: a share outside 0..1, no days in a row, days below nought are refused");
  failures += Expect(parse("barter_fact_yards_each_side", "5", read) &&
                         read.fact_yards_each_side == 5 && read.fact_days_in_row == 3,
                     "knobs: a key read lands, a key absent keeps its default");
  failures += Expect(core::BarterWorldParamKeys().size() == 9,
                     "knobs: nine keys are declared to the assembly");
  return failures;
}

}  // namespace

int CheckBarter() {
  return CheckTheDryCount() + CheckTheFactsThresholds() + CheckTheKnobs();
}
