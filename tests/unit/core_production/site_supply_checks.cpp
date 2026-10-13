// The checks of the supply seam of a works (site_supply_checks.h).

#include "site_supply_checks.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>

#include "core_common/calendar.h"
#include "core_common/day_off.h"
#include "core_common/quantities.h"
#include "core_common/site_supply_floor.h"
#include "core_common/state_table_ops.h"
#include "core_common/supply_estimate.h"
#include "core_common/unit_state.h"
#include "core_common/work_seam.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "field_haul.h"
#include "production_config.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

core::Grams At(const core::ResourceAmounts& amounts, core::ResourceId resource) {
  return resource.value < amounts.size() ? amounts[resource.value] : 0;
}

/// A works a kilometre from a store at the origin, delivering, its one leg
/// holding `grams` (dense by resource) - in a village with no horse.
core::WorldState HandSupplyWorld(const core::ResourceAmounts& grams) {
  core::WorldState world;
  world.calendar.tick = 25U * core::kTicksPerDay + 10U;  // June, ten o'clock
  core::RefreshCalendarCaches(world.calendar);
  core::UnitRow site;
  site.position = core::Vec2{.x = 1000.0F, .y = 0.0F};
  site.construction.phase = core::ConstructionPhase::kDelivering;
  site.construction.awaiting = grams;
  core::SupplyLeg leg;
  leg.grams = grams;
  for (const core::Grams of_one : grams) {
    leg.unpriced += of_one;
  }
  site.construction.legs.push_back(leg);
  core::AppendRow(world.units, site);
  return world;
}

/// The seam a settlement writes for a leg of `grams` on foot.
float HandSeamOf(const core::ProductionConfig& config, const core::ResourceAmounts& grams) {
  core::WorldState world = HandSupplyWorld(grams);
  core::SettleSiteSupply(config, world, false);
  return world.units.rows[0].construction.supply_days_remaining;
}

}  // namespace

// ON FOOT ONE RESOURCE A TRIP, EACH BY ITS FORM (#671, the second delivery; transport design §2,
// «Смешанное снабжение стройки»). By hand, over one way: a tonne of straw in 10-kg bundles is
// twice the days of a tonne of clay in 20-kg sacks, so a leg of both is three clay-tonnes' days;
// a third of that seam drained is half the straw and NO clay (until 0.37.240 it was a third of
// the sum, 667 kg, moved in id order); two thirds is all the straw and still no clay. A log goes
// by two - 100 kg a head, the loaded half at half pace: (20 / 100) x (3 / 2) = 0.3 of the days
// the same grams take as sacks.
int CheckTheHandSupplyGoesOneResourceATrip() {
  int failures = 0;
  std::string error;
  const auto tables = core::LoadTableSet(KOLKHOZ_TABLES_DIR, &error);
  core::ProductionConfig config;
  if (Expect(tables != nullptr && core::ParseProductionConfig(*tables, config, error),
             "hand supply: the shipped tables parse") != 0) {
    std::cout << error << '\n';
    return 1;
  }
  const core::ITable* const resources = tables->FindTable("resources");
  const auto id = [resources](const char* key) {
    return core::ResourceId{static_cast<std::uint16_t>(resources->FindRowByKey(key))};
  };
  const core::ResourceId straw = id("straw");
  const core::ResourceId clay = id("clay");
  const core::ResourceId logs = config.timber.log_resource;
  if (Expect(straw.value < clay.value && logs.value != core::kInvalidDefIdValue,
             "hand supply: straw comes before clay by id, and the roster names the log") != 0) {
    return failures + 1;
  }
  constexpr core::Grams kTonne = 1'000'000;
  const auto leg_of = [&](core::Grams straw_grams, core::Grams clay_grams, core::Grams log_grams) {
    core::ResourceAmounts grams(
        static_cast<std::size_t>(std::max({straw.value, clay.value, logs.value})) + 1U, 0);
    grams[straw.value] = straw_grams;
    grams[clay.value] = clay_grams;
    grams[logs.value] = log_grams;
    return grams;
  };
  const float clay_days = HandSeamOf(config, leg_of(0, kTonne, 0));
  const float straw_days = HandSeamOf(config, leg_of(kTonne, 0, 0));
  const float both_days = HandSeamOf(config, leg_of(kTonne, kTonne, 0));
  std::cout << "  hand supply, a tonne over a kilometre on foot: clay " << clay_days
            << " days, straw " << straw_days << ", both " << both_days << '\n';
  failures +=
      Expect(clay_days > 0.0F && straw_days > clay_days * 1.999F && straw_days < clay_days * 2.001F,
             "hand supply: a tonne of straw in bundles is twice the days of a tonne of "
             "clay in sacks");
  failures += Expect(both_days > clay_days * 2.999F && both_days < clay_days * 3.001F,
                     "hand supply: a leg of both is priced by each resource's own trip, summed");
  // The estimate before the start is the same arithmetic when the plan names
  // the resources, and the sack's when it names grams alone.
  {
    const core::WorldState world = HandSupplyWorld(leg_of(kTonne, kTonne, 0));
    core::SupplyEstimate plan;
    plan.grams = 2 * kTonne;
    plan.sender = core::Vec2{};
    const float by_sack =
        core::PriceSiteSupply(config, world, plan, world.units.rows[0].position).cart_days;
    plan.by_resource = leg_of(kTonne, kTonne, 0);
    const float by_form =
        core::PriceSiteSupply(config, world, plan, world.units.rows[0].position).cart_days;
    failures += Expect(by_form == both_days,
                       "hand supply: the price asked before the start is the seam's own when the "
                       "plan names the resources");
    failures += Expect(by_sack > clay_days * 1.999F && by_sack < clay_days * 2.001F,
                       "hand supply: a plan of grams alone is priced by the sack");
  }
  // ONE RESOURCE A TRIP: the seam's first third is half the straw, alone.
  {
    core::WorldState world = HandSupplyWorld(leg_of(kTonne, kTonne, 0));
    core::SettleSiteSupply(config, world, false);
    core::UnitRow& works = world.units.rows[0];
    works.construction.supply_days_remaining = both_days * 2.0F / 3.0F;
    core::SettleSiteSupply(config, world, false);
    const core::Grams straw_in = At(works.stock, straw);
    std::cout << "  hand supply, a third of the seam drained: straw " << straw_in / 1000
              << " kg, clay " << At(works.stock, clay) / 1000 << " kg\n";
    failures += Expect(straw_in > (kTonne / 2) - 2000 && straw_in < (kTonne / 2) + 2000 &&
                           At(works.stock, clay) == 0,
                       "hand supply: a third of the seam is half the straw and no clay - the "
                       "first resource before the next is touched");
    works.construction.supply_days_remaining = both_days / 3.0F;
    core::SettleSiteSupply(config, world, false);
    failures +=
        Expect(At(works.stock, straw) > kTonne - 4000 && At(works.stock, clay) < 4000 &&
                   At(works.stock, straw) + At(works.construction.awaiting, straw) == kTonne &&
                   At(works.stock, clay) + At(works.construction.awaiting, clay) == kTonne,
               "hand supply: two thirds is the straw whole and still no clay, to the gram "
               "between the site and what awaits");
    works.construction.supply_days_remaining = 0.0F;
    core::SettleSiteSupply(config, world, false);
    failures += Expect(At(works.stock, straw) == kTonne && At(works.stock, clay) == kTonne &&
                           works.construction.legs.empty(),
                       "hand supply: the seam drained to nought brings every gram of both");
  }
  // A LOG GOES BY TWO: its days are a pair's heads'.
  {
    const float log_days = HandSeamOf(config, leg_of(0, 0, 2 * kTonne));
    const float as_sacks = 2.0F * clay_days;
    std::cout << "  hand supply, two tonnes of logs by pairs: " << log_days << " days against "
              << as_sacks << " as sacks\n";
    failures += Expect(log_days > as_sacks * 0.299F && log_days < as_sacks * 0.301F,
                       "hand supply: logs are priced by the pair - 100 kg a head, the loaded "
                       "half at half pace - three tenths of the sack's days");
  }
  // AND THE YEAR'S BOOK COUNTS THE PAIRS' TRIPS: a tonne of logs on foot is
  // five logs, five pair trips.
  {
    core::WorldState world = HandSupplyWorld(leg_of(0, 0, kTonne));
    core::SettleSiteSupply(config, world, false);
    world.units.rows[0].construction.supply_days_remaining = 0.0F;
    core::SettleSiteSupply(config, world, false);
    const float trips = world.ledger.current.log_pair_trips;
    failures += Expect(trips > 4.99F && trips < 5.01F,
                       "hand supply: a tonne of logs carried on foot is booked as five pair "
                       "trips");
  }
  return failures;
}

// THE FLOOR'S MEMORY (#734; ConstructionState::supply_idle_days). By hand: a works with a tonne
// of clay awaiting, in a village with no horse and nobody carrying. Two working evenings with
// nothing landed are two idle days; an evening on a day off adds none; a paused works adds none
// and keeps its two; an hour in which the seam was drained lands grams, and that day's evening
// sets the count to nought and clears the day's switch; a works awaiting nothing reads nought.
int CheckTheSupplyCountsItsIdleDays() {
  int failures = 0;
  std::string error;
  const auto tables = core::LoadTableSet(KOLKHOZ_TABLES_DIR, &error);
  core::ProductionConfig config;
  if (Expect(tables != nullptr && core::ParseProductionConfig(*tables, config, error),
             "idle days: the shipped tables parse") != 0) {
    std::cout << error << '\n';
    return 1;
  }
  const core::ITable* const resources = tables->FindTable("resources");
  const core::ResourceId clay{static_cast<std::uint16_t>(resources->FindRowByKey("clay"))};
  core::ResourceAmounts grams(static_cast<std::size_t>(clay.value) + 1U, 0);
  grams[clay.value] = 1'000'000;
  core::WorldState world = HandSupplyWorld(grams);
  world.calendar.day_zero_weekday = core::Weekday::kMonday;
  world.chairman.harvest_without_days_off = 0;
  const auto set_day = [&world](std::uint32_t day) {
    world.calendar.tick = day * core::kTicksPerDay + 10U;
    core::RefreshCalendarCaches(world.calendar);
  };
  // A working day and a day off of one week in June.
  std::uint32_t working = 24;
  std::uint32_t off = 24;
  for (std::uint32_t day = 24; day < 31; ++day) {
    set_day(day);
    if (core::IsDayOffIn(world, day)) {
      off = day;
    } else {
      working = day;
    }
  }
  if (Expect(off != working && core::IsDayOffIn(world, off) && !core::IsDayOffIn(world, working),
             "idle days: the week has a working day and a day off") != 0) {
    return failures + 1;
  }
  core::ConstructionState& works = world.units.rows[0].construction;
  set_day(working);
  core::SettleSiteSupply(config, world, false);  // the seam is written, nothing moves
  failures += Expect(works.supply_idle_days == 0, "idle days: an hour of the day counts nothing");
  core::SettleSiteSupply(config, world, true);
  core::SettleSiteSupply(config, world, true);
  failures += Expect(works.supply_idle_days == 2,
                     "idle days: two working evenings with nothing landed are two");
  set_day(off);
  core::SettleSiteSupply(config, world, true);
  failures += Expect(works.supply_idle_days == 2, "idle days: a day off neither counts nor resets");
  set_day(working);
  world.units.rows[0].paused = 1;
  core::SettleSiteSupply(config, world, true);
  failures += Expect(works.supply_idle_days == 2, "idle days: a paused works keeps its count");
  world.units.rows[0].paused = 0;
  core::SettleSiteSupply(config, world, false);  // the seam written again after the pause
  works.supply_days_remaining = works.supply_days_written * 0.5F;  // half drained this hour
  core::SettleSiteSupply(config, world, false);
  failures += Expect(At(world.units.rows[0].stock, clay) > 0 && works.supply_moved_today == 1 &&
                         works.supply_idle_days == 2,
                     "idle days: the hour grams land sets the day's switch and leaves the count "
                     "to the evening");
  core::SettleSiteSupply(config, world, true);
  failures += Expect(works.supply_idle_days == 0 && works.supply_moved_today == 0,
                     "idle days: the evening of a day something landed starts from nought");
  core::SettleSiteSupply(config, world, true);
  failures += Expect(works.supply_idle_days == 1, "idle days: and the next idle evening is one");
  // ON THE FLOOR TILL THE RECIPE IS IN: at the floor's four days a delivery no
  // longer starts the count again - the floor's own cart must not take the
  // works off the floor it stands on.
  core::SettleSiteSupply(config, world, true);
  core::SettleSiteSupply(config, world, true);
  core::SettleSiteSupply(config, world, true);
  failures += Expect(works.supply_idle_days == 4 && core::OnSupplyFloor(config.supply_floor, works),
                     "idle days: four idle evenings are the floor");
  works.supply_days_remaining = works.supply_days_written * 0.5F;
  core::SettleSiteSupply(config, world, false);
  core::SettleSiteSupply(config, world, true);
  failures += Expect(works.supply_idle_days == 4 && works.supply_moved_today == 0 &&
                         core::OnSupplyFloor(config.supply_floor, works),
                     "idle days: a delivery to a works on the floor leaves it on the floor");
  core::SettleSiteSupply(config, world, true);
  failures += Expect(works.supply_idle_days == 5,
                     "idle days: and an idle evening on the floor still counts");
  works.supply_days_remaining = 0.0F;  // the rest carried in
  core::SettleSiteSupply(config, world, false);
  core::SettleSiteSupply(config, world, true);
  failures += Expect(works.legs.empty() && works.supply_idle_days == 0,
                     "idle days: a works awaiting nothing reads nought");
  return failures;
}

int CheckTheSupplyOfAWorksIsCarted() {
  int failures = 0;
  std::string error;
  const auto tables = core::LoadTableSet(KOLKHOZ_TABLES_DIR, &error);
  core::ProductionConfig config;
  if (Expect(tables != nullptr && core::ParseProductionConfig(*tables, config, error),
             "site supply: the shipped tables parse") != 0) {
    std::cout << error << '\n';
    return 1;
  }
  const core::ResourceId logs = config.timber.log_resource;
  if (Expect(logs.value != core::kInvalidDefIdValue, "site supply: the roster names the log") !=
      0) {
    return 1;
  }
  constexpr core::Grams kTenTonnes = 10'000'000;
  core::WorldState world;
  world.calendar.tick = 25U * core::kTicksPerDay + 10U;  // June, ten o'clock
  core::RefreshCalendarCaches(world.calendar);
  core::UnitRow site;
  site.level = 0;
  site.position = core::Vec2{.x = 1000.0F, .y = 0.0F};
  site.construction.phase = core::ConstructionPhase::kDelivering;
  site.construction.supply_from = core::Vec2{.x = 0.0F, .y = 0.0F};
  site.construction.awaiting.assign(static_cast<std::size_t>(logs.value) + 1U, 0);
  site.construction.awaiting[logs.value] = kTenTonnes;
  // One leg, a store at the origin, as the fixing writes it (site_supply.cpp).
  core::SupplyLeg from_store;
  from_store.grams = site.construction.awaiting;
  from_store.unpriced = kTenTonnes;
  site.construction.legs.push_back(from_store);
  const core::UnitId id = core::AppendRow(world.units, site);
  core::UnitRow& works = world.units.rows[0];

  // A LOAD WITH NO SEAM GETS ONE IN ITS HOUR: the site was started this
  // morning, and the day's top-up may send somebody.
  core::SettleSiteSupply(config, world, false);
  const float whole = works.construction.supply_days_remaining;
  failures += Expect(
      whole > 0.0F && works.construction.supply_days_written == whole && At(works.stock, logs) == 0,
      "site supply: a load with no seam is given one that hour, and nothing "
      "lands before anybody carts");
  failures += Expect(works.construction.supply_days_first == whole &&
                         works.construction.legs.front().unpriced == 0,
                     "site supply: the works' load as first priced is its one leg's seam, "
                     "priced once");

  // THE PRICE ASKED BEFORE THE START IS THE SEAM'S OWN: the same grams over
  // the same way cost the same days, and with no horse in the farm the
  // estimate says it is a carrier's.
  core::SupplyEstimate plan;
  plan.grams = kTenTonnes;
  // By resource, as construction's plan names them (#671): on foot the price
  // follows each resource's own trip - these logs go by two.
  plan.by_resource = works.construction.awaiting;
  plan.sender = works.construction.supply_from;
  const core::SupplyEstimate priced = core::PriceSiteSupply(config, world, plan, works.position);
  failures += Expect(priced.cart_days == whole && priced.on_foot && priced.grams == kTenTonnes,
                     "site supply: the estimate before the start is the seam's own days, and "
                     "says «on foot» while the farm has no horse");
  failures += Expect(
      core::PriceSiteSupply(config, world, core::SupplyEstimate{}, works.position).cart_days ==
          0.0F,
      "site supply: nothing to cart costs no days");

  // THE LABOUR HOUR'S SEAM: a hauling work that names the unit drains this
  // seam and no other.
  core::WorkAssignment carting;
  carting.kind = core::WorkKind::kHauling;
  carting.unit = id;
  failures += Expect(core::WorkSeamOf(world, carting) == &works.construction.supply_days_remaining,
                     "site supply: a hauling work that names the works drains its supply seam");
  core::Vec2 place;
  failures += Expect(core::WorkPlaceOf(world, carting, place) && place.x == 0.0F,
                     "site supply: the carter's place is the sender, where he loads");

  // HALF THE SEAM DRAINED: half of what awaited is on the site this hour,
  // the rest awaits, and the next settlement measures from here.
  works.construction.supply_days_remaining = whole / 2.0F;
  core::SettleSiteSupply(config, world, false);
  const core::Grams landed = At(works.stock, logs);
  failures += Expect(landed > (kTenTonnes / 2) - 1000 && landed < (kTenTonnes / 2) + 1000 &&
                         landed + works.construction.awaiting[logs.value] == kTenTonnes &&
                         works.construction.supply_days_written == whole / 2.0F,
                     "site supply: half the seam drained puts half the logs on the site in "
                     "that hour, and nothing is lost between the two");
  // AN HOUR WITH NOBODY CARTING moves nothing.
  core::SettleSiteSupply(config, world, false);
  failures +=
      Expect(At(works.stock, logs) == landed, "site supply: an hour nobody carted lands nothing");

  // PAUSED: the seam is closed and what awaits waits.
  works.paused = 1;
  core::SettleSiteSupply(config, world, true);
  failures += Expect(works.construction.supply_days_remaining == 0.0F &&
                         works.construction.awaiting[logs.value] == kTenTonnes - landed &&
                         core::WorkSeamOf(world, carting) == nullptr,
                     "site supply: nothing is carted to a paused works — its seam is closed, "
                     "what awaits stays");
  works.paused = 0;
  // THE EVENING writes tomorrow's seam from what still awaits: about half
  // the first one.
  core::SettleSiteSupply(config, world, true);
  const float rest = works.construction.supply_days_remaining;
  failures += Expect(rest > whole * 0.45F && rest < whole * 0.55F,
                     "site supply: the evening writes tomorrow's seam from what still awaits");

  // THE SEAM DRAINED TO NOUGHT BRINGS THE LAST GRAM, whatever the shares'
  // rounding left, and the seam is closed.
  works.construction.supply_days_remaining = 0.0F;
  core::SettleSiteSupply(config, world, false);
  failures +=
      Expect(At(works.stock, logs) == kTenTonnes && !core::AnythingAwaits(works.construction) &&
                 works.construction.supply_days_written == 0.0F &&
                 core::WorkSeamOf(world, carting) == nullptr,
             "site supply: a seam drained to nought brings every gram, and no seam "
             "is left to drain");
  failures +=
      Expect(works.construction.legs.empty() && works.construction.supply_days_first == 0.0F,
             "site supply: the leg carted out is dropped, and the load as first priced "
             "goes with the wait");

  // TWO SENDERS, A LEG EACH, CARTED ONE AFTER THE OTHER (the direct leg): four
  // tonnes felled at a stand two kilometres north, by the log cart's way, and
  // six in the pile at the origin. Nothing is lost between the legs, the
  // stock and what awaits — to the gram, at every step.
  constexpr core::Grams kFour = 4'000'000;
  constexpr core::Grams kSix = 6'000'000;
  works.stock.assign(works.stock.size(), 0);
  works.construction.awaiting.assign(static_cast<std::size_t>(logs.value) + 1U, 0);
  works.construction.awaiting[logs.value] = kFour + kSix;
  core::SupplyLeg from_stand;
  from_stand.from = core::Vec2{.x = 1000.0F, .y = 2000.0F};
  from_stand.log_way = 1;
  from_stand.grams.assign(static_cast<std::size_t>(logs.value) + 1U, 0);
  from_stand.grams[logs.value] = kFour;
  from_stand.unpriced = kFour;
  core::SupplyLeg from_pile;
  from_pile.grams.assign(static_cast<std::size_t>(logs.value) + 1U, 0);
  from_pile.grams[logs.value] = kSix;
  from_pile.unpriced = kSix;
  works.construction.legs = {from_stand, from_pile};
  const auto conserved = [&works, logs]() {
    core::Grams on_legs = 0;
    for (const core::SupplyLeg& leg : works.construction.legs) {
      on_legs += At(leg.grams, logs);
    }
    return on_legs == At(works.construction.awaiting, logs) &&
           At(works.stock, logs) + on_legs == kFour + kSix;
  };
  core::SettleSiteSupply(config, world, false);
  const float stand_seam = works.construction.supply_days_remaining;
  failures += Expect(stand_seam > 0.0F && works.construction.supply_from.y == 2000.0F &&
                         works.construction.supply_log_way == 1 &&
                         works.construction.supply_days_first > stand_seam && conserved(),
                     "direct leg: the seam is the front leg's — the stand's, by the log cart — "
                     "and the load as first priced is both legs'");
  const float both = works.construction.supply_days_first;
  failures += Expect(core::WorkPlaceOf(world, carting, place) && place.y == 2000.0F,
                     "direct leg: the carter loads at the stand");
  works.construction.supply_days_remaining = 0.0F;
  core::SettleSiteSupply(config, world, false);
  failures += Expect(At(works.stock, logs) == kFour && works.construction.legs.size() == 1 &&
                         works.construction.supply_from.y == 0.0F &&
                         works.construction.supply_log_way == 0 &&
                         works.construction.supply_days_remaining > 0.0F &&
                         works.construction.supply_days_first == both && conserved(),
                     "direct leg: the stand's four tonnes land, the leg is dropped and the seam "
                     "is written for the pile in the same hour; the first price stands");
  works.construction.supply_days_remaining = 0.0F;
  core::SettleSiteSupply(config, world, false);
  failures += Expect(At(works.stock, logs) == kFour + kSix && works.construction.legs.empty() &&
                         !core::AnythingAwaits(works.construction) && conserved(),
                     "direct leg: the pile's six land after them — ten tonnes fixed, ten on the "
                     "site, none awaiting");
  return failures;
}
