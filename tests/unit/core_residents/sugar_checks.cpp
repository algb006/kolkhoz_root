// The checks of the month's sugar (sugar_checks.h).

#include "sugar_checks.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include "core_common/calendar.h"
#include "core_common/event_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "sugar_issue.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

bool Near(float value, float expected) {
  return std::abs(value - expected) < 0.01F;
}

constexpr core::ResourceId kSugar{0};
constexpr core::ResourceId kHoney{1};
constexpr float kLife = 4.0F;

core::SugarIssueConfig Config() {
  core::SugarIssueConfig config;  // the design's numbers, the defaults
  config.sugar = kSugar;
  config.honey = kHoney;
  config.kg_per_resident_year = 12.0F;  // 1 kg a month a resident: round grams
  return config;
}

/// Two families of two adults, born 30 game years before day 0 at life x4;
/// a store holding `sugar_kg` of sugar; `honey_kg` in family 0's pantry.
core::WorldState Village(double sugar_kg, double honey_kg) {
  core::WorldState world;
  for (int index = 0; index < 2; ++index) {
    core::FamilyRow family;
    family.pantry.assign(2, 0);
    core::AppendRow(world.families, family);
  }
  world.families.rows[0].pantry[kHoney.value] = static_cast<core::Grams>(honey_kg * 1000.0);
  for (std::size_t index = 0; index < 4; ++index) {
    core::ResidentRow person;
    person.family = world.families.row_ids[index / 2];
    person.birth_day = -static_cast<std::int32_t>(30U * core::kDaysPerYear / 4U);
    person.mood_base = 60.0F;
    person.mood = 60.0F;
    core::AppendRow(world.residents, person);
  }
  core::UnitRow store;
  store.level = 1;
  store.stock.assign(2, 0);
  store.stock[kSugar.value] = static_cast<core::Grams>(sugar_kg * 1000.0);
  core::AppendRow(world.units, store);
  return world;
}

}  // namespace

int CheckTheSugar() {
  int failures = 0;
  const core::SugarIssueConfig config = Config();

  // A FULL MONTH: 4 kg needed, 10 in the store - 4 eaten, everyone +2.
  core::WorldState full = Village(10.0, 0.0);
  core::IssueSugarMonth(config, kLife, full);
  failures += Expect(full.units.rows[0].stock[kSugar.value] == 6000 &&
                         Near(full.families.rows[0].sugar_share, 1.0F) &&
                         Near(full.residents.rows[0].mood, 62.0F),
                     "sugar: a full month eats the need by eaters and gives +2");

  // HALF THE SUGAR: a neutral month (econ's linear term), and the store empty.
  core::WorldState half = Village(2.0, 0.0);
  core::IssueSugarMonth(config, kLife, half);
  failures += Expect(half.units.rows[0].stock[kSugar.value] == 0 &&
                         Near(half.families.rows[1].sugar_share, 0.5F) &&
                         Near(half.residents.rows[3].mood, 60.0F),
                     "sugar: half the need shared by need is a neutral month, the store emptied");

  // NONE: −2, and the month is dry.
  core::WorldState none = Village(0.0, 0.0);
  core::IssueSugarMonth(config, kLife, none);
  failures += Expect(
      Near(none.residents.rows[0].mood, 58.0F) && none.families.rows[0].sugar_dry_months == 1,
      "sugar: no sugar is −2 and a dry month");

  // HONEY FIRST: family 0's 2 kg of honey covers it; the store's 2 kg go to
  // family 1 whole - both full.
  core::WorldState honey = Village(2.0, 2.0);
  core::IssueSugarMonth(config, kLife, honey);
  failures += Expect(honey.families.rows[0].pantry[kHoney.value] == 0 &&
                         Near(honey.families.rows[0].sugar_share, 1.0F) &&
                         Near(honey.families.rows[1].sugar_share, 1.0F) &&
                         honey.units.rows[0].stock[kSugar.value] == 0,
                     "sugar: the pantry's honey counts first, the store covers the rest");

  // A CHILD: ±4, not ±2.
  core::WorldState child = Village(10.0, 0.0);
  child.residents.rows[1].birth_day = 0;  // born on day 0: a child
  core::IssueSugarMonth(config, kLife, child);
  failures +=
      Expect(Near(child.residents.rows[1].mood, 64.0F) && Near(child.residents.rows[0].mood, 62.0F),
             "sugar: a child takes +4 of a full month, an adult +2");

  // ONLY ON A MONTH'S FIRST DAY.
  core::WorldState midmonth = Village(10.0, 0.0);
  midmonth.calendar.day = 2;
  core::IssueSugarMonth(config, kLife, midmonth);
  failures += Expect(midmonth.units.rows[0].stock[kSugar.value] == 10000,
                     "sugar: a day inside the month issues nothing");

  // THE COMPLAINT: dry months 1 and 2 of game months 1-2 say nothing (before
  // month 3); dry month 3 (game month 3) says it once; game month 4, the same
  // season, does not.
  core::WorldState dry = Village(0.0, 0.0);
  const auto complaints = [&dry]() {
    int said = 0;
    for (const core::SimEvent& event : dry.step_events) {
      said += event.kind == core::EventKind::kChildrenAskSweets ? 1 : 0;
    }
    return said;
  };
  int by_month[4] = {0, 0, 0, 0};
  for (int month = 0; month < 4; ++month) {
    dry.step_events.clear();
    dry.calendar.day = static_cast<core::SimDay>(month) * core::kDaysPerMonth;
    core::IssueSugarMonth(config, kLife, dry);
    by_month[month] = complaints();
  }
  std::cout << "  sugar's complaint by game month 1-4: " << by_month[0] << ' ' << by_month[1] << ' '
            << by_month[2] << ' ' << by_month[3] << '\n';
  failures += Expect(by_month[0] == 0 && by_month[1] == 0 && by_month[2] == 1 && by_month[3] == 0,
                     "sugar: the children complain from game month 3 on the dry run, once a "
                     "season a village");
  return failures;
}
