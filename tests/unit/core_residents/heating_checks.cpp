// The checks of #2261 stage 1 (heating_checks.h).
//
// THE KNOWN ANSWERS at heating.md §3's numbers: at −10 °C a level-1 house burns 20 degrees ×
// 0.04 = 0.8 m³; insulated 0.6; level 2 0.68; dilapidated (wear 60) 1.12; a school (size 4)
// 3.2; nothing at +10 °C. The start's inhabited house gets 2 m³ once; an empty house burns
// nothing; a school with no buffer is a short day.

#include "heating_checks.h"

#include <cmath>
#include <cstdint>
#include <iostream>

#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "heating.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

bool Near(float value, float expected) {
  return std::fabs(value - expected) < 1e-4F;
}

constexpr std::uint16_t kHouse = 0;
constexpr std::uint16_t kSchool = 1;
constexpr std::uint16_t kBarn = 2;

core::HeatingConfig Config() {
  core::HeatingConfig config;
  config.has_heating = {1, 1, 0};
  config.is_housing = {1, 0, 0};
  config.heat_size_factor = {1.0F, 4.0F, 1.0F};
  return config;
}

core::UnitRow Unit(std::uint16_t type) {
  core::UnitRow unit;
  unit.type = core::UnitTypeId{type};
  unit.level = 1;
  return unit;
}

int CheckTheFormula() {
  int failures = 0;
  const core::HeatingConfig config = Config();
  core::UnitRow house = Unit(kHouse);
  failures += Expect(Near(core::HeatingBurnToday(config, house, -10.0F), 0.8F), "house −10: 0.8");
  failures += Expect(core::HeatingBurnToday(config, house, 10.0F) == 0.0F, "+10: no burn");
  failures += Expect(core::HeatingBurnToday(config, house, 15.0F) == 0.0F, "+15: no burn");
  house.insulated = 1;
  failures += Expect(Near(core::HeatingBurnToday(config, house, -10.0F), 0.6F), "insulated: 0.6");
  house.insulated = 0;
  house.level = 2;
  failures += Expect(Near(core::HeatingBurnToday(config, house, -10.0F), 0.68F), "level 2: 0.68");
  house.level = 1;
  house.wear = 60.0F;
  failures +=
      Expect(Near(core::HeatingBurnToday(config, house, -10.0F), 1.12F), "dilapidated: 1.12");
  const core::UnitRow school = Unit(kSchool);
  failures += Expect(Near(core::HeatingBurnToday(config, school, -10.0F), 3.2F), "school: 3.2");
  return failures;
}

int CheckTheDay() {
  int failures = 0;
  const core::HeatingConfig config = Config();
  core::WorldState world;
  world.weather.air_temperature_celsius = -10.0F;
  const core::UnitId lived = core::AppendRow(world.units, Unit(kHouse));
  core::AppendRow(world.units, Unit(kHouse));  // empty: nobody's house
  core::AppendRow(world.units, Unit(kSchool));
  core::AppendRow(world.units, Unit(kBarn));  // no stove
  core::FamilyRow family;
  family.house = lived;
  core::AppendRow(world.families, family);

  core::RunHeatingBurn(config, 4.0F, 16.0F, world);
  failures += Expect(world.heating.seeded == 1, "seeded on the first day");
  failures += Expect(Near(world.units.rows[0].heat_buffer_m3, 2.0F - 0.8F), "house: 2 − 0.8");
  failures += Expect(world.units.rows[1].heat_buffer_m3 == 0.0F, "empty house: no seed");
  failures += Expect(world.units.rows[2].heat_buffer_m3 == 0.0F, "school: no seed");
  failures += Expect(world.heating.probe_want_unit_days == 2, "two units wanted fuel");
  failures += Expect(world.heating.probe_empty_unit_days == 1, "the school's day short");
  failures +=
      Expect(world.heating.probe_want_house_days == 1 && world.heating.probe_empty_house_days == 0,
             "one house day, covered");
  failures += Expect(std::fabs(world.heating.probe_short_m3 - 3.2) < 1e-4, "short: 3.2 m³");

  // The second day: no second seed; the house burns its 1.2 down to 0.4, then a short day.
  core::RunHeatingBurn(config, 4.0F, 16.0F, world);
  failures += Expect(Near(world.units.rows[0].heat_buffer_m3, 0.4F), "house: 1.2 − 0.8");
  core::RunHeatingBurn(config, 4.0F, 16.0F, world);
  failures += Expect(world.units.rows[0].heat_buffer_m3 == 0.0F, "never below zero");
  failures += Expect(world.heating.probe_empty_house_days == 1, "the third day short");
  return failures;
}

/// Stage 2: a house under 3 m³ refills to 8 from a store holding firewood (500 kg a m³).
int CheckTheRefill() {
  int failures = 0;
  core::HeatingConfig config = Config();
  config.firewood = core::ResourceId{0};
  config.firewood_grams_per_m3 = 500'000.0;
  core::WorldState world;
  world.heating.seeded = 1;  // no start seed: the buffers begin empty
  world.weather.air_temperature_celsius = -10.0F;
  const core::UnitId lived = core::AppendRow(world.units, Unit(kHouse));
  core::UnitRow store = Unit(kBarn);
  store.stock = {5'000'000};  // 10 m³
  core::AppendRow(world.units, store);
  core::FamilyRow family;
  family.house = lived;
  core::AppendRow(world.families, family);

  core::RunHeatingBurn(config, 4.0F, 16.0F, world);
  // 8 m³ brought, 0.8 burnt: 7.2 left; the store keeps 2 m³.
  failures += Expect(Near(world.units.rows[0].heat_buffer_m3, 7.2F), "refilled to 8, burnt 0.8");
  failures += Expect(world.units.rows[1].stock[0] == 1'000'000, "the store gave 8 m³");
  failures += Expect(world.heating.probe_refills == 1 && world.heating.probe_refill_short == 0,
                     "one whole refill");
  // 7.2 → 2.4 over six days (no refill at 3.2), then day 8 starts under 3: wants 5.6, gets 2.
  for (int day = 0; day < 7; ++day) {
    core::RunHeatingBurn(config, 4.0F, 16.0F, world);
  }
  failures += Expect(world.units.rows[1].stock[0] == 0, "the store emptied");
  failures += Expect(world.heating.probe_refills == 2 && world.heating.probe_refill_short == 1,
                     "the second refill came short");
  failures += Expect(Near(world.units.rows[0].heat_buffer_m3, 2.4F + 2.0F - 0.8F), "2.4 + 2 − 0.8");
  // A warm day: nothing wanted, nothing refilled.
  world.weather.air_temperature_celsius = 15.0F;
  core::RunHeatingBurn(config, 4.0F, 16.0F, world);
  failures += Expect(world.heating.probe_refills == 2, "no refill above the threshold");
  return failures;
}

/// Stage 3c: an adult's house 2 km from a stand gathers half a trip (0.05 m³) a cold day; a
/// child-only house and a house past 3 km gather nothing.
int CheckTheDeadwood() {
  int failures = 0;
  const core::HeatingConfig config = Config();
  core::WorldState world;
  world.heating.seeded = 1;
  world.calendar.day = 400;
  world.weather.air_temperature_celsius = -10.0F;
  core::TimberStandRow stand;
  stand.position = core::Vec2{.x = 0.0F, .y = 0.0F};
  stand.stock_m3 = 100.0F;
  core::AppendRow(world.stands, stand);
  core::UnitRow near = Unit(kHouse);
  near.position = core::Vec2{.x = 2000.0F, .y = 0.0F};
  near.heat_buffer_m3 = 5.0F;  // above the refill line: only the deadwood moves it
  const core::UnitId near_id = core::AppendRow(world.units, near);
  core::UnitRow far = Unit(kHouse);
  far.position = core::Vec2{.x = 3500.0F, .y = 0.0F};
  far.heat_buffer_m3 = 5.0F;
  const core::UnitId far_id = core::AppendRow(world.units, far);
  core::FamilyRow near_family;
  near_family.house = near_id;
  const core::FamilyId near_family_id = core::AppendRow(world.families, near_family);
  core::FamilyRow far_family;
  far_family.house = far_id;
  const core::FamilyId far_family_id = core::AppendRow(world.families, far_family);
  core::ResidentRow adult;  // 400 days at x4 = 33 years
  adult.family = near_family_id;
  adult.birth_day = 0;
  core::AppendRow(world.residents, adult);
  core::ResidentRow far_adult = adult;
  far_adult.family = far_family_id;
  core::AppendRow(world.residents, far_adult);

  core::RunHeatingBurn(config, 4.0F, 16.0F, world);
  failures += Expect(Near(world.units.rows[0].heat_buffer_m3, 5.0F + 0.05F - 0.8F),
                     "2 km: half a trip, then the burn");
  failures += Expect(Near(world.units.rows[1].heat_buffer_m3, 5.0F - 0.8F), "3.5 km: nothing");
  failures += Expect(std::fabs(world.heating.probe_deadwood_m3 - 0.05) < 1e-4, "0.05 m³ counted");
  return failures;
}

/// Stage 4: an ordinary adult in a cold house (Epoch I: thresholds 40 / 80) - five cold nights
/// to «Холодно», ten to «Замёрз» with its health hit (chance forced to 100 %), and a warm night
/// back; a hardy one (+15) is not yet cold at 40.
int CheckTheCold() {
  int failures = 0;
  core::HeatingConfig config = Config();
  config.cold_frozen_sick_chance_pct = 100.0F;
  config.cold_sickness_pct_day = 0.0F;
  core::WorldState world;
  world.calendar.day = 400;
  world.weather.air_temperature_celsius = -10.0F;
  const core::UnitId house = core::AppendRow(world.units, Unit(kHouse));  // empty stove
  core::FamilyRow family;
  family.house = house;
  const core::FamilyId family_id = core::AppendRow(world.families, family);
  core::ResidentRow ordinary;
  ordinary.family = family_id;
  ordinary.birth_day = 0;  // 33 years at x4
  ordinary.stamina = 50.0F;
  ordinary.sportiness = 30.0F;
  ordinary.health = 70.0F;
  core::AppendRow(world.residents, ordinary);
  core::ResidentRow hardy = ordinary;
  hardy.stamina = 80.0F;
  hardy.sportiness = 50.0F;
  core::AppendRow(world.residents, hardy);
  // Eight more adults of 60..67 (no house): the bands are quantiles - the bottom 20 % cut is
  // 62, the top 15 % starts at the hardy's 130; the ordinary's 80 is neither.
  for (int other = 0; other < 8; ++other) {
    core::ResidentRow filler;
    filler.birth_day = 0;
    filler.stamina = 60.0F + static_cast<float>(other);
    filler.health = 70.0F;
    core::AppendRow(world.residents, filler);
  }

  for (int night = 0; night < 5; ++night) {
    core::RunResidentsCold(config, 4.0F, 16.0F, true, world);
  }
  failures += Expect(Near(world.residents.rows[0].cold, 40.0F), "five cold nights: 40");
  failures += Expect(world.residents.rows[0].cold_stage == 1, "ordinary: «Холодно» at 40");
  failures += Expect(world.residents.rows[1].cold_stage == 0, "hardy: not yet at 40");
  for (int night = 0; night < 5; ++night) {
    core::RunResidentsCold(config, 4.0F, 16.0F, true, world);
  }
  failures += Expect(world.residents.rows[0].cold_stage == 2, "ordinary: «Замёрз» at 80");
  failures += Expect(Near(world.residents.rows[0].health, 62.0F), "one hit of 8, once");
  // The fillers (no house, the two weakest frozen first) are hit too: the count is at least the
  // ordinary's one.
  failures += Expect(world.heating.probe_health_hits >= 1, "the hit counted");
  // A warm night (fuel in the stove): 80 − 20 = 60, back to «Холодно».
  world.units.rows[0].heat_buffer_m3 = 5.0F;
  core::RunResidentsCold(config, 4.0F, 16.0F, true, world);
  failures += Expect(Near(world.residents.rows[0].cold, 60.0F), "a warm night: −20");
  failures += Expect(world.residents.rows[0].cold_stage == 1, "back to «Холодно»");
  return failures;
}

}  // namespace

int CheckTheHeatingBurn() {
  return CheckTheFormula() + CheckTheDay() + CheckTheRefill() + CheckTheDeadwood() + CheckTheCold();
}
