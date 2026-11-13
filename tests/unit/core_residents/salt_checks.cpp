// The checks of the salt (salt_checks.h; #1630 (B)).
//
// THE KNOWN ANSWERS, by hand at the design's numbers (125 g an eater-day, 1 kg of a substitute an
// eater-day, steps from 4 / 8 / 12 days, -2 a met day): a family of two eats 250 g a day; a month
// of 4 days for two families of two is 2 kg; a store of 1 kg gives each half (500 g).

#include "salt_checks.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include "core_common/calendar.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "salt_issue.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

constexpr core::ResourceId kSalt{0};
constexpr core::ResourceId kFish{1};
constexpr core::ResourceId kMeat{2};

core::SaltIssueConfig Config(bool on) {
  core::SaltIssueConfig config;  // the design's numbers, the defaults
  config.on = on;
  config.salt = kSalt;
  config.salted_fish = kFish;
  config.salted_meat = kMeat;
  return config;
}

/// Two families of two; a store holding `store_g` of salt; `pantry_g` of salt
/// in each pantry.
core::WorldState Village(core::Grams store_g, core::Grams pantry_g) {
  core::WorldState world;
  for (int index = 0; index < 2; ++index) {
    core::FamilyRow family;
    family.pantry.assign(3, 0);
    family.pantry[kSalt.value] = pantry_g;
    core::AppendRow(world.families, family);
  }
  for (std::size_t index = 0; index < 4; ++index) {
    core::ResidentRow person;
    person.family = world.families.row_ids[index / 2];
    person.health = 80.0F;
    person.mood = 60.0F;
    person.mood_base = 60.0F;
    core::AppendRow(world.residents, person);
  }
  core::UnitRow store;
  store.level = 1;
  store.stock.assign(3, 0);
  store.stock[kSalt.value] = store_g;
  core::AppendRow(world.units, store);
  return world;
}

}  // namespace

int CheckTheSalt() {
  int failures = 0;
  const core::SaltIssueConfig on = Config(true);

  // OFF: nothing moves, the count stays 0.
  core::WorldState off = Village(1000, 0);
  core::IssueSaltMonth(Config(false), off);
  core::RunSaltDay(Config(false), off);
  failures += Expect(
      off.units.rows[0].stock[kSalt.value] == 1000 && off.families.rows[0].salt_hunger_days == 0.0F,
      "salt: off, nothing is issued and nobody counts");

  // THE MONTH'S ISSUE, SHORT: 2 kg asked, 1 kg held - 500 g to each, booked.
  core::WorldState month = Village(1000, 0);
  core::IssueSaltMonth(on, month);
  failures += Expect(month.units.rows[0].stock[kSalt.value] == 0 &&
                         month.families.rows[0].pantry[kSalt.value] == 500 &&
                         month.families.rows[1].pantry[kSalt.value] == 500 &&
                         month.ledger.current.issued[kSalt.value] == 1000,
                     "salt: a short store shares by need, the issue booked as issued");
  // Not a month's first day: no issue.
  core::WorldState midmonth = Village(1000, 0);
  midmonth.calendar.day = 1;
  core::IssueSaltMonth(on, midmonth);
  failures += Expect(midmonth.units.rows[0].stock[kSalt.value] == 1000,
                     "salt: the issue waits for the month's first day");

  // THE DAY'S NORM: 250 g a family of two eaten, the day met.
  core::WorldState day = Village(0, 300);
  core::RunSaltDay(on, day);
  failures += Expect(day.families.rows[0].pantry[kSalt.value] == 50 &&
                         day.families.rows[0].salt_hunger_days == 0.0F,
                     "salt: a family of two eats 250 g a day; the day is met");
  // The next day only 50 g: short, the count grows.
  core::RunSaltDay(on, day);
  failures += Expect(day.families.rows[0].salt_hunger_days == 1.0F &&
                         day.families.rows[0].pantry[kSalt.value] == 0,
                     "salt: a short day adds one to the count");

  // A SUBSTITUTE closes it: 2 kg of salted fish for two eater-days.
  core::WorldState fish = Village(0, 0);
  fish.families.rows[0].pantry[kFish.value] = 3000;
  core::RunSaltDay(on, fish);
  failures +=
      Expect(fish.families.rows[0].pantry[kFish.value] == 1000 &&
                 fish.families.rows[0].salt_hunger_days == 0.0F &&
                 fish.families.rows[1].salt_hunger_days == 1.0F,
             "salt: 1 kg of salted fish an eater-day meets the norm; a family without counts");

  // THE STEPS AND THE PRICE: 12 days without - step 3, health -1 a day.
  core::WorldState dry = Village(0, 0);
  for (int index = 0; index < 12; ++index) {
    core::RunSaltDay(on, dry);
  }
  failures += Expect(core::SaltStepOf(on, 3.9F) == 0 && core::SaltStepOf(on, 4.0F) == 1 &&
                         core::SaltStepOf(on, 8.0F) == 2 && core::SaltStepOf(on, 12.0F) == 3,
                     "salt: steps from 4, 8 and 12 days");
  failures += Expect(dry.families.rows[0].salt_step == 3 &&
                         std::fabs(dry.residents.rows[0].health - 79.0F) < 0.01F,
                     "salt: on step 3 from the 12th day, the health falls 1 that day");
  // The month's mood: step 3 keeps step 2's -8.
  dry.calendar.day = 0;
  core::SaltMoodMonth(on, dry);
  failures += Expect(
      std::fabs(dry.residents.rows[0].mood - 52.0F) < 0.01F && core::SaltMoodTerm(on, 1) == -4.0F,
      "salt: the month's mood -8 from step 2, -4 on step 1");
  // THE FLOOR: step 3 takes health to 45 and no lower; one below keeps his own.
  dry.residents.rows[0].health = 45.5F;
  dry.residents.rows[1].health = 30.0F;
  core::RunSaltDay(on, dry);
  failures += Expect(std::fabs(dry.residents.rows[0].health - 45.0F) < 0.01F &&
                         std::fabs(dry.residents.rows[1].health - 30.0F) < 0.01F,
                     "salt: step 3 stops at the health floor 45; below it nothing is taken");
  // RECOVERY: a met day takes two off.
  dry.families.rows[0].pantry[kSalt.value] = 250;
  core::RunSaltDay(on, dry);
  failures +=
      Expect(dry.families.rows[0].salt_hunger_days == 11.0F && dry.families.rows[0].salt_step == 2,
             "salt: a met day takes two days off the count (13 -> 11, step 2)");
  return failures;
}
