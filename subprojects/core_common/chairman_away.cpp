// The chairman away in the district (core_common/chairman_away.h).

#include "core_common/chairman_away.h"

namespace core {

bool ChairmanAway(const WorldState& world) {
  const ChairmanState& chairman = world.chairman;
  const Tick now = world.calendar.tick;
  // INCLUSIVE OF THE RETURN TICK, like a resident's absence (boss,
  // boss-core-epoch1-2 seq 1, answer 6: «в отъезде — одно слово, один
  // ответ»): the village's orders of that tick are answered first in the
  // decisions slot, before the trip brings him home, so on that tick he is
  // still away for them.
  return chairman.away_from_tick != 0 && chairman.away_from_tick <= now &&
         now <= chairman.away_until_tick;
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
