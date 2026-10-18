// The checks of the clamps' rot and its lamp (clamp_rot_checks.h).
//
// THE KNOWN ANSWERS, by hand: a clamp and a church store holding the same potatoes rot the same
// grams a day (store_keeping_checks.cpp); the book's spoiled takes both, its spoiled_in_clamps and
// the window the clamp's alone. The lamp's months on the shipped tables: May is days 16-19 of the
// year and the potato's window opens in September, day 32, so it is lit on days 16-31, red on
// 24-31 (days_to_loss 8 or less at alarm_red_within_days 8).
#include "clamp_rot_checks.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "clamp_rot_lamp.h"
#include "core_common/alarm_state.h"
#include "core_common/clamp_rot_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "field_haul.h"
#include "production_config.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: clamp rot: " << label << '\n';
    return 1;
  }
  return 0;
}

core::Grams At(const core::ResourceAmounts& column, core::ResourceId resource) {
  return resource.value < column.size() ? column[resource.value] : 0;
}

}  // namespace

int CheckTheClampRotAndItsLamp() {
  int failures = 0;
  std::string error;
  const auto tables = core::LoadTableSet(KOLKHOZ_TABLES_DIR, &error);
  core::ProductionConfig config;
  if (Expect(tables != nullptr && core::ParseProductionConfig(*tables, config, error),
             "the shipped tables parse") != 0) {
    std::cout << error << '\n';
    return 1;
  }
  const core::ResourceId potato = config.potato_resource;
  const core::ResourceId vegetables = config.vegetables_resource;
  failures += Expect(potato.value != core::kInvalidDefIdValue &&
                         vegetables.value != core::kInvalidDefIdValue &&
                         config.vegetable_store_type.value != core::kInvalidDefIdValue,
                     "the tables name the potato, the vegetables and the vegetable store");

  // THE WRITER: a clamp and a church store, ten tonnes of potatoes and five of vegetables each.
  constexpr core::Grams kTen = 10'000'000;
  constexpr core::Grams kFive = 5'000'000;
  const auto store_of = [&](core::UnitTypeId type) {
    core::UnitRow store;
    store.type = type;
    store.level = 1;
    store.stock.assign(static_cast<std::size_t>(std::max(potato.value, vegetables.value)) + 1U, 0);
    store.stock[potato.value] = kTen;
    store.stock[vegetables.value] = kFive;
    return store;
  };
  core::WorldState world;
  world.calendar.day = 60;
  core::AppendRow(world.units, store_of(config.clamp_type));
  core::AppendRow(world.units, store_of(config.church_store_type));
  core::SpoilStores(config, world);
  const core::UnitRow& clamp = world.units.rows[0];
  const core::UnitRow& church = world.units.rows[1];
  const core::Grams clamp_potato = kTen - clamp.stock[potato.value];
  const core::Grams clamp_vegetables = kFive - clamp.stock[vegetables.value];
  const core::Grams church_potato = kTen - church.stock[potato.value];
  const core::YearLedger& book = world.ledger.current;
  std::cout << "  clamp rot, one day, kg: clamp potato " << clamp_potato / 1000 << ", vegetables "
            << clamp_vegetables / 1000 << ", church potato " << church_potato / 1000 << "; window "
            << core::ClampRotOfYear(world.clamp_rot, 60) / 1000 << '\n';
  failures += Expect(clamp_potato > 0 && clamp_vegetables > 0 && church_potato > 0,
                     "both stores rot both crops at all");
  failures += Expect(At(book.spoiled, potato) == clamp_potato + church_potato,
                     "the book's spoiled takes the clamp's rot and the church's alike");
  failures += Expect(At(book.spoiled_in_clamps, potato) == clamp_potato &&
                         At(book.spoiled_in_clamps, vegetables) == clamp_vegetables,
                     "spoiled_in_clamps takes the clamp's rot and not the church's");
  failures += Expect(core::ClampRotOfYear(world.clamp_rot, 60) == clamp_potato + clamp_vegetables,
                     "the window holds the clamp's potato and vegetables of the day");

  // THE WINDOW: a year of days back is forgotten, a day inside it is kept.
  core::ClampRotYear year;
  core::NoteClampRot(year, 10, 7'000);
  core::NoteClampRot(year, 50, 3'000);
  failures +=
      Expect(core::ClampRotOfYear(year, 50) == 10'000 && core::ClampRotOfYear(year, 57) == 10'000,
             "the window keeps a day less than a year back");
  core::NoteClampRot(year, 58, 0);
  failures += Expect(core::ClampRotOfYear(year, 58) == 3'000,
                     "the window forgets the day a year back once the year moves on");

  // THE LAMP: thirteen tonnes in the window on day 20 of the year (May).
  const auto lamp_on = [&config](core::SimDay day, core::Grams rotted, bool store_site) {
    core::WorldState state;
    state.calendar.day = day;
    core::NoteClampRot(state.clamp_rot, day, rotted);
    if (store_site) {
      core::UnitRow site;
      site.type = config.vegetable_store_type;
      site.level = 0;
      core::AppendRow(state.units, site);
    }
    std::vector<core::Alarm> alarms;
    core::CollectClampRotAlarms(config, state, alarms);
    core::PaintAlarms(alarms, 8);
    return alarms;
  };
  constexpr core::Grams kThirteen = 13'000'000;
  const std::vector<core::Alarm> may = lamp_on(48 + 20, kThirteen, false);
  failures += Expect(may.size() == 1 && may[0].kind == core::AlarmKind::kClampRot &&
                         may[0].resource.value == potato.value && may[0].amount == kThirteen &&
                         may[0].advice == core::AlarmAdvice::kBuildStore &&
                         may[0].advice_unit_type.value == config.vegetable_store_type.value,
                     "in May over the threshold the lamp lights, the potato, its grams, the store");
  failures += Expect(may.size() == 1 && may[0].days_to_loss == 12 &&
                         may[0].colour == core::AlarmColour::kYellow && may[0].lamp == 1,
                     "twelve days before the potato's window it is a yellow lamp");
  const std::vector<core::Alarm> august = lamp_on(48 + 28, kThirteen, false);
  failures += Expect(august.size() == 1 && august[0].days_to_loss == 4 &&
                         august[0].colour == core::AlarmColour::kRed,
                     "four days before the potato's window it is red");
  failures += Expect(lamp_on(48 + 15, kThirteen, false).empty(), "in April it is out");
  failures += Expect(lamp_on(48 + 32, kThirteen, false).empty(),
                     "the day the potato's window opens it is out");
  failures += Expect(lamp_on(48 + 20, 11'000'000, false).empty(), "under the threshold it is out");
  failures +=
      Expect(lamp_on(48 + 20, kThirteen, true).empty(), "a vegetable store staked puts it out");
  return failures;
}
