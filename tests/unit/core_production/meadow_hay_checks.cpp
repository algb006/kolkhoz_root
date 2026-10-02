// The checks of the meadow's hay lying at the meadow (meadow_hay_checks.h).

#include "meadow_hay_checks.h"

#include <cstdint>
#include <iostream>
#include <string>

#include "core_common/calendar.h"
#include "core_common/land_state.h"
#include "core_common/ledger_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "field_haul.h"
#include "field_work.h"
#include "production_config.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

/// A standing unit of the shipped table's type `key`, with an empty stock.
core::UnitId AddUnit(core::WorldState& world,
                     const core::ITableSet& tables,
                     const char* key,
                     core::Vec2 position,
                     std::size_t resources) {
  core::UnitRow unit;
  unit.type = core::UnitTypeId{
      static_cast<std::uint16_t>(tables.FindTable("unit_types")->FindRowByKey(key))};
  unit.level = 1;
  unit.position = position;
  unit.stock.assign(resources, 0);
  return core::AppendRow(world.units, unit);
}

core::Grams HayAt(const core::WorldState& world, core::UnitId unit, core::ResourceId hay) {
  const core::UnitRow& row = world.units.rows[core::FindRow(world.units, unit)];
  return hay.value < row.stock.size() ? row.stock[hay.value] : 0;
}

}  // namespace

int CheckMeadowHayLiesAtTheMeadow() {
  int failures = 0;
  std::string error;
  const auto tables = core::LoadTableSet(KOLKHOZ_TABLES_DIR, &error);
  core::ProductionConfig config;
  if (Expect(tables != nullptr && core::ParseProductionConfig(*tables, config, error),
             "meadow hay: the shipped tables parse") != 0) {
    std::cout << error << '\n';
    return 1;
  }
  const core::ResourceId hay = config.hay_resource;
  core::WorldState world;
  world.calendar.tick = 25U * core::kTicksPerDay;  // June
  core::RefreshCalendarCaches(world.calendar);
  const std::size_t resources = config.feed_values.size();
  // The settlement's manger and its hay stack, both empty: until 0.37.119 the
  // mown share went into the first of them in the hour it was cut.
  const core::UnitId manger =
      AddUnit(world, *tables, "cattle_yard", {.x = 0.0F, .y = 0.0F}, resources);
  const core::UnitId stack =
      AddUnit(world, *tables, "hay_stack", {.x = 50.0F, .y = 0.0F}, resources);
  core::FieldRow meadow;
  meadow.kind = core::LandKind::kMeadow;
  meadow.center = core::Vec2{.x = 1000.0F, .y = 0.0F};
  meadow.area_ga = 10.0F;
  meadow.phase = core::FieldPhase::kHarvest;
  const float total_days = config.farming.meadow_mow_days_per_ha * meadow.area_ga;
  meadow.work_days_remaining = total_days * 0.5F;
  core::AppendRow(world.fields, meadow);
  const core::Grams season =
      core::GramsFromKilograms(config.farming.meadow_yield_kg_per_ha * meadow.area_ga);

  core::LayMownShare(config, world, world.fields.rows[0]);
  const core::FieldRow& mown = world.fields.rows[0];
  std::cout << "  meadow hay: half mown — at the meadow " << mown.reaped_grams / 1000
            << " kg, in the "
            << "manger " << HayAt(world, manger, hay) / 1000 << " kg, in the stack "
            << HayAt(world, stack, hay) / 1000 << " kg, carting written " << mown.haul_days_written
            << " norm-days\n";
  failures += Expect(mown.reaped_grams == season / 2 && mown.reaped_resource.value == hay.value,
                     "meadow hay: the mown half of the season's hay lies at the meadow, named hay");
  failures += Expect(HayAt(world, manger, hay) == 0 && HayAt(world, stack, hay) == 0,
                     "meadow hay: nothing reaches the manger or the stack in the hour it is cut");
  failures += Expect(core::AmountOf(world.ledger.current.harvest, hay) == season / 2 &&
                         core::AmountOf(world.ledger.current.lost_no_room, hay) == 0,
                     "meadow hay: the cut is booked as harvest, and nothing is lost for want of "
                     "room — the meadow holds it");
  failures +=
      Expect(mown.haul_days_written > 0.0F && mown.haul_days_remaining == mown.haul_days_written,
             "meadow hay: the carting is priced the day the hay is laid");

  // THE ALLOWED SIDE: carted, the hay is in the stores and the meadow is
  // clear — the seam drained by the carters, the evening's settlement moving
  // the share they carried.
  world.fields.rows[0].haul_days_remaining = 0.0F;
  core::SettleHauling(config, world);
  failures += Expect(world.fields.rows[0].reaped_grams == 0,
                     "meadow hay: carted through, none is left at the meadow");
  // IN THE MANGER, where it went by itself before — not in the stack beside
  // it: the delivery changes who brings the hay, not where it lies.
  failures += Expect(HayAt(world, manger, hay) == season / 2 && HayAt(world, stack, hay) == 0,
                     "meadow hay: carted through, the whole of it is in the manger — where the "
                     "mown hay always went — and none in the stack");

  // THE OTHER LAND: the hay of sown grass off an ARABLE field goes through
  // the store door as it did, to the stack, and not to the manger.
  core::FieldRow sown;
  sown.kind = core::LandKind::kArable;
  sown.center = core::Vec2{.x = 800.0F, .y = 0.0F};
  sown.area_ga = 5.0F;
  sown.reaped_grams = 1'000'000;
  sown.reaped_resource = hay;
  sown.haul_days_written = 1.0F;
  sown.haul_days_remaining = 0.0F;
  core::AppendRow(world.fields, sown);
  const core::Grams manger_before = HayAt(world, manger, hay);
  core::SettleHauling(config, world);
  std::cout << "  meadow hay: a tonne of sown-grass hay off an arable field, carted — in the "
            << "manger " << (HayAt(world, manger, hay) - manger_before) / 1000
            << " kg more, in the "
            << "stack " << HayAt(world, stack, hay) / 1000 << " kg\n";
  failures += Expect(HayAt(world, manger, hay) == manger_before,
                     "meadow hay: the hay of an arable field does not go to the manger — its door "
                     "is the stores', as before");
  return failures;
}
