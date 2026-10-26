// The checks of the herd plan's contract (herd_plan_checks.h).

#include "herd_plan_checks.h"

#include <cstdint>
#include <iostream>

#include "core_common/herd_state.h"
#include "core_common/ledger_state.h"
#include "core_common/order_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "herd_life.h"
#include "livestock_homes.h"
#include "production_config.h"
#include "production_orders.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

/// A cattle yard of 30 places with a kolkhoz herd of 10 cows in it, and a
/// second herd of 5 of the same yard type in a second yard of 30.
struct Farm {
  core::ProductionConfig config;
  core::WorldState world;
  core::HerdId cows;
  core::HerdId other;
};

Farm MakeFarm() {
  Farm farm;
  farm.config.unit_types.resize(1);
  farm.config.unit_types[0].level_livestock_capacity_head.assign(3, 30.0F);
  farm.config.livestock.resize(1);
  core::LivestockDef& cow = farm.config.livestock[0];
  cow.home_unit = core::UnitTypeId{0};
  cow.sexed = 0;  // no sire to keep in this world: the gate under test alone
  cow.births_per_game_year = 48.0F;
  cow.litter_heads = 1.0F;
  farm.config.farming.birth_from_month = 0;
  farm.config.farming.birth_to_month = 0;
  const auto yard = [&farm] {
    core::UnitRow unit;
    unit.type = core::UnitTypeId{0};
    unit.level = 1;
    return core::AppendRow(farm.world.units, unit);
  };
  core::HerdRow herd;
  herd.kind = core::LivestockKindId{0};
  herd.unit = yard();
  herd.adult_count = 10;
  herd.fed_share = 1.0F;
  farm.cows = core::AppendRow(farm.world.herds, herd);
  herd.unit = yard();
  herd.adult_count = 5;
  farm.other = core::AppendRow(farm.world.herds, herd);
  return farm;
}

/// Issues the order through the production order book, as the session does.
core::OrderRefusal SetPlan(Farm& farm, core::HerdId herd, std::int64_t n) {
  core::OrderRow order;
  order.kind = core::OrderKind::kSetHerdPlan;
  order.status = core::OrderStatus::kPending;
  order.herd = herd;
  order.amount = n;
  const core::OrderId id = core::AppendRow(farm.world.orders, order);
  core::ConsumeProductionOrders(farm.config, farm.world);
  return farm.world.orders.rows[core::FindRow(farm.world.orders, id)].refusal;
}

core::HerdRow& Herd(Farm& farm, core::HerdId id) {
  return farm.world.herds.rows[core::FindRow(farm.world.herds, id)];
}

/// One day of the calving band for the herd; returns the heads born.
std::uint32_t BornInADay(Farm& farm) {
  core::HerdRow& herd = Herd(farm, farm.cows);
  const std::uint32_t before = core::TotalHeads(herd);
  core::YearLedger book;
  core::RunBirths(farm.config,
                  farm.config.livestock[0],
                  core::LivestockKindId{0},
                  herd,
                  farm.cows,
                  true,
                  false,
                  farm.world,
                  book);
  return core::TotalHeads(herd) - before;
}

}  // namespace

int CheckTheHerdPlan() {
  int failures = 0;
  // THE PLACES (boss, core-boss-1289 [2] B): two yards of the type, 60, less
  // the other herd's 5 heads - this herd's own 10 are its places.
  {
    Farm farm = MakeFarm();
    failures +=
        Expect(core::HerdPlaces(
                   farm.config, farm.world, core::FindRow(farm.world.herds, farm.cows)) == 55U,
               "herd plan: a herd's places are its type's room less the other herds' heads");
  }
  // THE ORDER: accepted at the places, refused above them («отказ один»),
  // refused for a yard's own herd and for no herd, accepted below every
  // floor (nought too), and kNoHerdPlan clears it.
  {
    Farm farm = MakeFarm();
    failures +=
        Expect(SetPlan(farm, farm.cows, 55) == core::OrderRefusal::kNone &&
                   Herd(farm, farm.cows).plan_heads == 55U &&
                   SetPlan(farm, farm.cows, 56) == core::OrderRefusal::kNoRoomForStock &&
                   Herd(farm, farm.cows).plan_heads == 55U,
               "herd plan: N up to the places is set, one above is refused kNoRoomForStock");
    failures +=
        Expect(SetPlan(farm, farm.cows, 0) == core::OrderRefusal::kNone &&
                   Herd(farm, farm.cows).plan_heads == 0U &&
                   SetPlan(farm, farm.cows, core::kNoHerdPlan) == core::OrderRefusal::kNone &&
                   Herd(farm, farm.cows).plan_heads == core::kNoHerdPlan,
               "herd plan: nought is a plan, kNoHerdPlan clears it");
    Herd(farm, farm.other).household_owned = 1;
    failures +=
        Expect(SetPlan(farm, farm.other, 3) == core::OrderRefusal::kNotEligible &&
                   SetPlan(farm, core::HerdId{999}, 3) == core::OrderRefusal::kNoSuchSubject,
               "herd plan: a yard's own herd is not eligible, no herd is no subject");
  }
  // THE BIRTH GATE, on top of the roof share: no plan - calves; N above the
  // heads - calves; N at the heads - none, and nothing accrues (C: a stopped
  // calf is lost, not banked).
  {
    Farm none = MakeFarm();
    Farm above = MakeFarm();
    Herd(above, above.cows).plan_heads = 20;
    Farm at = MakeFarm();
    Herd(at, at.cows).plan_heads = 10;
    const float carry_before = Herd(at, at.cows).birth_progress;
    failures += Expect(BornInADay(none) > 0U && BornInADay(above) > 0U,
                       "herd plan: with no plan, or N above the heads, the herd calves");
    failures += Expect(BornInADay(at) == 0U && Herd(at, at.cows).birth_progress == carry_before,
                       "herd plan: at its N the herd calves nought and banks nothing");
  }
  return failures;
}
