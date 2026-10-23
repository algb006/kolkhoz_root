// The checks of loose hay weathering (hay_weathering_checks.h).
//
// THE KNOWN ANSWERS, by hand from the shipped farming.csv (0 / 0.10 / 0.03 / 0.10 a game month,
// a month of 4 days): 100 t of hay on a field in September loses 2.5 t a night, in July nothing (a
// drying swath), in January 0.75 t, in April 2.5 t; the same 100 t in a rick loses nothing (hay has
// no shelf life in a store).
#include "hay_weathering_checks.h"

#include <cstdint>
#include <iostream>
#include <string>

#include "core_common/calendar.h"
#include "core_common/land_state.h"
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
    std::cout << "FAIL: hay weathering: " << label << '\n';
    return 1;
  }
  return 0;
}

}  // namespace

int CheckTheHayWeathersOnTheField() {
  int failures = 0;
  std::string error;
  const auto tables = core::LoadTableSet(KOLKHOZ_TABLES_DIR, &error);
  core::ProductionConfig config;
  if (Expect(tables != nullptr && core::ParseProductionConfig(*tables, config, error),
             "the shipped tables parse") != 0) {
    std::cout << error << '\n';
    return 1;
  }
  constexpr core::Grams kHundred = 100'000'000;
  const auto lost_on_field = [&config](core::Month month) {
    core::WorldState world;
    world.calendar.date.month = month;
    core::FieldRow field;
    field.kind = core::LandKind::kMeadow;
    field.reaped_grams = kHundred;
    field.reaped_resource = config.hay_resource;
    field.haul_days_remaining = 2.0F;
    core::AppendRow(world.fields, field);
    core::SpoilFieldHeaps(config, world);
    const core::Grams booked = config.hay_resource.value < world.ledger.current.spoiled.size()
                                   ? world.ledger.current.spoiled[config.hay_resource.value]
                                   : 0;
    const core::Grams lost = kHundred - world.fields.rows[0].reaped_grams;
    return lost == booked ? lost : -1;
  };
  const core::Grams september = lost_on_field(core::Month::kSeptember);
  const core::Grams july = lost_on_field(core::Month::kJuly);
  const core::Grams january = lost_on_field(core::Month::kJanuary);
  const core::Grams december = lost_on_field(core::Month::kDecember);
  const core::Grams april = lost_on_field(core::Month::kApril);
  std::cout << "  hay weathering, 100 t on a field, one night, kg: Sep " << september / 1000
            << ", Jul " << july / 1000 << ", Jan " << january / 1000 << ", Dec " << december / 1000
            << ", Apr " << april / 1000 << '\n';
  failures +=
      Expect(september == 2'500'000 && april == 2'500'000,
             "autumn and spring take 10 % a month, 2.5 t a night of 100 t, booked as spoiled");
  failures += Expect(january == 750'000 && december == 750'000,
                     "winter takes 3 % a month, 0.75 t a night, December winter's");
  failures += Expect(july == 0, "a drying swath in summer keeps (#1015, the first pair's lesson)");

  core::WorldState rick_world;
  rick_world.calendar.date.month = core::Month::kSeptember;
  core::UnitRow rick;
  rick.type = core::UnitTypeId{0};
  rick.level = 1;
  rick.stock.assign(static_cast<std::size_t>(config.hay_resource.value) + 1U, 0);
  rick.stock[config.hay_resource.value] = kHundred;
  core::AppendRow(rick_world.units, rick);
  core::SpoilStores(config, rick_world);
  failures += Expect(rick_world.units.rows[0].stock[config.hay_resource.value] == kHundred,
                     "hay in a store keeps: weathering is the field's, not the rick's");
  return failures;
}
