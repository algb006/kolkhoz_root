// The checks of whose transport carries to the district
// (district_carts_checks.h).

#include "district_carts_checks.h"

#include <cstdint>
#include <iostream>

#include "core_common/calendar.h"
#include "core_common/limit_state.h"
#include "core_common/order_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "district_carts.h"
#include "production_config.h"

namespace {

constexpr core::Grams kTonne = 1'000'000;

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

/// A barn with 5 t of rye and nothing else; the plan asks 10 t of rye, 2 t of
/// potato and 1 t of a third good, of which 4 t of rye and the third are
/// delivered. No field: no seed is held back.
void SetUpVillage(core::ProductionConfig& config, core::WorldState& world) {
  config.unit_types.resize(1);
  config.unit_types[0].level_storage_capacity_kg = {100'000.0F};
  core::UnitRow barn;
  barn.type = core::UnitTypeId{0};
  barn.level = 1;
  barn.stock = {5 * kTonne, 0, 0};
  core::AppendRow(world.units, barn);
  world.plan.announced = 1;
  world.plan.due = {10 * kTonne, 2 * kTonne, kTonne};
  world.plan.delivered = {4 * kTonne, 0, kTonne};
  world.ledger.current.delivered = {4 * kTonne, 0, kTonne};
}

core::ResourceId Rye() {
  return core::ResourceId{0};
}

core::ResourceId All() {
  return core::ResourceId{};
}

int CheckTheCalendar() {
  int failures = 0;
  failures += Expect(
      core::DistrictCollectionDay(0) == core::kDaysPerMonth - 1 &&
          core::DistrictCollectionDay(core::kDaysPerMonth + 1) == (2 * core::kDaysPerMonth) - 1 &&
          core::DistrictCollectionDay(core::kDaysPerMonth - 1) == core::kDaysPerMonth - 1,
      "district calendar: the collection is the month's last day");
  core::WorldState world;
  world.calendar.day = core::kDaysPerMonth - 2;
  failures += Expect(!core::DistrictCollectsToday(world),
                     "district calendar: no collection on another day of the month");
  world.calendar.day = core::kDaysPerMonth - 1;
  failures += Expect(core::DistrictCollectsToday(world),
                     "district calendar: the cart comes on the month's last day");
  world.weather.mud = true;
  failures += Expect(!core::DistrictCollectsToday(world),
                     "district calendar: a mud day slips the collection to the next month's");
  return failures;
}

int CheckTheDistrictsRequest() {
  int failures = 0;
  core::ProductionConfig config;
  core::WorldState world;
  failures += Expect(
      core::RequestPlanDelivery(config, world, All(), 0, false) == core::OrderRefusal::kNoPlanYet,
      "district request: no figure named, nothing to request");
  SetUpVillage(config, world);
  world.calendar.day = 1;
  failures += Expect(
      core::RequestPlanDelivery(config, world, All(), 0, false) == core::OrderRefusal::kNone &&
          world.plan.requested == core::ResourceAmounts{6 * kTonne, 2 * kTonne, 0},
      "district request: every position rises to what is still owed, the "
      "delivered one stays at nought");
  failures += Expect(core::RequestPlanDelivery(config, world, All(), 0, false) ==
                             core::OrderRefusal::kRuleForbids &&
                         world.plan.requested == core::ResourceAmounts{6 * kTonne, 2 * kTonne, 0},
                     "district request: asked again, it does not rise twice");
  // The request takes no grain until the cart comes.
  failures +=
      Expect(world.units.rows[0].stock[0] == 5 * kTonne && world.plan.delivered[0] == 4 * kTonne,
             "district request: the order itself moves no grain");
  core::SettleDistrictCollection(config, world);
  failures += Expect(world.plan.delivered[0] == 4 * kTonne,
                     "district collection: nothing on a day that is not the month's last");
  world.calendar.day = core::kDaysPerMonth - 1;
  world.weather.mud = true;
  core::SettleDistrictCollection(config, world);
  failures +=
      Expect(world.plan.delivered[0] == 4 * kTonne, "district collection: nothing in the mud");
  world.weather.mud = false;
  core::SettleDistrictCollection(config, world);
  // Rye: 5 t in the barn of the 6 t asked; potato: nothing lies, 2 t wait.
  failures += Expect(world.plan.delivered == core::ResourceAmounts{9 * kTonne, 0, kTonne} &&
                         world.plan.requested == core::ResourceAmounts{kTonne, 2 * kTonne, 0} &&
                         world.units.rows[0].stock[0] == 0 &&
                         world.ledger.current.delivered[0] == 9 * kTonne,
                     "district collection: what lies in the stores leaves, the rest of the "
                     "request waits for the next month's");
  failures +=
      Expect(world.limit_deliveries.rows.empty() && (world.ledger.current.delivered_own.empty() ||
                                                     world.ledger.current.delivered_own[0] == 0),
             "district collection: the district's cart opens no own delivery");
  return failures;
}

int CheckTheOwnDelivery() {
  int failures = 0;
  core::ProductionConfig config;
  core::WorldState world;
  SetUpVillage(config, world);
  world.calendar.day = 5;
  failures += Expect(
      core::RequestPlanDelivery(config, world, Rye(), 0, true) == core::OrderRefusal::kNone &&
          world.limit_deliveries.rows.size() == 1,
      "own delivery: the order opens one outgoing row");
  if (world.limit_deliveries.rows.size() != 1) {
    return failures;
  }
  const core::LimitDeliveryRow& row = world.limit_deliveries.rows[0];
  failures +=
      Expect(row.outbound == 1 && row.own_carts == 1 && row.lot.value == core::kInvalidDefIdValue &&
                 row.arrive_day == 5 && row.goods == core::ResourceAmounts{5 * kTonne, 0, 0},
             "own delivery: the row goes out, by own carts, no lot, today, with what "
             "lies above the seed of the 6 t owed");
  failures +=
      Expect(world.units.rows[0].stock[0] == 5 * kTonne && world.plan.delivered[0] == 4 * kTonne,
             "own delivery: the grain leaves only as it is carted");
  failures += Expect(core::RequestPlanDelivery(config, world, Rye(), 0, true) ==
                             core::OrderRefusal::kRuleForbids &&
                         world.limit_deliveries.rows.size() == 1,
                     "own delivery: ordered again, the same grain goes on no second row");
  failures += Expect(
      core::RequestPlanDelivery(config, world, Rye(), 0, false) == core::OrderRefusal::kNone &&
          world.plan.requested[0] == kTonne,
      "own delivery: the district is asked only for what the row does not carry");
  world.calendar.day = (2 * core::kDaysPerMonth) - 1;
  core::SettleDistrictCollection(config, world);
  failures +=
      Expect(world.units.rows[0].stock[0] == 5 * kTonne && world.plan.delivered[0] == 4 * kTonne,
             "own delivery: the district's cart leaves the row's grain to its carters");
  return failures;
}

int CheckOwnCartsTakeOverTheRequest() {
  int failures = 0;
  core::ProductionConfig config;
  core::WorldState world;
  SetUpVillage(config, world);
  world.calendar.day = 1;
  core::RequestPlanDelivery(config, world, Rye(), 0, false);
  failures += Expect(
      core::RequestPlanDelivery(config, world, Rye(), 0, true) == core::OrderRefusal::kNone &&
          world.plan.requested[0] == kTonne && world.limit_deliveries.rows.size() == 1,
      "own delivery: what own carts take on leaves the district's request");
  return failures;
}

int CheckTheBonus() {
  int failures = 0;
  core::ProductionConfig config;
  config.limit.plan_met_points = 100;
  config.own_delivery_bonus_share = 0.1F;
  core::WorldState world;
  world.ledger.current.delivered = {10 * kTonne, 0};
  world.ledger.current.delivered_own = {5 * kTonne, 0};
  // Half by own carts: 10 % of (100 + 20) by one half = 6.
  failures += Expect(core::OwnDeliveryBonusPoints(config, world, true, 20.0F) == 6,
                     "bonus: a tenth of the plan's and the above-plan points, by the own share");
  failures += Expect(core::OwnDeliveryBonusPoints(config, world, false, 20.0F) == 1,
                     "bonus: a plan not met earns only the above-plan part");
  world.ledger.current.delivered_own = {0, 0};
  failures += Expect(core::OwnDeliveryBonusPoints(config, world, true, 20.0F) == 0,
                     "bonus: nothing carried by own carts, nothing");
  world.ledger.current.delivered_own = {10 * kTonne, 0};
  config.own_delivery_bonus_share = 0.0F;
  failures += Expect(core::OwnDeliveryBonusPoints(config, world, true, 20.0F) == 0,
                     "bonus: the share at nought switches it off");
  return failures;
}

}  // namespace

int CheckWhoseTransportToTheDistrict() {
  int failures = 0;
  failures += CheckTheCalendar();
  failures += CheckTheDistrictsRequest();
  failures += CheckTheOwnDelivery();
  failures += CheckOwnCartsTakeOverTheRequest();
  failures += CheckTheBonus();
  return failures;
}
