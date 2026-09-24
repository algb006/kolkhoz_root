// The district's milk cart (core_production/milk_cart.h).

#include "milk_cart.h"

#include "core_common/calendar.h"
#include "core_common/ids.h"
#include "core_common/ledger_state.h"
#include "core_common/quantities.h"
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
  const Grams left = TakeableGrams(current, config, config.milk_resource);
  const Grams taken = TakeFromStorage(current, config, config.milk_resource, left);
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
  const Grams taken = TakeFromStorage(current, config, config.milk_resource, wanted);
  BookDelivered(config, current, taken, true);
  current.plan.milk_debt = wanted - taken;
}

}  // namespace core
