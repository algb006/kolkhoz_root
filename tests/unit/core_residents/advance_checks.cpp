// The checks of the new household's advance (advance_checks.h).
//
// THE KNOWN ANSWERS, by hand. One food on the roster that is issued - grain, 3.3 kcal a gram, the
// reference grain itself, so a gram of it is a gram of grain equivalent; a kilogram a trudoden.
// An adult of thirty needs 300 kg a game year of 48 days: 6.25 kg a day. The exchange runs every
// day (period 1). A barrack household of two workers formed two days ago, nothing earned, an empty
// pantry, a store of a tonne: topped up with 12.5 kg, owes 12.5 kg. With 5 kg in the pantry: 7.5.
// At 2.00 trudodni earned (one a worker): nothing. Moved to a house: nothing. On the 49th day:
// nothing; on the 48th: still. An empty store: nothing, and no debt. A couple formed in a house of
// its own: advanced there. A row of the genesis: never. Two elders: nothing - no worker. The cap,
// 57 kg an adult: 114 kg for two; 110 kg already given leaves 4. And the repayment: a family owing
// 20 kg with ten trudodni outstanding is issued 10 kg, of which the store keeps 5 - 5 kg in the
// pantry, 15 kg owed, the ten trudodni redeemed in full; owing 2 kg, the store keeps 2.
#include "advance_checks.h"

#include <cstdint>
#include <iostream>
#include <utility>

#include "core_common/calendar.h"
#include "core_common/event_state.h"
#include "core_common/family_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "family_exchange.h"
#include "food_config.h"
#include "household_advance.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: advance: " << label << '\n';
    return 1;
  }
  return 0;
}

constexpr core::Grams kKilo = core::kGramsPerKilogram;
constexpr std::uint32_t kToday = 12;

/// Grain alone is issued; potato is on the roster and in no bundle.
core::FoodConfig GrainConfig() {
  core::FoodConfig config;
  config.resources.assign(2, core::FoodResourceDef{});
  config.resources[0] = {.kcal_per_gram = 3.3F,
                         .category = core::FoodCategory::kBread,
                         .issue_kg_per_trudoden = 1.0F,
                         .ration_kg_per_day = 2.0F};
  config.resources[1] = {.kcal_per_gram = 0.77F,
                         .category = core::FoodCategory::kPotato,
                         .issue_kg_per_trudoden = 0.0F,
                         .ration_kg_per_day = 0.0F};
  config.distribution.period_days = 1;
  config.distribution.reserve_seed_fund = 0;
  config.plot.fish_kg_per_yard_year = {0.0F, 0.0F, 0.0F};
  return config;
}

/// A store of `grain_kg`, and one household of two adults of `age_years`,
/// formed two days ago in a barrack, with nothing earned; the day is kToday.
core::WorldState BarrackWorld(float grain_kg, float age_years = 30.0F) {
  core::WorldState world;
  world.calendar.tick = static_cast<core::Tick>(kToday) * core::kTicksPerDay;
  core::RefreshCalendarCaches(world.calendar);
  world.chairman.ration_auto = 0;
  core::UnitRow store;
  store.level = 1;
  store.stock.assign(2, 0);
  store.stock[0] = static_cast<core::Grams>(grain_kg * static_cast<float>(kKilo));
  core::AppendRow(world.units, store);
  core::FamilyRow yard;
  yard.in_barrack = 1;
  core::MarkFormedInPlay(yard, kToday - 2);
  const core::FamilyId id = core::AppendRow(world.families, yard);
  for (int member = 0; member < 2; ++member) {
    core::ResidentRow adult;
    adult.family = id;
    adult.birth_day =
        static_cast<std::int32_t>(kToday) - static_cast<std::int32_t>(age_years * 12.0F);
    adult.satiety = 70.0F;
    core::AppendRow(world.residents, adult);
  }
  return world;
}

core::Grams Pantry(const core::WorldState& world) {
  const core::ResourceAmounts& pantry = world.families.rows[0].pantry;
  return pantry.empty() ? 0 : pantry[0];
}

core::Grams Booked(const core::WorldState& world) {
  const core::ResourceAmounts& advance = world.ledger.current.advance;
  return advance.empty() ? 0 : advance[0];
}

bool Near(core::Grams value, core::Grams wanted) {
  const core::Grams off = value > wanted ? value - wanted : wanted - value;
  return off <= 2;  // the float's grams
}

}  // namespace

int CheckTheNewHouseholdsAdvance() {
  int failures = 0;
  const core::FoodConfig config = GrainConfig();
  constexpr float kSpeedup = 4.0F;
  const core::Grams norm = (12 * kKilo) + (kKilo / 2);  // two adults, 6.25 kg each

  // THE RULE's FIRST LINE: topped up to the norm, and owed.
  {
    core::WorldState world = BarrackWorld(1000.0F);
    core::RunFamilyExchange(config, kSpeedup, world);
    const core::FamilyRow& yard = world.families.rows[0];
    std::cout << "  advance, a barrack household of two, nothing earned: pantry " << Pantry(world)
              << " g, owes " << yard.advance_debt << " g, store " << world.units.rows[0].stock[0]
              << " g\n";
    failures += Expect(Near(Pantry(world), norm) && Near(yard.advance_debt, norm) &&
                           Near(yard.advance_given, norm) && Near(Booked(world), norm) &&
                           Near(world.units.rows[0].stock[0], (1000 * kKilo) - norm),
                       "a barrack household with nothing earned is topped up to the day's norm, "
                       "12.5 kg, owes it, and the year's book has it");
    failures += Expect(yard.trudodni_redeemed == 0 && world.ledger.current.advance_repaid == 0,
                       "an advance redeems no trudoden and repays nothing");
  }
  // LESS WHAT THE PANTRY HOLDS.
  {
    core::WorldState world = BarrackWorld(1000.0F);
    world.families.rows[0].pantry.assign(2, 0);
    world.families.rows[0].pantry[0] = 5 * kKilo;
    core::RunFamilyExchange(config, kSpeedup, world);
    failures += Expect(
        Near(world.families.rows[0].advance_debt, norm - (5 * kKilo)) && Near(Pantry(world), norm),
        "with 5 kg in the pantry the top-up is 7.5 kg - the norm less what it holds");
    world = BarrackWorld(1000.0F);
    world.families.rows[0].pantry.assign(2, 0);
    world.families.rows[0].pantry[0] = 20 * kKilo;
    core::RunFamilyExchange(config, kSpeedup, world);
    failures += Expect(world.families.rows[0].advance_debt == 0 && Pantry(world) == 20 * kKilo,
                       "a pantry that holds the norm already gets nothing");
  }
  // THE ENDS: the trudoden, the house, the ceiling.
  {
    core::WorldState world = BarrackWorld(1000.0F);
    world.families.rows[0].earned_since_formed = 199;
    core::RunFamilyExchange(config, kSpeedup, world);
    failures += Expect(world.families.rows[0].advance_debt > 0,
                       "at 1.99 trudodni for two workers it is still advanced");
    world = BarrackWorld(1000.0F);
    world.families.rows[0].earned_since_formed = 200;
    core::RunFamilyExchange(config, kSpeedup, world);
    failures += Expect(world.families.rows[0].advance_debt == 0 && Pantry(world) == 0,
                       "at one trudoden a worker it gets nothing");
    world = BarrackWorld(1000.0F);
    world.families.rows[0].in_barrack = 0;  // moved to a house with a yard
    core::RunFamilyExchange(config, kSpeedup, world);
    failures += Expect(world.families.rows[0].advance_debt == 0,
                       "a household formed in a barrack gets nothing the day it lives in a house");
    world = BarrackWorld(1000.0F);
    world.calendar.tick = static_cast<core::Tick>(60) * core::kTicksPerDay;
    core::RefreshCalendarCaches(world.calendar);
    world.families.rows[0].formed_day = 60 - 48;
    core::RunFamilyExchange(config, kSpeedup, world);
    failures += Expect(world.families.rows[0].advance_debt > 0,
                       "on the 48th day after it was formed it is still advanced");
    world = BarrackWorld(1000.0F);
    world.calendar.tick = static_cast<core::Tick>(60) * core::kTicksPerDay;
    core::RefreshCalendarCaches(world.calendar);
    world.families.rows[0].formed_day = 60 - 49;
    core::RunFamilyExchange(config, kSpeedup, world);
    failures +=
        Expect(world.families.rows[0].advance_debt == 0, "on the 49th day the ceiling has run out");
  }
  // ONLY OUT OF THE FREE REMAINDER.
  {
    core::WorldState world = BarrackWorld(0.0F);
    core::RunFamilyExchange(config, kSpeedup, world);
    failures += Expect(world.families.rows[0].advance_debt == 0 &&
                           world.families.rows[0].advance_given == 0 && Pantry(world) == 0,
                       "an empty store advances nothing and writes no debt");
    world = BarrackWorld(4.0F);
    core::RunFamilyExchange(config, kSpeedup, world);
    failures += Expect(Near(world.families.rows[0].advance_debt, Pantry(world)) &&
                           Pantry(world) > 0 && Pantry(world) <= 4 * kKilo,
                       "a store short of the norm gives what it has, and the debt is what it gave");
  }
  // WHO: a household formed in the village, a row of the genesis, no worker.
  {
    core::WorldState world = BarrackWorld(1000.0F);
    world.families.rows[0].in_barrack = 0;
    world.families.rows[0].formed_in_barrack = 0;  // wed into a house of their own
    core::RunFamilyExchange(config, kSpeedup, world);
    failures += Expect(Near(world.families.rows[0].advance_debt, norm),
                       "a household formed in a house of its own is advanced there");
    world = BarrackWorld(1000.0F);
    world.families.rows[0].formed_day = core::kNotFormedInPlay;
    core::RunFamilyExchange(config, kSpeedup, world);
    failures +=
        Expect(world.families.rows[0].advance_debt == 0, "a row of the genesis is never advanced");
    world = BarrackWorld(1000.0F, 65.0F);
    core::RunFamilyExchange(config, kSpeedup, world);
    failures += Expect(world.families.rows[0].advance_debt == 0,
                       "a household of two elders has no worker and gets nothing");
  }
  // THE WRITE-OFF AT THE SECOND YEAR-END SHARE-OUT: a debt open at the first
  // turn stands; at the second it is written off into the year's book. A row
  // of the genesis counts no turns.
  {
    core::WorldState world = BarrackWorld(0.0F);
    const auto turn = [&world, &config](std::uint32_t year) {
      world.calendar.tick = static_cast<core::Tick>(year * core::kDaysPerYear) * core::kTicksPerDay;
      core::RefreshCalendarCaches(world.calendar);
      core::RunFamilyExchange(config, 4.0F, world);
    };
    world.families.rows[0].formed_day = core::kDaysPerYear - 5;
    world.families.rows[0].earned_since_formed = 1000;  // not advanced again
    world.families.rows[0].advance_debt = 9 * kKilo;
    world.families.rows[0].advance_given = 9 * kKilo;
    turn(1);
    failures += Expect(world.families.rows[0].advance_turns == 1 &&
                           world.families.rows[0].advance_debt == 9 * kKilo &&
                           world.ledger.current.advance_written_off_turns == 0,
                       "the first year-end share-out leaves the debt standing");
    const auto written_off_events = [&world]() {
      std::int64_t grams = 0;
      std::uint32_t count = 0;
      for (const core::SimEvent& event : world.step_events) {
        if (event.kind == core::EventKind::kAdvanceWrittenOff &&
            event.family.value == world.families.row_ids[0].value) {
          grams += event.amount;
          ++count;
        }
      }
      return std::pair<std::uint32_t, std::int64_t>{count, grams};
    };
    failures +=
        Expect(written_off_events().first == 0, "and names no household: nothing was written off");
    turn(2);
    failures +=
        Expect(written_off_events() == std::pair<std::uint32_t, std::int64_t>{1U, 9 * kKilo},
               "the write-off names its household where it happens: one event, this "
               "family, 9 kg");
    failures += Expect(world.families.rows[0].advance_turns == 2 &&
                           world.families.rows[0].advance_debt == 0 &&
                           world.ledger.current.advance_written_off_turns == 9 * kKilo &&
                           world.families.rows[0].advance_given == 9 * kKilo,
                       "the second writes it off into the year's book, 9 kg");
    core::WorldState old_row = BarrackWorld(0.0F);
    old_row.families.rows[0].formed_day = core::kNotFormedInPlay;
    old_row.calendar.tick = static_cast<core::Tick>(core::kDaysPerYear) * core::kTicksPerDay;
    core::RefreshCalendarCaches(old_row.calendar);
    core::RunFamilyExchange(config, 4.0F, old_row);
    failures += Expect(old_row.families.rows[0].advance_turns == 0,
                       "a row of the genesis counts no share-out");
  }
  // NO CAP IN KILOGRAMS: the ceiling's days are the only limit. A household
  // already advanced 500 kg is topped up to the norm like any other.
  {
    core::WorldState world = BarrackWorld(1000.0F);
    world.families.rows[0].advance_given = 500 * kKilo;
    world.families.rows[0].advance_debt = 500 * kKilo;
    core::RunFamilyExchange(config, kSpeedup, world);
    failures += Expect(Near(world.families.rows[0].advance_given, (500 * kKilo) + norm) &&
                           Near(Pantry(world), norm),
                       "what was given before bounds nothing: still topped up to the norm");
  }
  // THE REPAYMENT: OUT OF WHAT AN ISSUE GIVES ABOVE THE NORM, AND NEVER OUT OF
  // THE NORM. Ten trudodni outstanding are ten kilograms of grain; the norm of
  // two adults is 12.5 kg. With an empty pantry the issue leaves the family
  // under the norm: it comes whole and repays nothing. With 10 kg in the
  // pantry the issue lifts it to 20 kg, 7.5 kg above the norm: the store
  // keeps half of that, 3.75 kg - 6.25 kg handed, the debt 20 -> 16.25 kg.
  {
    core::WorldState world = BarrackWorld(1000.0F);
    core::FamilyRow& yard = world.families.rows[0];
    yard.earned_since_formed = 1000;  // past the trudoden: no new advance today
    yard.trudodni_account = 1000;     // ten trudodni outstanding, a kilogram each
    yard.advance_debt = 20 * kKilo;
    yard.advance_given = 20 * kKilo;
    core::RunFamilyExchange(config, kSpeedup, world);
    failures +=
        Expect(Pantry(world) == 10 * kKilo && world.families.rows[0].advance_debt == 20 * kKilo &&
                   world.ledger.current.advance_repaid == 0 &&
                   world.families.rows[0].trudodni_redeemed == 1000,
               "an issue that leaves an indebted family under the norm comes whole and "
               "repays nothing");
  }
  {
    core::WorldState world = BarrackWorld(1000.0F);
    core::FamilyRow& yard = world.families.rows[0];
    yard.earned_since_formed = 1000;
    yard.trudodni_account = 1000;
    yard.advance_debt = 20 * kKilo;
    yard.advance_given = 20 * kKilo;
    yard.pantry.assign(2, 0);
    yard.pantry[0] = 10 * kKilo;
    core::RunFamilyExchange(config, kSpeedup, world);
    const core::Grams issued =
        world.ledger.current.issued.empty() ? 0 : world.ledger.current.issued[0];
    std::cout << "  advance, repayment: issued " << issued << " g, pantry " << Pantry(world)
              << " g, owes " << world.families.rows[0].advance_debt << " g, kept "
              << world.ledger.current.advance_repaid << " g, redeemed "
              << world.families.rows[0].trudodni_redeemed << '\n';
    const core::Grams kept = (3 * kKilo) + ((3 * kKilo) / 4);  // 3.75 kg
    failures +=
        Expect(Near(world.ledger.current.advance_repaid, kept) &&
                   Near(issued, (10 * kKilo) - kept) && Near(Pantry(world), (20 * kKilo) - kept) &&
                   Near(world.families.rows[0].advance_debt, (20 * kKilo) - kept) &&
                   world.families.rows[0].trudodni_redeemed == 1000,
               "an issue that lifts the pantry 7.5 kg over the norm: half of that, "
               "3.75 kg, is kept against the debt, the trudodni redeemed in full");
    failures += Expect(world.families.rows[0].advance_given == 20 * kKilo,
                       "a repayment does not lower what was ever given");
  }
  {
    core::WorldState world = BarrackWorld(1000.0F);
    core::FamilyRow& yard = world.families.rows[0];
    yard.earned_since_formed = 1000;
    yard.trudodni_account = 1000;
    yard.advance_debt = 100;  // a hundred grams: less than half of what is above the norm
    yard.advance_given = 20 * kKilo;
    yard.pantry.assign(2, 0);
    yard.pantry[0] = 10 * kKilo;
    core::RunFamilyExchange(config, kSpeedup, world);
    failures += Expect(
        world.families.rows[0].advance_debt == 0 && world.ledger.current.advance_repaid == 100,
        "a debt smaller than the half is paid whole and no more is kept");
  }
  // THE RATION IS NOT TOUCHED by a debt.
  {
    core::WorldState world = BarrackWorld(1000.0F);
    world.chairman.ration_auto = 1;
    for (core::ResidentRow& resident : world.residents.rows) {
      resident.satiety = 10.0F;
    }
    world.families.rows[0].component_satiety = 10.0F;
    world.families.rows[0].earned_since_formed = 1000;  // not advanced: the ration alone
    world.families.rows[0].advance_debt = 20 * kKilo;
    world.families.rows[0].advance_given = 20 * kKilo;
    core::RunFamilyExchange(config, kSpeedup, world);
    const core::Grams ration =
        world.ledger.current.ration.empty() ? 0 : world.ledger.current.ration[0];
    failures += Expect(ration == 4 * kKilo && Pantry(world) == 4 * kKilo &&
                           world.families.rows[0].advance_debt == 20 * kKilo,
                       "a hungry indebted family's ration comes whole and repays nothing");
  }
  // THE EARNED SUM OUTLIVES THE YEAR'S TURN, where the account burns.
  {
    core::WorldState world = BarrackWorld(0.0F);
    world.calendar.tick = static_cast<core::Tick>(core::kDaysPerYear) * core::kTicksPerDay;
    core::RefreshCalendarCaches(world.calendar);
    world.families.rows[0].formed_day = core::kDaysPerYear - 5;
    world.families.rows[0].trudodni_account = 150;
    world.families.rows[0].earned_since_formed = 150;
    core::RunFamilyExchange(config, kSpeedup, world);
    failures += Expect(world.families.rows[0].trudodni_account == 0 &&
                           world.families.rows[0].earned_since_formed == 150,
                       "the year's turn burns the account and leaves the earned sum");
  }
  // The arithmetic's own edges.
  {
    core::AdvanceRules rules;
    failures += Expect(core::AdvanceRepayment(rules, 0, 10 * kKilo) == 0 &&
                           core::AdvanceRepayment(rules, 20 * kKilo, 10 * kKilo) == 5 * kKilo &&
                           core::AdvanceRepayment(rules, 2 * kKilo, 10 * kKilo) == 2 * kKilo,
                       "the repayment is half of what is above the norm, never more than the debt");
    core::HouseholdCounts two;
    two.need_kg_per_day = 12.5F;
    failures += Expect(
        core::IssueAboveNorm(two, 1, 0, 10 * kKilo) == 0 &&
            core::IssueAboveNorm(two, 1, 10 * kKilo, 10 * kKilo) == (7 * kKilo) + (kKilo / 2) &&
            core::IssueAboveNorm(two, 1, 30 * kKilo, 10 * kKilo) == 10 * kKilo &&
            core::IssueAboveNorm(two, 2, 10 * kKilo, 10 * kKilo) == 0,
        "above the norm: nothing under it, the excess over it, never more than "
        "the issue itself, and the norm is the period's");
    failures += Expect(core::GrainEquivalentPerGram(config, 0) == 1.0F &&
                           core::GrainEquivalentPerGram(config, 9) == 0.0F,
                       "a gram of the reference grain is a gram of grain equivalent");
    rules.ceiling_days = 0;
    core::FoodConfig off = config;
    off.advance = rules;
    core::WorldState world = BarrackWorld(1000.0F);
    core::RunFamilyExchange(off, kSpeedup, world);
    failures += Expect(world.families.rows[0].advance_debt == 0,
                       "a ceiling of nought days switches the advance off");
  }
  return failures;
}
