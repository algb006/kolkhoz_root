// The checks of manure carted heap -> field on sledges (manure_cart_checks.h).

#include "manure_cart_checks.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>

#include "core_common/calendar.h"
#include "core_common/herd_state.h"
#include "core_common/land_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "herd_system.h"
#include "manure_cart.h"
#include "production_config.h"
#include "stock_ops.h"
#include "team_oat_plan.h"

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

  // THE TEAM'S OATS (C2.2, R8): with no oats over the ladder no dose is
  // priced, and the windows say why.
  core::WorldState hungry = world;
  core::PriceManureDoses(config, hungry);
  failures += Expect(!(hungry.fields.rows[near].manure_dose.days_remaining > 0.0F) &&
                         hungry.fields.rows[near].manure_dose.priced == 0 &&
                         hungry.traction_watch.manure_waits_for_oats == 1,
                     "manure carting: with no oats over the ladder no dose is priced, and the "
                     "manure waits for the oats");
  // A thousand tonnes of oats in store and no horse to hold them for: every
  // standing dose is bought whole.
  const core::ResourceId oats = core::TeamWorkFeed(config);
  core::UnitRow& store = world.units.rows[0];
  if (store.stock.size() <= oats.value) {
    store.stock.resize(static_cast<std::size_t>(oats.value) + 1U, 0);
  }
  store.stock[oats.value] = 1'000'000'000;

  // THE PRICE: none on a bare day; on a sledge day every booked field.
  core::WorldState bare = world;
  bare.weather.snow_cover_days = 0;
  core::PriceManureDoses(config, bare);
  failures += Expect(!(bare.fields.rows[near].manure_dose.days_remaining > 0.0F),
                     "manure carting: no field is priced on a day with no snow");
  core::PriceManureDoses(config, world);
  failures += Expect(
      world.fields.rows[near].manure_dose.priced == world.fields.rows[near].manure_dose.due &&
          world.traction_watch.manure_waits_for_oats == 0 && world.traction_watch.winter_hold == 0,
      "manure carting: with oats to spare and no horse to hold them for, a dose "
      "is priced whole and nothing waits");
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

  // THE NEXT BOOKING (a later sledge day, or the turn): a standing booking
  // stands and a short one is topped up, from the heap's manure no booking
  // holds - nothing lost, nothing spread free (boss, the #1056 thread [140]:
  // until then the turn cleared every booking).
  const core::Grams held = core::StockOf(world.units.rows[0].stock, manure);
  const core::Grams unbooked = held - world.fields.rows[far].manure_dose.due;
  const core::Grams top_up = unbooked < dose / 2 ? unbooked : dose / 2;
  const core::Grams rest = unbooked - top_up < dose ? unbooked - top_up : dose;
  core::BookManureDoses(config, world);
  failures +=
      Expect(core::StockOf(world.units.rows[0].stock, manure) == held && unbooked > 0 &&
                 booked(near) == dose && world.fields.rows[near].manure_dose.due == 0 &&
                 booked(middle) == 0 && booked(far) == dose / 2 + top_up && booked(rich) == rest,
             "manure carting: the next booking keeps the carted dose, leaves the ploughed "
             "field out, tops the far one's half up from the unbooked heap, and gives the "
             "rest to the rich one only once none below the mark is short");

  // THE REAPED HEAP FIRST: a dose does not stand while a harvest lies there.
  world.fields.rows[far].reaped_grams = 1'000'000;
  failures += Expect(!core::ManureDoseStands(world.fields.rows[far]),
                     "manure carting: a field's dose waits while its reaped heap lies on it");
  world.fields.rows[far].reaped_grams = 0;

  // THE WINTER'S HOLD (C2.2, R8 (c)): in year 1, with no book, every adult
  // horse at a full work day's oats, every day from tomorrow to the first
  // spring sowing window.
  core::HerdRow team;
  team.kind = config.horse_kind;
  team.adult_count = 10;
  team.unit = world.units.row_ids[0];
  core::AppendRow(world.herds, team);
  std::uint32_t first = core::kDaysPerYear;
  for (const core::CropDef& crop : config.crops) {
    if (!crop.is_winter) {
      first =
          std::min(first, static_cast<std::uint32_t>(crop.sow_from_month) * core::kDaysPerMonth);
    }
  }
  const std::uint32_t today = world.calendar.day % core::kDaysPerYear;
  const std::uint32_t days =
      today < first ? first - today - 1U : core::kDaysPerYear - today - 1U + first;
  const core::Grams horse_day =
      core::TeamWorkFeedGrams(config,
                              config.livestock[config.horse_kind.value].feed_units_per_game_day *
                                  config.farming.traction_full_ration_share);
  const core::Grams hold = core::WinterHoldGrams(config, world);
  const core::Grams expected_hold =
      core::GramsFromFloat(10.0F * static_cast<float>(days) * static_cast<float>(horse_day));
  failures += Expect(days > 0 && horse_day > 0 && hold == expected_hold,
                     "manure carting: year 1's winter hold is every adult horse at a full work "
                     "day's oats for every day to the first spring sowing window");

  // R8 (a) and (b): today's oats cover a trip, the hold to the plough does
  // not - no trip; the oats above the hold buy only their own cart-days, the
  // nearest dose first, so the winter's other carting keeps its oats.
  const core::ResourceAmounts allowance = core::HerdFeedAllowance(config, world);
  const core::Grams over_ladder = allowance[oats.value];
  store.stock[oats.value] -= over_ladder - (hold - horse_day);  // a horse-day short of the hold
  core::PriceManureDoses(config, world);
  bool none_priced = true;
  for (const core::FieldRow& field : world.fields.rows) {
    none_priced = none_priced && !(field.manure_dose.days_remaining > 0.0F);
  }
  failures += Expect(none_priced && world.traction_watch.manure_waits_for_oats == 1 &&
                         world.traction_watch.winter_hold == hold,
                     "manure carting: oats today but short of the hold to the plough - no manure "
                     "trip, and the manure waits for the oats");
  store.stock[oats.value] += 3 * horse_day;  // two horse-days over the hold
  core::PriceManureDoses(config, world);
  float priced_days = 0.0F;
  int in_part = 0;
  for (const core::FieldRow& field : world.fields.rows) {
    priced_days += field.manure_dose.days_remaining;
    in_part += field.manure_dose.priced > 0 && field.manure_dose.priced < field.manure_dose.due;
  }
  failures += Expect(priced_days > 1.9F && priced_days < 2.1F && in_part == 1,
                     "manure carting: the oats over the hold buy their own two cart-days of a "
                     "standing dose, in part, and no more");
  return failures;
}
