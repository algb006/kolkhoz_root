// The chairman away in the district (core_common/chairman_away.h).

#include "core_common/chairman_away.h"

namespace core {

bool ChairmanAway(const WorldState& world) {
  const ChairmanState& chairman = world.chairman;
  const Tick now = world.calendar.tick;
  return chairman.away_from_tick != 0 && chairman.away_from_tick <= now &&
         now < chairman.away_until_tick;
}

bool DistrictDoor(OrderKind kind) {
  return kind == OrderKind::kTradePlan || kind == OrderKind::kOrderLimitLot;
}

void RefuseVillageOrdersWhileAway(WorldState& current) {
  if (!ChairmanAway(current)) {
    return;
  }
  for (OrderRow& order : current.orders.rows) {
    if (order.status == OrderStatus::kPending && !DistrictDoor(order.kind)) {
      order.status = OrderStatus::kRefused;
      order.refusal = OrderRefusal::kChairmanAway;
    }
  }
}

}  // namespace core
