/// @file
/// @brief The district's limit: the year's points and the lots on their way
/// (district design §1, §4; boss's decisions of 2026-09-13, parcels 208 and
/// 211).
/// @threading PARALLEL_READONLY
/// Plain data. Written only in the sequential decisions slot — the order that
/// buys a lot, the delivery that lands, the year's turn that burns and grants
/// — and read by anyone between steps.
///
/// A POINT IS NOT MONEY (district design §1): a conventional unit of the
/// district's shortage, handed out once a year and BURNT at the year's end
/// if not spent. The balance therefore carries no history; the year's grant,
/// spend and burn are the ledger's (ledger_state.h).

#ifndef CORE_COMMON_LIMIT_STATE_H_
#define CORE_COMMON_LIMIT_STATE_H_

#include <cstdint>

#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_common/state_table.h"

namespace core {

/// @brief The points the chairman may still spend this year.
struct LimitState {
  /// Whole points left this year. Never negative: an order that costs more
  /// is refused (kLimitShort), not taken on credit.
  std::int32_t points = 0;
};

/// @brief One lot bought from the catalogue and not yet in the stores: the
/// district's own cart on the road (district design §4, "транспортом
/// района").
struct LimitDeliveryRow {
  /// The catalogue row it was bought as (tables/limit_catalog.csv).
  LimitLotId lot;

  /// The campaign day the cart reaches the village: the order's day plus
  /// limit_delivery_days plus a delay of 0..limit_delivery_delay_days_max drawn
  /// from the world's generator state at the order and the order's tick
  /// (district_limit.cpp, OrderLimitLot). From that day on the goods go through the store
  /// door every day until none is left.
  std::uint32_t arrive_day = 0;

  /// What is still on the cart, grams by resource. FROZEN AT THE ORDER from
  /// the lot's amounts: a balance edit mid-delivery does not change a cart
  /// already on the road. What the door refuses stays here — "невлезшее ждёт
  /// у ворот" (boss, parcel 211) — and is offered again the next day.
  ResourceAmounts goods;
};

/// @brief Every cart on its way, in the order the lots were bought.
using LimitDeliveryTable = StateTable<LimitDeliveryId, LimitDeliveryRow>;

}  // namespace core

#endif  // CORE_COMMON_LIMIT_STATE_H_
