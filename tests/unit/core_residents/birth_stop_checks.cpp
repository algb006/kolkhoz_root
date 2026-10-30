/// @file
/// @brief Unit cases of the births' hard stops as the book counts them
/// (birth_stop_checks.h).

#include "birth_stop_checks.h"

#include <cstdint>
#include <iostream>

#include "core_common/calendar.h"
#include "core_common/ledger_state.h"
#include "core_common/random.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "demography.h"
#include "life_config.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

/// Twelve couples, every family at `satiety_year`, a birth nearly sure every
/// day; twenty days lived. Returns the year's book as it stands.
core::YearLedger LiveTwentyDays(float satiety_year) {
  core::LifeConfig config;
  config.definitions.units.is_housing = {1};
  config.migration_per_year = 0.0F;
  config.marriage_chance_percent_per_day = 0.0F;
  for (core::EpochDemography& epoch : config.epochs) {
    epoch.children_per_family = 1.0e6F;
    epoch.child_mortality_percent = 0.0F;
  }
  core::WorldState world;
  world.world_seed = 1937;
  world.rng = core::SeedRngState(1937, 0);
  for (std::uint32_t i = 0; i < 12; ++i) {
    core::UnitRow house;
    house.level = 1;
    const core::UnitId house_id = AppendRow(world.units, house);
    core::FamilyRow family;
    family.house = house_id;
    family.satisfaction = 60.0F;
    family.satiety_year_mean = satiety_year;
    const core::FamilyId family_id = AppendRow(world.families, family);
    world.units.rows.back().household = family_id;
    core::ResidentRow wife;
    wife.family = family_id;
    wife.sex = core::Sex::kFemale;
    wife.birth_day = -300 - static_cast<std::int32_t>(i);
    wife.satiety = 80.0F;
    wife.health = 80.0F;
    const core::ResidentId wife_id = AppendRow(world.residents, wife);
    core::ResidentRow husband = wife;
    husband.sex = core::Sex::kMale;
    husband.spouse = wife_id;
    const core::ResidentId husband_id = AppendRow(world.residents, husband);
    world.residents.rows[FindRow(world.residents, wife_id)].spouse = husband_id;
  }
  for (core::SimDay day = 1; day <= 20; ++day) {
    world.calendar.tick = static_cast<std::uint64_t>(day) * core::kTicksPerDay;
    core::RefreshCalendarCaches(world.calendar);
    core::RunDemographyDay(config, world);
  }
  return world.ledger.current;
}

}  // namespace

/// THE STOPS ARE COUNTED WHERE THEY FIRE (#1205; boss, boss-core-1205-birth-
/// prints [3]). econ read births falling from 35 to 4 a year in the 33-year
/// canon and could not tell a stop from a missing draw: a probe that copied
/// the draw would agree with its error. A known direction: a family's year
/// satiety under the stop raises the satiety counter, and nothing is born;
/// the same village fed counts no stop and bears.
int CheckTheBirthStopsAreBooked() {
  int failures = 0;
  const core::YearLedger hungry = LiveTwentyDays(10.0F);
  const core::YearLedger fed = LiveTwentyDays(80.0F);
  std::cout << "  birth stops: hungry drawn " << hungry.births_drawn << ", stopped by satiety "
            << hungry.births_stopped_satiety << ", born " << hungry.births << "; fed drawn "
            << fed.births_drawn << ", stopped " << fed.births_stopped_satiety << ", born "
            << fed.births << '\n';
  failures += Expect(hungry.births_drawn > 0 &&
                         hungry.births_stopped_satiety == hungry.births_drawn && hungry.births == 0,
                     "birth stops: a hungry year stops every drawn birth, and the book names the "
                     "satiety");
  failures +=
      Expect(fed.births_drawn > 0 && fed.births_stopped_satiety == 0 &&
                 fed.births_stopped_health == 0 && fed.births_stopped_away == 0 && fed.births > 0,
             "birth stops: a fed year stops none and bears");
  return failures;
}
