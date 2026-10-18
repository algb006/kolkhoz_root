// The district's milk cart (core_production/milk_cart.h).

#include "milk_cart.h"

#include <limits>

#include "core_common/calendar.h"
#include "core_common/ids.h"
#include "core_common/ledger_state.h"
#include "core_common/quantities.h"
#include "district_carts.h"
#include "stock_ops.h"

namespace core {
namespace {

/// Books `grams` of milk as delivered to the district: against the position
/// or outside any, and in the year's ledger.
void BookDelivered(const ProductionConfig& config,
                   WorldState& current,
                   Grams grams,
                   bool against_position) {
  if (grams <= 0) {
    return;
  }
  AddToStock(against_position ? current.plan.delivered : current.plan.delivered_outside,
             config.milk_resource,
             grams);
  AddLedgerAmount(current.ledger.current.delivered, config.milk_resource, grams);
  if (against_position) {
    LowerPlanRequest(current, config.milk_resource, grams);
  }
}

/// THE BARNS' MILK (#1056 C1.1; FarmingConfig::milk_at_barn): under the switch
/// the kolkhoz herds' milk lies in their barns, which are not stores
/// (IsTakenFrom answers no), so the cart reads them first and the stores
/// after. Nothing without the switch: no barn holds milk then.
Grams TakeMilkFromBarns(WorldState& current, const ProductionConfig& config, Grams wanted) {
  if (config.farming.milk_at_barn < 0.5F) {
    return 0;
  }
  Grams taken = 0;
  for (UnitRow& unit : current.units.rows) {
    if (taken >= wanted) {
      break;
    }
    if (!IsTakenFrom(unit, config)) {
      taken += TakeFromUnit(unit, config.milk_resource, wanted - taken);
    }
  }
  return taken;
}

/// The cart's take: the barns first, then the stores (TakeFromStorage).
Grams TakeMilkForCart(WorldState& current, const ProductionConfig& config, Grams wanted) {
  const Grams from_barns = TakeMilkFromBarns(current, config, wanted);
  return from_barns + TakeFromStorage(current, config, config.milk_resource, wanted - from_barns);
}

}  // namespace

bool MilkPositionStands(const ProductionConfig& config, const WorldState& current) {
  return current.plan.announced != 0 && config.milk_resource.value < current.plan.due.size() &&
         current.plan.due[config.milk_resource.value] > 0 &&
         current.calendar.day % kDaysPerYear >= MilkSeasonFirstDay();
}

std::uint32_t MilkSeasonFirstDay() {
  return static_cast<std::uint32_t>(Month::kMarch) * kDaysPerMonth;
}

void ShipMilkLeftover(const ProductionConfig& config, WorldState& current) {
  if (config.milk_resource.value == kInvalidDefIdValue) {
    return;
  }
  // Every gram: the barns' (under milk_at_barn, what the leg did not carry)
  // and the stores' - nothing stays overnight.
  const Grams from_barns = TakeMilkFromBarns(current, config, std::numeric_limits<Grams>::max());
  const Grams left = TakeableGrams(current, config, config.milk_resource);
  const Grams taken = from_barns + TakeFromStorage(current, config, config.milk_resource, left);
  // OVER THE PLAN, ALWAYS (the milk with debt, boss seq 26/28). What the
  // issue left used to go against the position while one stood. That
  // quietly made up the short days, so the position read met while the
  // cart's own days were short, and the book and the verdict read milk
  // through two different doors. Only the cart's share and its debt
  // (ShipMilkShare) count against the position now.
  BookDelivered(config, current, taken, false);
}

void ShipMilkShare(const ProductionConfig& config, WorldState& current) {
  if (config.milk_resource.value == kInvalidDefIdValue || current.plan.milk_daily_share <= 0 ||
      !MilkPositionStands(config, current)) {
    return;
  }
  // THE SHARE AND THE DEBT (PlanState::milk_debt): the day's share plus
  // what earlier short days still owe, the debt first because it is older.
  // A short day's difference joins the debt.
  const Grams wanted = current.plan.milk_daily_share + current.plan.milk_debt;
  const Grams taken = TakeMilkForCart(current, config, wanted);
  BookDelivered(config, current, taken, true);
  current.plan.milk_debt = wanted - taken;
  // THE BOOK SEES IT (boss seq 7 of epoch1-5): the debt of today, and at the
  // turn what was left unpaid (YearLedger::milk_debt).
  current.ledger.current.milk_debt = current.plan.milk_debt;
}

void CollectMilkDebtAlarms(const ProductionConfig& config,
                           const WorldState& world,
                           std::vector<Alarm>& alarms) {
  if (world.plan.milk_debt <= 0 || !MilkPositionStands(config, world)) {
    return;
  }
  // A DEBT STANDING MEANS THE CART TOOK EVERY LITRE: it is what the stores
  // could not give at the last milking, so nothing was left for the yards'
  // morning issue.
  Alarm alarm;
  alarm.kind = AlarmKind::kMilkAllToDebt;
  alarm.resource = config.milk_resource;
  alarm.amount = world.plan.milk_debt;
  alarms.push_back(alarm);
}

}  // namespace core
