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

  core::RunHeatingBurn(config, world);
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
  core::RunHeatingBurn(config, world);
  failures += Expect(Near(world.units.rows[0].heat_buffer_m3, 0.4F), "house: 1.2 − 0.8");
  core::RunHeatingBurn(config, world);
  failures += Expect(world.units.rows[0].heat_buffer_m3 == 0.0F, "never below zero");
  failures += Expect(world.heating.probe_empty_house_days == 1, "the third day short");
  return failures;
}

}  // namespace

int CheckTheHeatingBurn() {
  return CheckTheFormula() + CheckTheDay();
}
