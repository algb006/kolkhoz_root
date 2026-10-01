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

/// The fixture's day: 16, the first day of May. Its crops are reaped from
/// August (month 8, day 28), so a harvest is TWELVE days ahead.
constexpr core::SimDay kMayFirst = 16;

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

/// The defaults — a third of the need eaten of one perishable, a third
/// «lacking», 4 days ahead, 4 days hungry; 3 days, 3 yards, 5 % — with the
/// grain and the potato reaped from August and the milk from nothing.
core::BarterConfig Rules() {
  core::BarterConfig config;
  config.harvest_month_by_resource = {8, 0, 8};
  return config;
}

/// A world standing in the counter's hour of `day`.
core::WorldState WorldAt(const core::BarterConfig& config, core::SimDay day) {
  core::WorldState world;
  world.calendar.tick = (static_cast<core::Tick>(day) * core::kTicksPerDay) + config.hour;
  core::RefreshCalendarCaches(world.calendar);
  return world;
}

/// One adult's daily need, grams of the grain equivalent: D.
double AdultNeed(const core::FoodConfig& food) {
  return static_cast<double>(core::DailyNeedKilograms(
             food.consumption, core::BiologicalAgeYears(kSpeedup, kAdultBirth, kMayFirst), false)) *
         static_cast<double>(core::kGramsPerKilogram);
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
/// adult's daily need; the harvest is twelve days ahead.
///   A: grain 6 D, milk 5 D. Of milk he eats a third of D today: 4.667 D is
///      offered — (a). Lacks potato: 4 days at a third of D = 1.333 D. What
///      is left of the 4.667, 3.333 D, takes anything that keeps longer:
///      grain.
///   B: grain 20 D: 8 D above the twelve days to the harvest — (b). Lacks
///      dairy, of milk no more than two days' third: 0.667 D; lacks potato:
///      1.333 D.
///   C: potato 15 D: 3 D above the twelve. Lacks bread 1.333 D, dairy 0.667 D.
///   D: grain 0.5 D, milk 3 D — 3.5 days in all, HUNGRY: gives the 2.667 D
///      of milk over his day's share and takes the richest on offer, grain,
///      to that value.
/// Offered: milk 7.333 D, grain 8 D, potato 3 D. Claimed: milk 1.333 D,
/// grain 3.333 + 1.333 + 2.667 = 7.333 D, potato 2.667 D. The pairs settle
/// at the smaller side:
///   A-B 14/33 D (A's milk to B 4.667 x 0.667 / 7.333; B's grain to A 3.333);
///   A-C 14/33 D (milk; potato 1.333);   B-C 4/3 D (grain, potato);
///   B-D 8/33 D (grain 2.667; D's milk 2.667 x 0.667 / 7.333);
///   A-D and C-D nothing.
/// Each gram once on the giving side: 2 x 80/33 D = 160/33 D = 4.848 D.
int CheckTheDryCount() {
  int failures = 0;
  const core::FoodConfig food = ThreeFoods();
  const core::BarterConfig config = Rules();
  const double need = AdultNeed(food);
  failures += Expect(need > 0.0, "dry count: the fixture's adult eats");

  core::WorldState world = WorldAt(config, kMayFirst);
  YardOf(world, food, need, 6.0, 5.0, 0.0);
  YardOf(world, food, need, 20.0, 0.0, 0.0);
  YardOf(world, food, need, 0.0, 0.0, 15.0);
  YardOf(world, food, need, 0.5, 3.0, 0.0);
  const std::vector<core::FamilyRow> before = world.families.rows;

  core::RunBarterDryCount(config, food, kSpeedup, world);
  const double volume = static_cast<double>(world.barter.dry_equivalent);
  // A gram's rounding of each pantry moves the figure by a few grams; a
  // pair settled at the larger side would move it by several D.
  failures += Expect(std::abs(volume - (160.0 / 33.0 * need)) < 0.001 * need,
                     "dry count: the four yards would pass 4.848 days of one man's need between "
                     "them");
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

  // THE COUNT'S OWN WORKING, BY YARD (0.37.80, BarterDryLines): the same
  // four yards. A offers 4.667 D of milk and would hand over 28/33 D of it
  // (to B and to C); B offers 8 D of grain and would hand over 14/33 + 4/3 +
  // 8/33 = 2 D. Every yard would carry home what it handed over, and what
  // is handed over in all is the count's volume.
  {
    const std::vector<core::BarterYardLine> lines =
        core::BarterDryLines(config, food, kSpeedup, world);
    const auto line_of = [&](std::size_t family_row, std::uint32_t resource) {
      for (const core::BarterYardLine& line : lines) {
        if (line.family == world.families.row_ids[family_row] && line.resource.value == resource) {
          return line;
        }
      }
      return core::BarterYardLine{};
    };
    const auto near = [need](core::Grams grams, double days) {
      return std::abs(static_cast<double>(grams) - (days * need)) < 0.001 * need;
    };
    failures += Expect(near(line_of(0, 1).offered, 14.0 / 3.0) &&
                           near(line_of(0, 1).would_give, 28.0 / 33.0) &&
                           near(line_of(1, 0).offered, 8.0) && near(line_of(1, 0).would_give, 2.0),
                       "dry lines: A's milk and B's grain — what each offers and what it would "
                       "hand over");
    core::Grams given = 0;
    core::Grams taken = 0;
    bool each_carries_its_own = true;
    for (std::size_t row = 0; row < world.families.rows.size(); ++row) {
      core::Grams gave = 0;
      core::Grams took = 0;
      for (const core::BarterYardLine& line : lines) {
        if (line.family == world.families.row_ids[row]) {
          gave += line.would_give;
          took += line.would_take;
        }
      }
      given += gave;
      taken += took;
      // A gram of rounding a line: three resources a yard.
      each_carries_its_own = each_carries_its_own && std::abs(gave - took) <= 3;
    }
    failures += Expect(each_carries_its_own && std::abs(given - taken) <= 12 &&
                           std::abs(static_cast<double>(given) - volume) <= 12.0,
                       "dry lines: every yard would carry home what it handed over, and the sum "
                       "is the count's volume");
    bool still_untouched = world.barter.dry_days_in_row == 1;
    for (std::size_t row = 0; row < before.size(); ++row) {
      still_untouched = still_untouched && world.families.rows[row].pantry == before[row].pantry;
    }
    failures += Expect(still_untouched,
                       "dry lines: asking moves no pantry and does not touch the count's run");
  }

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

/// What is a surplus: the perishable over the day's share, the reaped over
/// the need till the harvest — and the hour the count stands in.
int CheckWhatIsASurplus() {
  int failures = 0;
  const core::FoodConfig food = ThreeFoods();
  const core::BarterConfig config = Rules();
  const double need = AdultNeed(food);
  const auto givers = [&](core::SimDay day, double grain, double milk, double potato) {
    core::WorldState world = WorldAt(config, day);
    YardOf(world, food, need, grain, milk, potato);
    core::RunBarterDryCount(config, food, kSpeedup, world);
    return world.barter.dry_givers;
  };
  failures += Expect(givers(kMayFirst, 6.0, 0.3, 0.0) == 0 && givers(kMayFirst, 6.0, 1.0, 0.0) == 1,
                     "surplus: milk under a third of the day's need is eaten today, a day's need "
                     "of it is an offer — the shelf life does not multiply the share");
  failures += Expect(givers(kMayFirst, 6.0, 0.0, 5.0) == 0,
                     "surplus: the potato keeps 120 days — no perishable, and five days of it "
                     "twelve days before its harvest is no surplus");
  // Day 4, the first of February: the harvest is 24 days ahead, not 12.
  constexpr core::SimDay kFebruaryFirst = 4;
  failures +=
      Expect(givers(kMayFirst, 20.0, 0.0, 0.0) == 1 && givers(kFebruaryFirst, 20.0, 0.0, 0.0) == 0,
             "surplus: twenty days of bread is 8 over the need twelve days before the harvest "
             "and nothing over it twenty-four days before");
  // The harvest's own first day: a whole year to the next one.
  constexpr core::SimDay kAugustFirst = 28;
  failures +=
      Expect(givers(kAugustFirst, 40.0, 0.0, 0.0) == 0 && givers(kAugustFirst, 50.0, 0.0, 0.0) == 1,
             "surplus: on the harvest's first day the next one is 48 days ahead");
  {
    // Not the counter's hour: the count does not run, and says nothing.
    core::WorldState world = WorldAt(config, kMayFirst);
    world.calendar.tick -= 1;
    core::RefreshCalendarCaches(world.calendar);
    YardOf(world, food, need, 20.0, 5.0, 0.0);
    core::RunBarterDryCount(config, food, kSpeedup, world);
    failures += Expect(world.barter.dry_givers == 0,
                       "hour: an hour before the counter's the dry count does not run");
  }
  return failures;
}

/// The thresholds hold the fact back, each by itself.
int CheckTheFactsThresholds() {
  int failures = 0;
  const core::FoodConfig food = ThreeFoods();
  const double need = AdultNeed(food);
  // Two yards that would exchange — grain above the twelve days against
  // potato above them.
  const auto village = [&](core::WorldState& world) {
    YardOf(world, food, need, 20.0, 0.0, 0.0);
    YardOf(world, food, need, 0.0, 0.0, 15.0);
  };
  {
    const core::BarterConfig config = Rules();  // three yards a side
    core::WorldState world = WorldAt(config, kMayFirst);
    village(world);
    for (int day = 0; day < 5; ++day) {
      core::RunBarterDryCount(config, food, kSpeedup, world);
    }
    failures +=
        Expect(world.barter.dry_givers == 2 && world.barter.dry_equivalent > 0 &&
                   world.barter.dry_days_in_row == 0 && world.barter.worth_starting_raised == 0,
               "fact: two yards exchanging are fewer than three a side — no day is met");
  }
  {
    core::BarterConfig config = Rules();
    config.fact_yards_each_side = 2;
    config.fact_share_of_village_need = 1.0F;  // 2 D: the two pass 2 x 1.333 D, enough
    core::WorldState world = WorldAt(config, kMayFirst);
    village(world);
    for (int day = 0; day < 3; ++day) {
      core::RunBarterDryCount(config, food, kSpeedup, world);
    }
    failures += Expect(world.barter.worth_starting_raised == 1,
                       "fact: with two a side asked and the volume above the share, it rises");
  }
  {
    core::BarterConfig config = Rules();
    config.fact_yards_each_side = 2;
    config.fact_share_of_village_need = 1.0F;
    core::WorldState world = WorldAt(config, kMayFirst);
    village(world);
    core::RunBarterDryCount(config, food, kSpeedup, world);
    core::RunBarterDryCount(config, food, kSpeedup, world);
    // The second yard eats its surplus: nothing to pass on the third day.
    world.families.rows[1].pantry[2] = 0;
    core::RunBarterDryCount(config, food, kSpeedup, world);
    failures += Expect(world.barter.dry_days_in_row == 0 && world.barter.worth_starting_raised == 0,
                       "fact: a day not met breaks the run — days IN A ROW");
  }
  {
    // A lone yard with milk over its day's share: it offers, nobody takes,
    // nothing passes, and the two counts say so apart from the volume.
    const core::BarterConfig config = Rules();
    core::WorldState world = WorldAt(config, kMayFirst);
    YardOf(world, food, need, 6.0, 5.0, 0.0);
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
                 !parse("barter_perishable_max_keep_days", "-1", read),
             "knobs: a share outside 0..1, no days in a row, days below nought are refused");
  failures += Expect(parse("barter_fact_yards_each_side", "5", read) &&
                         read.fact_yards_each_side == 5 && read.fact_days_in_row == 3,
                     "knobs: a key read lands, a key absent keeps its default");
  failures += Expect(core::BarterWorldParamKeys().size() == 10,
                     "knobs: ten keys are declared to the assembly");
  failures += Expect(parse("barter_perishable_share_of_need", "0.5", read) &&
                         read.perishable_share_of_need == 0.5F &&
                         !parse("barter_perishable_share_of_need", "1.1", read),
                     "knobs: the perishable's share is read, and above one is refused");
  {
    // The harvest's month by resource: the soonest of the crops reaped into
    // it; a resource no crop is reaped into has none.
    const test::FakeTable resources({"key"}, {{"grain"}, {"milk"}, {"potato"}});
    const test::FakeTable crops({"key", "resource", "harvest_from_month"},
                                {{"rye_winter", "grain", "7"},
                                 {"wheat_spring", "grain", "8"},
                                 {"potato", "potato", "9"},
                                 {"flax", "fibre", "8"}});
    const test::FakeTableSet set({{"resources", &resources}, {"crops", &crops}});
    core::BarterConfig months;
    std::string trouble;
    const bool parsed = core::ParseBarterConfig(set, months, trouble);
    failures +=
        Expect(parsed && months.harvest_month_by_resource == std::vector<std::uint8_t>{7, 0, 9},
               "knobs: the grain's harvest opens with the rye in July, the potato's in "
               "September, the milk has none");
  }
  return failures;
}

}  // namespace

int CheckBarter() {
  return CheckTheDryCount() + CheckWhatIsASurplus() + CheckTheFactsThresholds() + CheckTheKnobs();
}
