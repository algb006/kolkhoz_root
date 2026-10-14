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
  // A SECOND RICK, NEARER THE MEADOW and later in the rows (routing stage В3):
  // the meadow's hay goes to the nearest by road, not the first by row.
  const core::UnitId near_stack =
      AddUnit(world, *tables, "hay_stack", {.x = 900.0F, .y = 0.0F}, resources);
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
  // IN THE NEAREST RICK (0.37.252; field_haul.h, HeapRickRow) - a hundred
  // metres off, not the first by row at the far end, and NOT IN THE MANGER,
  // where all of it went from 0.37.119: a farm's hay is carted to its buffer.
  failures += Expect(HayAt(world, near_stack, hay) == season / 2 && HayAt(world, stack, hay) == 0 &&
                         HayAt(world, manger, hay) == 0,
                     "meadow hay: carted through, the whole of it is in the nearest rick — none in "
                     "the rick first by row, none in the manger");

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

  // THE FIELD's STRAW GETS ITS CART (routing stage В4, item 4): two tonnes of
  // straw lying at an arable field whose grain is in - that evening the straw
  // becomes the field's load, priced to the rick nearest by road; carted
  // through, it is in that rick and none is left at the field.
  const core::ResourceId straw = config.straw_resource;
  core::FieldRow stubble;
  stubble.kind = core::LandKind::kArable;
  stubble.center = core::Vec2{.x = 950.0F, .y = 0.0F};
  stubble.area_ga = 5.0F;
  stubble.straw_grams = 2'000'000;
  core::AppendRow(world.fields, stubble);
  core::SettleHauling(config, world);
  const core::FieldRow& turned = world.fields.rows.back();
  failures +=
      Expect(turned.reaped_resource.value == straw.value && turned.reaped_grams == 2'000'000 &&
                 turned.straw_grams == 0 && turned.haul_days_remaining > 0.0F,
             "field straw: the evening the grain is in, the straw is the field's load and "
             "its carting is priced");
  world.fields.rows.back().haul_days_remaining = 0.0F;
  core::SettleHauling(config, world);
  const auto straw_at = [&](core::UnitId unit) {
    const core::UnitRow& row = world.units.rows[core::FindRow(world.units, unit)];
    return straw.value < row.stock.size() ? row.stock[straw.value] : 0;
  };
  std::cout << "  field straw: carted - in the nearest rick " << straw_at(near_stack) / 1000
            << " kg, at the field " << world.fields.rows.back().reaped_grams / 1000 << " kg\n";
  failures +=
      Expect(straw_at(near_stack) == 2'000'000 && world.fields.rows.back().reaped_grams == 0,
             "field straw: carted through, the whole of it is in the nearest rick");
  return failures;
}
