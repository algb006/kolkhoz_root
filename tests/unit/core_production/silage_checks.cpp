// The checks of the autumn's silage laying (silage_checks.h).

#include "silage_checks.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "core_common/calendar.h"
#include "core_common/event_state.h"
#include "core_common/fund_ladder.h"
#include "core_common/land_state.h"
#include "core_common/ledger_state.h"
#include "core_common/order_state.h"
#include "core_common/quantities.h"
#include "core_common/rain_stops_work.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "field_haul.h"
#include "field_work.h"
#include "production_config.h"
#include "silage_laying.h"

namespace {

constexpr core::Grams kTonne = 1'000'000;

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

/// Within twenty kilograms: the laying's numbers pass through floats twice.
bool Near(core::Grams value, core::Grams wanted) {
  return std::llabs(value - wanted) <= 20'000;
}

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

/// A dry meadow of ten hectares in its cut, `mown` of it already mown.
core::FieldId AddMeadow(core::WorldState& world,
                        const core::ProductionConfig& config,
                        float x,
                        float mown) {
  core::FieldRow meadow;
  meadow.kind = core::LandKind::kMeadow;
  meadow.center = core::Vec2{.x = x, .y = 0.0F};
  meadow.area_ga = 10.0F;
  meadow.phase = core::FieldPhase::kHarvest;
  const float total_days = config.farming.meadow_mow_days_per_ha * meadow.area_ga;
  meadow.harvest_work_days = total_days;
  meadow.work_days_remaining = total_days * (1.0F - mown);
  return core::AppendRow(world.fields, meadow);
}

void SetDay(core::WorldState& world, std::uint32_t day) {
  world.calendar.tick = static_cast<core::Tick>(day) * core::kTicksPerDay;
  core::RefreshCalendarCaches(world.calendar);
}

core::OrderRow LayOrder(core::UnitId unit, core::Grams silage) {
  core::OrderRow order;
  order.kind = core::OrderKind::kLaySilage;
  order.unit = unit;
  order.amount = silage;
  return order;
}

core::Grams Held(const core::WorldState& world, core::UnitId unit, core::ResourceId resource) {
  return core::AmountOf(world.units.rows[core::FindRow(world.units, unit)].stock, resource);
}

}  // namespace

int CheckTheSilageLaying() {
  int failures = 0;
  std::string error;
  const auto tables = core::LoadTableSet(KOLKHOZ_TABLES_DIR, &error);
  core::ProductionConfig config;
  if (Expect(tables != nullptr && core::ParseProductionConfig(*tables, config, error),
             "silage: the shipped tables parse") != 0) {
    std::cout << error << '\n';
    return 1;
  }
  // One day's green carting, fixed for the arithmetic below whatever the
  // table's STUB says this week.
  config.farming.silage_cut_ahead_t = 25.0F;
  const core::Grams day_green = 25 * kTonne;
  const core::ResourceId green = config.green_mass_resource;
  const core::ResourceId silage = config.silage_resource;
  const core::ResourceId hay = config.hay_resource;
  const std::size_t resources = config.feed_values.size();
  // The first of September (day 32): the laying's window in the shipped table.
  constexpr std::uint32_t kFirstOfSeptember = 32;
  core::WorldState world;
  SetDay(world, kFirstOfSeptember);
  const core::UnitId trench =
      AddUnit(world, *tables, "silage_trench", {.x = 0.0F, .y = 0.0F}, resources);
  const core::UnitId yard =
      AddUnit(world, *tables, "cattle_yard", {.x = 100.0F, .y = 0.0F}, resources);
  // The NEAR meadow is half mown for hay and its hay not yet carted; the FAR
  // one no scythe has touched.
  const core::FieldId near_id = AddMeadow(world, config, 500.0F, 0.5F);
  const core::FieldId far_id = AddMeadow(world, config, 3000.0F, 0.0F);
  core::LayMownShare(config, world, world.fields.rows[0]);
  const core::Grams hay_season =
      core::GramsFromKilograms(config.farming.meadow_yield_kg_per_ha * 10.0F);
  const core::Grams green_season =
      core::GramsFromFloat(static_cast<float>(hay_season) * config.farming.green_mass_per_hay);

  // THE ORDER AND WHO IT IS NOT FOR.
  failures += Expect(core::IsSilageTrench(config, world.units.rows[0]) &&
                         !core::IsSilageTrench(config, world.units.rows[1]),
                     "silage: the trench is green mass's home, the cattle yard is not");
  failures +=
      Expect(core::OrderLaySilage(config, world, LayOrder(yard, 10 * kTonne)) ==
                     core::OrderRefusal::kRuleForbids &&
                 core::OrderLaySilage(config, world, LayOrder(core::UnitId{77}, kTonne)) ==
                     core::OrderRefusal::kNoSuchSubject &&
                 world.units.rows[1].silage_target == 0,
             "silage: a yard is refused by the rule, a unit that is not there as no subject");
  // The near meadow's uncut half: 22.5 t of silage is 30 t of green mass.
  const core::Grams wanted_green = green_season / 2;
  const core::Grams wanted_silage =
      core::GramsFromFloat(static_cast<float>(wanted_green) * config.farming.silage_per_green);
  failures += Expect(core::OrderLaySilage(config, world, LayOrder(trench, wanted_silage)) ==
                             core::OrderRefusal::kNone &&
                         Near(world.units.rows[0].silage_target, wanted_green),
                     "silage: the order stands on the trench in green mass - its silage over the "
                     "ripening's share");

  // THE FIRST MORNING: the NEAREST meadow is turned though hay lies on its
  // heap - the hay is put aside - and the far one is left to the hay's
  // scythe, because the near one's grass covers the day's mowing.
  core::RunSilageLaying(config, world);
  {
    const core::FieldRow& near_meadow = world.fields.rows[core::FindRow(world.fields, near_id)];
    const core::FieldRow& far_meadow = world.fields.rows[core::FindRow(world.fields, far_id)];
    const float half_days = config.farming.meadow_mow_days_per_ha * 10.0F * 0.5F *
                            config.farming.silage_mow_factor;
    std::cout << "  silage: after the first morning - near share " << near_meadow.silage_share
              << ", work opened " << near_meadow.work_days_remaining << " and held "
              << near_meadow.silage_work_held << " of " << half_days << " norm-days, hay put aside "
              << near_meadow.set_aside_grams / 1000 << " kg; far share " << far_meadow.silage_share
              << '\n';
    failures += Expect(near_meadow.silage_share == 0.5F &&
                           near_meadow.silage_trench.value == trench.value &&
                           near_meadow.set_aside_grams == hay_season / 2 &&
                           near_meadow.set_aside_resource.value == hay.value &&
                           near_meadow.reaped_grams == 0 && near_meadow.haul_days_written == 0.0F,
                       "silage: the nearest meadow's uncut half is turned for the trench, and the "
                       "hay on its heap is put aside with its carting's price");
    failures += Expect(core::HeapGrams(world, hay) == hay_season / 2,
                       "silage: the hay put aside still counts as lying on the fields");
    failures += Expect(far_meadow.silage_share == 0.0F &&
                           far_meadow.silage_trench.value == core::kInvalidEntityIdValue,
                       "silage: the far meadow is not turned while the near one's grass covers the "
                       "day - a meadow is taken from the hay the morning it is wanted");
    // PACED BY THE HEAPS: of the cut's work only a day's carting is opened.
    const float opened_share = static_cast<float>(day_green) / static_cast<float>(wanted_green);
    failures += Expect(std::abs(near_meadow.work_days_remaining - (half_days * opened_share)) <
                               0.01F &&
                           std::abs(near_meadow.silage_work_held -
                                    (half_days * (1.0F - opened_share))) < 0.01F,
                       "silage: the scythes are opened one day's carting, 25 t of the 30, and the "
                       "rest of the cut's work is held back");
    // THE RAIN: it stops the hay's scythe and not the silage's.
    failures += Expect(!core::RainStopsFieldWork(core::Precipitation::kRain,
                                                 core::WorkKind::kHarvest,
                                                 near_meadow) &&
                           core::RainStopsFieldWork(core::Precipitation::kRain,
                                                    core::WorkKind::kHarvest,
                                                    far_meadow),
                       "silage: rain stops the hay cut on one meadow and not the silage cut on "
                       "the other");
  }

  // THE DAY'S WORK MOWN: 25 t of green mass on the heap, booked as green
  // mass, and the hay's book untouched by it.
  {
    core::FieldRow& near_meadow = world.fields.rows[core::FindRow(world.fields, near_id)];
    near_meadow.work_days_remaining = 0.0F;
    core::LayMownShare(config, world, near_meadow);
    failures += Expect(Near(near_meadow.reaped_grams, day_green) &&
                           near_meadow.reaped_resource.value == green.value &&
                           Near(core::AmountOf(world.ledger.current.harvest, green), day_green) &&
                           core::AmountOf(world.ledger.current.harvest, hay) == hay_season / 2,
                       "silage: the opened work mown lays 25 t of green mass, booked as green "
                       "mass - the hay's book holds the 7.5 t mown before the turning alone");
    std::cout << "  silage: carting " << near_meadow.reaped_grams / 1000 << " kg of green mass over "
              << near_meadow.center.x << " m with no road is priced at "
              << near_meadow.haul_days_written << " norm-days\n";
    failures += Expect(near_meadow.haul_days_written > 0.0F,
                       "silage: the green heap's carting is priced - the trench is its door");
  }

  // A NIGHT NOT CARTED: half the green heap rots (its own two days, not the
  // field heap's whole heap), AND THE CARTING'S SEAM GOES DOWN WITH IT - the
  // carters' next day is priced for the heap that is left.
  {
    core::WorldState rot = world;
    const float priced = rot.fields.rows[core::FindRow(rot.fields, near_id)].haul_days_written;
    core::SpoilFieldHeaps(config, rot);
    const core::FieldRow& heap = rot.fields.rows[core::FindRow(rot.fields, near_id)];
    std::cout << "  silage: a night on the heap - " << heap.reaped_grams / 1000 << " kg of "
              << day_green / 1000 << " left, its carting priced " << heap.haul_days_written
              << " of " << priced << " norm-days\n";
    failures += Expect(Near(heap.reaped_grams, day_green / 2) &&
                           Near(core::AmountOf(rot.ledger.current.spoiled, green), day_green / 2),
                       "silage: a green heap loses half in a night it is not carted, booked as "
                       "spoiled");
    failures += Expect(std::abs(heap.haul_days_written - (priced * 0.5F)) < priced * 0.01F &&
                           heap.haul_days_remaining == heap.haul_days_written,
                       "the rot scales the carting's seam: half the heap gone, half the price");
  }

  // THE SECOND MORNING: 5 t still wanted, the heap is no backlog (one day's
  // carting lies on it) - the held work is opened, and mown.
  SetDay(world, kFirstOfSeptember + 1);
  core::RunSilageLaying(config, world);
  {
    core::FieldRow& near_meadow = world.fields.rows[core::FindRow(world.fields, near_id)];
    failures += Expect(near_meadow.silage_work_held == 0.0F && near_meadow.work_days_remaining > 0.0F,
                       "silage: the next morning opens what the order still lacks, 5 t");
    near_meadow.work_days_remaining = 0.0F;
    core::LayMownShare(config, world, near_meadow);
    failures += Expect(Near(near_meadow.reaped_grams, wanted_green),
                       "silage: mown through, the turned half has laid its 30 t");
  }

  // THE THIRD MORNING: covered by what lies on the heap - the cut is closed,
  // and the heap keeps its trench's name.
  SetDay(world, kFirstOfSeptember + 2);
  core::RunSilageLaying(config, world);
  {
    const core::FieldRow& near_meadow = world.fields.rows[core::FindRow(world.fields, near_id)];
    failures += Expect(near_meadow.silage_share == 0.0F &&
                           near_meadow.phase == core::FieldPhase::kGrowing &&
                           near_meadow.silage_trench.value == trench.value &&
                           near_meadow.last_mown_day == kFirstOfSeptember + 2 &&
                           Near(near_meadow.reaped_grams, wanted_green),
                       "silage: the order covered by the heap, the cut is closed and the heap is "
                       "still the trench's");
    // Nothing sealed yet: the heap is on the way, and the hay waits aside.
    core::SettleSilageLaying(config, world);
    failures += Expect(world.units.rows[0].silage_sealed_day == core::kTrenchNotSealed &&
                           Near(world.units.rows[0].silage_target, wanted_green) &&
                           near_meadow.set_aside_grams == hay_season / 2,
                       "silage: a trench whose heap is still at the meadow is not sealed, and the "
                       "hay stays aside under the green");
  }

  // CARTED: into the trench - not the manger - where it does not rot.
  {
    core::FieldRow& near_meadow = world.fields.rows[core::FindRow(world.fields, near_id)];
    near_meadow.haul_days_remaining = 0.0F;
    core::SettleHauling(config, world);
    core::SpoilStores(config, world);
    failures +=
        Expect(near_meadow.reaped_grams == 0 && Near(Held(world, trench, green), wanted_green) &&
                   Held(world, yard, green) == 0 &&
                   core::AmountOf(world.ledger.current.spoiled, green) == 0,
               "silage: carted through, the green mass is in the trench, none in the "
               "yard, and a night there rots none of it");
  }

  // THE EVENING: nothing left to mow or cart - sealed, the order cleared, the
  // meadow's name dropped, THE HAY BACK ON ITS HEAP with its carting priced,
  // the event said.
  world.step_events.clear();
  core::SettleSilageLaying(config, world);
  {
    const core::UnitRow& sealed = world.units.rows[0];
    const core::FieldRow& near_meadow = world.fields.rows[core::FindRow(world.fields, near_id)];
    bool said = false;
    for (const core::SimEvent& event : world.step_events) {
      said = said || (event.kind == core::EventKind::kSilageLaid &&
                      event.unit.value == trench.value && Near(event.amount, wanted_green));
    }
    failures += Expect(sealed.silage_sealed_day == kFirstOfSeptember + 2 &&
                           sealed.silage_target == 0 &&
                           near_meadow.silage_trench.value == core::kInvalidEntityIdValue && said,
                       "silage: the laying over, the trench is sealed that evening, its order "
                       "cleared, and kSilageLaid names the trench and its 30 t");
    failures += Expect(near_meadow.set_aside_grams == 0 &&
                           near_meadow.set_aside_resource.value == core::kInvalidDefIdValue &&
                           near_meadow.reaped_grams == hay_season / 2 &&
                           near_meadow.reaped_resource.value == hay.value &&
                           near_meadow.haul_days_written > 0.0F,
                       "silage: the green gone, the hay put aside is back on the heap, named hay, "
                       "its carting priced anew");
    failures += Expect(core::OrderLaySilage(config, world, LayOrder(trench, kTonne)) ==
                           core::OrderRefusal::kTrenchSealed,
                       "silage: a sealed trench refuses a second laying by its own name");
  }

  // RIPE: not a day early, and then all of it, at the ripening's share.
  SetDay(world, kFirstOfSeptember + 1 + config.farming.silage_ripen_days);
  core::RunSilageLaying(config, world);
  failures +=
      Expect(Held(world, trench, silage) == 0 && Near(Held(world, trench, green), wanted_green),
             "silage: a day before its term the trench still holds green mass");
  SetDay(world, kFirstOfSeptember + 2 + config.farming.silage_ripen_days);
  core::RunSilageLaying(config, world);
  std::cout << "  silage: ripe - " << Held(world, trench, silage) / 1000 << " kg of silage from "
            << wanted_green / 1000 << " kg of green mass\n";
  failures += Expect(Near(Held(world, trench, silage), wanted_silage) &&
                         Held(world, trench, green) == 0 &&
                         world.units.rows[0].silage_sealed_day == core::kTrenchNotSealed &&
                         Near(core::AmountOf(world.ledger.current.made, silage), wanted_silage) &&
                         Near(core::AmountOf(world.ledger.current.processed, green), wanted_green),
                     "silage: on its day the green mass is silage, 22.5 t of 30, booked as made "
                     "and processed, and the trench is open to a new laying");

  // A BACKLOG STOPS THE SCYTHES: with two days' carting already on the heaps
  // nothing is opened that morning.
  {
    core::WorldState behind;
    SetDay(behind, kFirstOfSeptember);
    const core::UnitId pit =
        AddUnit(behind, *tables, "silage_trench", {.x = 0.0F, .y = 0.0F}, resources);
    const core::FieldId waiting = AddMeadow(behind, config, 400.0F, 0.0F);
    AddMeadow(behind, config, 900.0F, 0.0F);
    failures += Expect(core::OrderLaySilage(config, behind, LayOrder(pit, 150 * kTonne)) ==
                           core::OrderRefusal::kNone,
                       "silage: an order of 150 t is taken");
    core::FieldRow& heap = behind.fields.rows[core::FindRow(behind.fields, waiting)];
    heap.phase = core::FieldPhase::kGrowing;  // mown out, its green not carted
    heap.silage_trench = pit;
    heap.reaped_resource = green;
    heap.reaped_grams = 2 * day_green;
    core::RunSilageLaying(config, behind);
    failures += Expect(behind.fields.rows[1].silage_share == 0.0F,
                       "silage: with two days' carting on the heaps no grass is turned and no "
                       "work opened - «косят столько, сколько увозят»");
    heap.reaped_grams = day_green + (day_green / 2);
    core::RunSilageLaying(config, behind);
    const core::FieldRow& turned = behind.fields.rows[1];
    const float full_days = config.farming.meadow_mow_days_per_ha * 10.0F *
                            config.farming.silage_mow_factor;
    const float half_day_share =
        static_cast<float>(day_green / 2) / static_cast<float>(green_season);
    failures += Expect(turned.silage_share == 1.0F &&
                           std::abs(turned.work_days_remaining - (full_days * half_day_share)) < 0.01F,
                       "silage: with a day and a half on the heaps, half a day's mowing is opened");
  }

  // THE WINDOW'S LAST EVENING closes a cut still turned, and seals what was
  // laid short of the order.
  {
    core::WorldState late;
    SetDay(late, kFirstOfSeptember + 2);
    const core::UnitId pit =
        AddUnit(late, *tables, "silage_trench", {.x = 0.0F, .y = 0.0F}, resources);
    const core::FieldId grass = AddMeadow(late, config, 800.0F, 0.0F);
    failures += Expect(core::OrderLaySilage(config, late, LayOrder(pit, 100 * kTonne)) ==
                           core::OrderRefusal::kNone,
                       "silage: an order larger than the grass is taken");
    core::RunSilageLaying(config, late);
    core::SettleSilageLaying(config, late);
    failures += Expect(late.fields.rows[0].silage_share == 1.0F,
                       "silage: on an evening inside the window the turned cut stays open");
    SetDay(late, kFirstOfSeptember + 3);  // September's last day
    core::SettleSilageLaying(config, late);
    failures += Expect(late.fields.rows[core::FindRow(late.fields, grass)].silage_share == 0.0F &&
                           late.fields.rows[0].silage_work_held == 0.0F &&
                           late.fields.rows[0].phase == core::FieldPhase::kGrowing &&
                           late.units.rows[0].silage_target == 0 &&
                           late.units.rows[0].silage_sealed_day == core::kTrenchNotSealed,
                       "silage: on the window's last evening the cut is closed and the order "
                       "cleared; an empty trench is not sealed");
    // PAST THE WINDOW the order is refused by the window's own name.
    SetDay(late, kFirstOfSeptember + 4);
    failures += Expect(core::OrderLaySilage(config, late, LayOrder(pit, kTonne)) ==
                           core::OrderRefusal::kLayingWindowClosed,
                       "silage: in October the order is refused - the window is closed");
    // INSIDE THE WINDOW WITH NO GRASS STANDING it is refused by that name,
    // and BEFORE the window the same world takes it: nobody can say in June
    // what the haymaking will leave.
    SetDay(late, kFirstOfSeptember);
    failures +=
        Expect(core::OrderLaySilage(config, late, LayOrder(pit, kTonne)) ==
                   core::OrderRefusal::kNoUncutMeadow,
               "silage: inside the window with every meadow closed, there is nothing to lay");
    SetDay(late, 21);  // June
    failures += Expect(core::OrderLaySilage(config, late, LayOrder(pit, kTonne)) ==
                               core::OrderRefusal::kNone &&
                           late.units.rows[0].silage_target > 0,
                       "silage: before the window the same order stands for the autumn");
    core::RunSilageLaying(config, late);
    core::SettleSilageLaying(config, late);
    failures += Expect(late.units.rows[0].silage_target > 0,
                       "silage: and an evening in June does not clear it");
    // WITHDRAWN.
    failures += Expect(core::OrderLaySilage(config, late, LayOrder(pit, 0)) ==
                               core::OrderRefusal::kNone &&
                           late.units.rows[0].silage_target == 0,
                       "silage: an order of nought withdraws it");
  }
  return failures;
}
