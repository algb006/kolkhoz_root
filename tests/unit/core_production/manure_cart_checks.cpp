// The checks of manure carted heap -> field on sledges (manure_cart_checks.h).

#include "manure_cart_checks.h"

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
#include "manure_cart.h"
#include "production_config.h"
#include "stock_ops.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

/// An idle arable field of 10 ha in the rotation, `km` east of the heap.
std::uint32_t AddField(core::WorldState& world, float km, float fertility) {
  core::FieldRow field;
  field.kind = core::LandKind::kArable;
  field.phase = core::FieldPhase::kIdle;
  field.area_ga = 10.0F;
  field.fertility = fertility;
  field.rotation_assigned = 1;
  field.center = {.x = km * 1000.0F, .y = 0.0F};
  core::AppendRow(world.fields, field);
  return static_cast<std::uint32_t>(world.fields.rows.size() - 1U);
}

}  // namespace

int CheckManureIsCartedOnSledges() {
  int failures = 0;
  std::string error;
  const auto tables = core::LoadTableSet(KOLKHOZ_TABLES_DIR, &error);
  core::ProductionConfig config;
  if (Expect(tables != nullptr && core::ParseProductionConfig(*tables, config, error),
             "manure carting: the shipped tables parse") != 0) {
    std::cout << error << '\n';
    return 1;
  }
  const core::ResourceId manure = config.manure_resource;
  // January, snow lying: a heap at the origin and four fields - three below
  // the mark (70) at 3, 1 and 2 km, one above it nearest of all.
  core::WorldState world;
  world.calendar.tick = 0;
  core::RefreshCalendarCaches(world.calendar);
  world.weather.snow_cover_days = 5;
  core::UnitRow heap;
  heap.type = config.compost_heap_type;
  heap.level = 1;
  heap.stock.assign(static_cast<std::size_t>(manure.value) + 1U, 0);
  const core::Grams dose = core::GramsFromKilograms(config.farming.manure_norm_kg_per_ha * 10.0F);
  heap.stock[manure.value] = (5 * dose) / 2;  // two and a half doses
  core::AppendRow(world.units, heap);
  const std::uint32_t far = AddField(world, 3.0F, 60.0F);
  const std::uint32_t near = AddField(world, 1.0F, 60.0F);
  const std::uint32_t rich = AddField(world, 0.5F, 75.0F);
  const std::uint32_t middle = AddField(world, 2.0F, 55.0F);

  // THE ORDER: below the mark, nearest by road first; above it none while any
  // below is short - the near field whole, the middle one whole, the far one
  // the half that is left, the rich near one nothing.
  core::BookManureDoses(config, world);
  const auto booked = [&world](std::uint32_t row) {
    return world.fields.rows[row].manure_dose.booked;
  };
  failures += Expect(booked(near) == dose && booked(middle) == dose && booked(far) == dose / 2 &&
                         booked(rich) == 0,
                     "manure carting: the fields below the mark are booked nearest first, the "
                     "last one partly, and the rich near field none while any below is short");
  failures += Expect(core::StockOf(world.units.rows[0].stock, manure) == (5 * dose) / 2 &&
                         world.fields.rows[near].manure_applied == 0,
                     "manure carting: booking moves no gram - the heap keeps it all and no field "
                     "is manured at the turn");

  // THE PRICE: none on a bare day; on a sledge day every booked field.
  core::WorldState bare = world;
  bare.weather.snow_cover_days = 0;
  core::PriceManureDoses(config, bare);
  failures += Expect(!(bare.fields.rows[near].manure_dose.days_remaining > 0.0F),
                     "manure carting: no field is priced on a day with no snow");
  core::PriceManureDoses(config, world);
  const core::ManureDoseSeam& near_seam = world.fields.rows[near].manure_dose;
  failures += Expect(
      near_seam.days_remaining > 0.0F && near_seam.days_written == near_seam.days_remaining &&
          world.fields.rows[far].manure_dose.days_remaining > near_seam.days_remaining &&
          !(world.fields.rows[rich].manure_dose.days_remaining > 0.0F),
      "manure carting: on a sledge day each booked field is priced, the far one "
      "dearer, the unbooked one not at all");

  // THE SETTLE: the near field's carting drained whole moves its whole dose.
  world.fields.rows[near].manure_dose.days_remaining = 0.0F;
  core::SettleManureDoses(config, world);
  failures += Expect(world.fields.rows[near].manure_dose.due == 0 &&
                         world.fields.rows[near].manure_applied == 100 &&
                         core::StockOf(world.units.rows[0].stock, manure) == (3 * dose) / 2 &&
                         world.ledger.current.manure_carted == dose &&
                         !core::ManureDoseStands(world.fields.rows[near]),
                     "manure carting: a drained dose leaves the heap, lies on its field at its "
                     "full share, and is booked as carted");

  // THE LATE DOSE: the middle field's ploughing opens with half of it carted.
  core::ManureDoseSeam& middle_seam = world.fields.rows[middle].manure_dose;
  middle_seam.days_remaining = middle_seam.days_written / 2.0F;
  core::SettleManureDoses(config, world);
  const core::Grams middle_due = world.fields.rows[middle].manure_dose.due;
  core::CancelLateManure(world, world.fields.rows[middle]);
  failures += Expect(middle_due > 0 && middle_due < dose &&
                         world.ledger.current.manure_late == middle_due &&
                         world.fields.rows[middle].manure_dose.booked == 0 &&
                         world.fields.rows[middle].manure_applied > 0 &&
                         world.fields.rows[middle].manure_applied < 100,
                     "manure carting: a dose not all on its field when its ploughing opens is "
                     "cancelled, counted late, and the field keeps the share that arrived");

  // THE CARRY-OVER: next turn re-books what the heap still holds; nothing is
  // lost and nothing spread free.
  const core::Grams held = core::StockOf(world.units.rows[0].stock, manure);
  core::BookManureDoses(config, world);
  const core::Grams far_share = held < dose ? held : dose;
  const core::Grams rest = held - far_share < dose ? held - far_share : dose;
  failures += Expect(core::StockOf(world.units.rows[0].stock, manure) == held && held > 0 &&
                         booked(near) == 0 && booked(middle) == 0 && booked(far) == far_share &&
                         booked(rich) == rest,
                     "manure carting: the next turn books the heap's carry-over - the manured "
                     "fields out, the far one whole, the rest to the rich one once none below "
                     "the mark is short");

  // THE REAPED HEAP FIRST: a dose does not stand while a harvest lies there.
  world.fields.rows[far].reaped_grams = 1'000'000;
  failures += Expect(!core::ManureDoseStands(world.fields.rows[far]),
                     "manure carting: a field's dose waits while its reaped heap lies on it");
  return failures;
}
