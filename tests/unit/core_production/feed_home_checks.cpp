// The checks of a farm's own rick (feed_home_checks.h).

#include "feed_home_checks.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/day_off.h"
#include "core_common/event_state.h"
#include "core_common/herd_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "feed_home.h"
#include "herd_system.h"
#include "production_config.h"
#include "rick_supply.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

constexpr core::Grams kTonne = 1'000'000;

/// A standing unit of the shipped table's type `key`, with an empty stock.
std::uint32_t AddUnit(core::WorldState& world,
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
  core::AppendRow(world.units, unit);
  return static_cast<std::uint32_t>(world.units.rows.size() - 1U);
}

/// Ten grown head of the shipped kind `key`, the kolkhoz's, fed at `unit_row`.
void AddHerd(core::WorldState& world,
             const core::ITableSet& tables,
             const char* key,
             std::uint32_t unit_row) {
  core::HerdRow herd;
  herd.kind = core::LivestockKindId{
      static_cast<std::uint16_t>(tables.FindTable("livestock")->FindRowByKey(key))};
  herd.adult_count = 10;
  herd.unit = world.units.row_ids[unit_row];
  core::AppendRow(world.herds, herd);
}

core::Grams& HayOf(core::WorldState& world, std::uint32_t unit_row, core::ResourceId hay) {
  return world.units.rows[unit_row].stock[hay.value];
}

}  // namespace

int CheckEachFarmEatsFromItsOwnRick() {
  int failures = 0;
  std::string error;
  const auto tables = core::LoadTableSet(KOLKHOZ_TABLES_DIR, &error);
  core::ProductionConfig config;
  if (Expect(tables != nullptr && core::ParseProductionConfig(*tables, config, error),
             "feed home: the shipped tables parse") != 0) {
    std::cout << error << '\n';
    return 1;
  }
  const core::ResourceId hay = config.hay_resource;
  const std::size_t resources = config.feed_values.size();
  // THE BOUND FEEDS ARE THE RICK's, by the shipped homes: hay and straw yes,
  // oats - a granary's - no.
  failures +=
      Expect(core::IsBoundFeed(config, hay) && core::IsBoundFeed(config, config.straw_resource),
             "feed home: hay and straw are a rick's feeds by the shipped homes");
  const core::ITable* resource_rows = tables->FindTable("resources");
  const core::ResourceId oats{static_cast<std::uint16_t>(resource_rows->FindRowByKey("oats"))};
  failures += Expect(!core::IsBoundFeed(config, oats),
                     "feed home: oats, a granary's, are not bound to a rick");

  // TWO FARMS TWO KILOMETRES APART, A RICK BY EACH, January (the stall).
  core::WorldState world;
  world.calendar.tick = 0;
  core::RefreshCalendarCaches(world.calendar);
  const std::uint32_t cattle =
      AddUnit(world, *tables, "cattle_yard", {.x = 0.0F, .y = 0.0F}, resources);
  const std::uint32_t cattle_rick =
      AddUnit(world, *tables, "hay_stack", {.x = 50.0F, .y = 0.0F}, resources);
  const std::uint32_t team =
      AddUnit(world, *tables, "horse_yard", {.x = 2000.0F, .y = 0.0F}, resources);
  const std::uint32_t team_rick =
      AddUnit(world, *tables, "hay_stack", {.x = 2050.0F, .y = 0.0F}, resources);
  AddHerd(world, *tables, "cow", cattle);
  AddHerd(world, *tables, "horse", team);
  failures += Expect(core::FeedHomeOf(config, world, cattle).rick_row == cattle_rick &&
                         core::FeedHomeOf(config, world, team).rick_row == team_rick,
                     "feed home: each farm's rick is the one by its gate, not the first by row");

  // THE TAKE, BY SOURCE. The team's rick full: the horses' hay comes from it
  // and nothing from the cattle yard's, two kilometres off.
  HayOf(world, cattle_rick, hay) = 50 * kTonne;
  HayOf(world, team_rick, hay) = 50 * kTonne;
  std::uint32_t far_row = core::kNoRow;
  core::BoundTake take =
      core::TakeBoundFeedAt(config, world, team, team_rick, hay, kTonne, &far_row);
  failures += Expect(take.from_its_rick == kTonne && take.from_far == 0 &&
                         HayOf(world, cattle_rick, hay) == 50 * kTonne,
                     "feed home: with its own rick full a farm takes nothing from the far one");
  // THE OTHER SIDE: its rick out, the far rick gives by the old chain - and
  // the take says so, naming the unit.
  HayOf(world, team_rick, hay) = 0;
  take = core::TakeBoundFeedAt(config, world, team, team_rick, hay, kTonne, &far_row);
  failures += Expect(take.from_its_rick == 0 && take.from_far == kTonne && far_row == cattle_rick,
                     "feed home: with its own rick out a farm eats from the far rick, counted and "
                     "named");

  // WHICH RICK IS SHORT: the team's (empty) on the first day, the cattle
  // yard's (49 t for ten cows) not within the week - the village's sum would
  // have answered «not short» for both.
  const std::vector<core::ShortRick> short_ricks = core::ShortRicksWithin(config, world, 7);
  std::cout << "  feed home: short ricks within 7 days " << short_ricks.size();
  for (const core::ShortRick& rick : short_ricks) {
    std::cout << " [row " << rick.rick_row << ", " << rick.days_left << " days left]";
  }
  std::cout << '\n';
  failures += Expect(short_ricks.size() == 1 && short_ricks[0].rick_row == team_rick &&
                         short_ricks[0].days_left == 0,
                     "feed home: the empty rick alone is short, on the first day, with the other "
                     "farm's rick full");
  // SHORT OF HAY WITH STRAW LYING IN IT (arm A's trace): ten tonnes of straw
  // in the team's rick - a fifth of a horse's day at most - do not make a
  // rick with no hay «not out».
  core::WorldState with_straw = world;
  with_straw.units.rows[team_rick].stock[config.straw_resource.value] = 10 * kTonne;
  const std::vector<core::ShortRick> short_with_straw =
      core::ShortRicksWithin(config, with_straw, 7);
  failures += Expect(short_with_straw.size() == 1 && short_with_straw[0].rick_row == team_rick &&
                         short_with_straw[0].days_left == 0,
                     "feed home: a rick out of hay is short though straw lies in it");

  // WHERE THE DOOR SENDS A LOAD: from a meadow beside the cattle yard's rick
  // the hay goes to THE TEAM's, two kilometres on - the larger gap, not the
  // nearer rick.
  core::RickChoice choice;
  const core::Vec2 meadow{.x = -100.0F, .y = 0.0F};
  failures += Expect(core::RickForLoadAt(config, world, meadow, hay, &choice) == team_rick &&
                         choice.nearest_row == cattle_rick && choice.road_km > choice.nearest_km,
                     "feed home: a load goes to the rick with the larger gap, past the nearer one");
  // THE OTHER SIDE: both ricks carrying their herds to the horizon, the
  // nearest by road takes it.
  HayOf(world, team_rick, hay) = 500 * kTonne;
  HayOf(world, cattle_rick, hay) = 500 * kTonne;
  failures += Expect(core::RickForLoadAt(config, world, meadow, hay, &choice) == cattle_rick,
                     "feed home: with no rick short of the horizon a load goes to the nearest");
  // THE OVERFLOW (arm A3): the nearer rick holding its herds' hay to the
  // lamp's horizon and the farther one twenty tonnes - a dozen days of the
  // horses', not short by the urgent rule and short of the horizon - the load
  // passes the nearer rick. THE OTHER SIDE: the nearer rick short of the
  // horizon too, it takes the load itself.
  HayOf(world, team_rick, hay) = 20 * kTonne;
  failures += Expect(core::RickForLoadAt(config, world, meadow, hay, &choice) == team_rick &&
                         choice.nearest_row == cattle_rick,
                     "feed home: the nearer rick full to the horizon, the overflow goes to the "
                     "rick that is not");
  HayOf(world, cattle_rick, hay) = 20 * kTonne;
  failures += Expect(core::RickForLoadAt(config, world, meadow, hay, &choice) == cattle_rick,
                     "feed home: the nearer rick short of the horizon itself, the load stays "
                     "with it");

  // THE DAY's ONE WORD: a herd day with the team's rick out says, once for
  // that herd, that it ate from elsewhere - and nothing for the cows.
  HayOf(world, team_rick, hay) = 0;
  HayOf(world, cattle_rick, hay) = 50 * kTonne;
  world.step_events.clear();
  core::RunHerdDay(config, world);
  std::uint32_t said = 0;
  bool names_the_team = false;
  for (const core::SimEvent& event : world.step_events) {
    if (event.kind == core::EventKind::kHerdAteFromFarRick) {
      ++said;
      names_the_team = event.herd.value == world.herds.row_ids[1].value &&
                       event.unit.value == world.units.row_ids[cattle_rick].value;
    }
  }
  std::cout << "  feed home: a herd day with the team's rick out — far-rick words " << said
            << ", the cattle yard's rick now " << HayOf(world, cattle_rick, hay) / 1000 << " kg\n";
  failures += Expect(said == 1 && names_the_team,
                     "feed home: the far-rick day is said once, for the horses, naming the rick "
                     "that gave");

  // THE PACKED AMOUNT AT EACH CEILING (PackFarRickAmount): a way of 900 km
  // reads 655.35 km and leaves the kilograms whole; a weight far above the
  // kilograms' ceiling reads the ceiling and leaves the way whole.
  const std::int64_t long_way = core::PackFarRickAmount(900.0F, 70'000 * 1000LL);
  const std::int64_t heavy = core::PackFarRickAmount(2.5F, std::int64_t{1} << 62);
  failures += Expect((long_way & core::kFarRickWayMax) == core::kFarRickWayMax &&
                         (long_way >> core::kFarRickKilogramsShift) == 70'000,
                     "feed home: a way above 655 km is clamped and the kilograms stay whole");
  failures += Expect((heavy & core::kFarRickWayMax) == 250 &&
                         (heavy >> core::kFarRickKilogramsShift) == core::kFarRickKilogramsMax,
                     "feed home: a weight above its ceiling is clamped and the way stays whole");

  // NOT FOR A REMAINDER (kFarRickDayShare): the team's rick a kilogram short
  // of the horses' day - the kilogram comes from the far rick and nothing is
  // said; a fifth of the day short, it is.
  core::WorldState full = world;
  HayOf(full, team_rick, hay) = 50 * kTonne;
  core::RunHerdDay(config, full);
  const core::Grams ration = 50 * kTonne - HayOf(full, team_rick, hay);
  const auto words_with_the_rick_short_by = [&](core::Grams lacking) {
    core::WorldState day = world;
    HayOf(day, team_rick, hay) = ration - lacking;
    day.step_events.clear();
    core::RunHerdDay(config, day);
    std::uint32_t words = 0;
    for (const core::SimEvent& event : day.step_events) {
      words += event.kind == core::EventKind::kHerdAteFromFarRick ? 1U : 0U;
    }
    return words;
  };
  const std::uint32_t for_a_kilogram = words_with_the_rick_short_by(1000);
  const std::uint32_t for_a_fifth = words_with_the_rick_short_by(ration / 5);
  std::cout << "  feed home: the horses' day of hay " << ration / 1000
            << " kg; its rick a kilogram short — far-rick words " << for_a_kilogram
            << "; a fifth short — " << for_a_fifth << '\n';
  failures += Expect(ration > 20'000 && for_a_kilogram == 0,
                     "feed home: a kilogram's remainder from the far rick is not a far-rick day");
  failures +=
      Expect(for_a_fifth == 1, "feed home: a fifth of the day from the far rick is a far-rick day");
  return failures;
}

int CheckARickIsSuppliedFromAnother() {
  int failures = 0;
  std::string error;
  const auto tables = core::LoadTableSet(KOLKHOZ_TABLES_DIR, &error);
  core::ProductionConfig config;
  if (Expect(tables != nullptr && core::ParseProductionConfig(*tables, config, error),
             "rick supply: the shipped tables parse") != 0) {
    std::cout << error << '\n';
    return 1;
  }
  const core::ResourceId hay = config.hay_resource;
  const std::size_t resources = config.feed_values.size();
  // The two farms of the check above, January: the team's rick empty, the
  // cattle yard's holding 50 t for ten cows.
  core::WorldState world;
  world.calendar.tick = 0;
  core::RefreshCalendarCaches(world.calendar);
  const std::uint32_t cattle =
      AddUnit(world, *tables, "cattle_yard", {.x = 0.0F, .y = 0.0F}, resources);
  const std::uint32_t cattle_rick =
      AddUnit(world, *tables, "hay_stack", {.x = 50.0F, .y = 0.0F}, resources);
  const std::uint32_t team =
      AddUnit(world, *tables, "horse_yard", {.x = 2000.0F, .y = 0.0F}, resources);
  const std::uint32_t team_rick =
      AddUnit(world, *tables, "hay_stack", {.x = 2050.0F, .y = 0.0F}, resources);
  AddHerd(world, *tables, "cow", cattle);
  AddHerd(world, *tables, "horse", team);
  HayOf(world, cattle_rick, hay) = 50 * kTonne;

  // THE SWITCH OFF (farming.csv rick_supply 0, as shipped): no rick has a supply.
  core::WorldState off = world;
  config.farming.rick_supply = 0.0F;
  core::PriceRickSupply(config, off);
  failures +=
      Expect(off.units.rows[team_rick].rick_supply.from.value == core::kInvalidEntityIdValue,
             "rick supply: with the switch off no rick is supplied");

  // THE SWITCH ON: the empty rick is supplied from the full one, against the
  // urgent horizon plus the contract's seven days.
  config.farming.rick_supply = 1.0F;
  core::PriceRickSupply(config, world);
  const core::RickSupplySeam& seam = world.units.rows[team_rick].rick_supply;
  const std::uint32_t threshold =
      core::UrgentHayHorizonDays(world, static_cast<std::uint32_t>(config.hay_cart_urgent_days)) +
      static_cast<std::uint32_t>(config.farming.rick_threshold_add_days);
  std::cout << "  rick supply: the team's rick priced " << seam.grams / 1000 << " kg from row "
            << (seam.from.value == world.units.row_ids[cattle_rick].value ? cattle_rick : 99U)
            << ", " << seam.days_remaining << " cart-days, threshold " << +seam.threshold_days
            << " days\n";
  failures +=
      Expect(seam.from.value == world.units.row_ids[cattle_rick].value &&
                 seam.resource.value == hay.value && seam.grams > 0 && seam.grams < 50 * kTonne &&
                 seam.days_remaining > 0.0F && seam.days_written == seam.days_remaining &&
                 seam.threshold_days == threshold,
             "rick supply: the empty rick is priced hay from the full one, by its threshold");
  failures +=
      Expect(world.units.rows[cattle_rick].rick_supply.from.value == core::kInvalidEntityIdValue,
             "rick supply: the full rick is not supplied");
  failures += Expect(core::RickSupplyStands(world.units.rows[team_rick]),
                     "rick supply: the team's rick's supply stands for the labour");

  // THE SETTLE: half the seam drained moves half the hay, rick to rick, and
  // no gram is lost or made on the way.
  core::RickSupplySeam& open = world.units.rows[team_rick].rick_supply;
  const core::Grams priced = open.grams;
  open.days_remaining = open.days_written / 2.0F;
  core::SettleRickSupply(config, world);
  const core::Grams moved = HayOf(world, team_rick, hay);
  std::cout << "  rick supply: half the seam drained moved " << moved / 1000 << " kg of "
            << priced / 1000 << '\n';
  failures += Expect(moved > (priced * 49) / 100 && moved < (priced * 51) / 100 &&
                         HayOf(world, cattle_rick, hay) + moved == 50 * kTonne &&
                         world.units.rows[team_rick].rick_supply.grams == priced - moved,
                     "rick supply: half the seam drained moves half the hay, none lost");

  // THE SENDER KEEPS ITS OWN: a cattle rick with a day's hay spares nothing,
  // and nobody is supplied.
  core::WorldState poor = world;
  HayOf(poor, cattle_rick, hay) = kTonne / 4;
  HayOf(poor, team_rick, hay) = 0;
  core::PriceRickSupply(config, poor);
  failures +=
      Expect(poor.units.rows[team_rick].rick_supply.from.value == core::kInvalidEntityIdValue,
             "rick supply: a sender short itself spares nothing");
  return failures;
}
