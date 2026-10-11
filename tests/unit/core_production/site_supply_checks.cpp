// The checks of the supply seam of a works (site_supply_checks.h).

#include "site_supply_checks.h"

#include <cstdint>
#include <iostream>
#include <string>

#include "core_common/calendar.h"
#include "core_common/quantities.h"
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

}  // namespace

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
