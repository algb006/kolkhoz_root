// The district's milk cart (core_production/milk_cart.h).

#include "milk_cart.h"

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

bool MilkPositionStands(const ProductionConfig& config, const WorldState& current) {
  return current.plan.announced != 0 && config.milk_resource.value < current.plan.due.size() &&
         current.plan.due[config.milk_resource.value] > 0;
}

}  // namespace

void ShipMilkLeftover(const ProductionConfig& config, WorldState& current) {
  if (config.milk_resource.value == kInvalidDefIdValue) {
    return;
  }
  const Grams left = TakeableGrams(current, config, config.milk_resource);
  const Grams taken = TakeFromStorage(current, config, config.milk_resource, left);
  BookDelivered(config, current, taken, MilkPositionStands(config, current));
}

void ShipMilkShare(const ProductionConfig& config, WorldState& current) {
  if (config.milk_resource.value == kInvalidDefIdValue || current.plan.milk_daily_share <= 0 ||
      !MilkPositionStands(config, current)) {
    return;
  }
  const Grams taken =
      TakeFromStorage(current, config, config.milk_resource, current.plan.milk_daily_share);
  BookDelivered(config, current, taken, true);
}

}  // namespace core
