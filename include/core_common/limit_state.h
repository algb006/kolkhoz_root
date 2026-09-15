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

/// @brief Where the district MTS's column stands (MTS design §1; boss,
/// parcels 448-449). One column a season, bought as a `service` lot.
enum class MtsColumnPhase : std::uint8_t {
  /// No column ordered, or the last one's season is over and read.
  kNone = 0,

  /// Bought and on the district's road; arrives on `arrive_day`.
  kOnTheRoad,

  /// At the field camp and working the fields, 10 ha a working day (STUB),
  /// until 70 ha are worked or the season's field-work window closes.
  kWorking,

  /// Done: the limit worked out or the window closed. Stays until the next
  /// order, so a reader can see how the season went.
  kGone,

  /// No field camp stood when the column was due, nor by the window's end:
  /// the column never came, the points are not returned (MTS §1).
  kNotArrived,

  kMtsColumnPhaseCount,
};

/// @brief The district MTS's column of this season — one row, not a table:
/// "одна колонна на сезон".
///
/// CONTRACT (boss, parcel 449; numbers STUB, world_params.csv):
///   * the order is kOrderLimitLot of a lot of kind `service`
///     (mts_column_spring / mts_column_autumn): accepted, the points spent,
///     not cancellable (MTS §1); the district's quota — "a neighbour may take
///     the column first" — is a STUB: the column is always given;
///   * it arrives limit_delivery_days after the order, at the field camp
///     (unit field_camp), if one stands; kMtsColumnArrived {unit: the camp};
///   * it works 10 ha a working day, spring doing ploughing, harrowing and
///     sowing of a hectare at once, autumn reaping and carting; the fields are
///     taken by the brigade's queue (window, deadline), nearest the camp
///     first, until 70 ha are worked;
///   * it leaves when the 70 ha are worked or the window closes;
///     kMtsColumnLeft {amount: whole hectares worked};
///   * with no camp by the window's end it never comes;
///     kMtsColumnNotArrived {}.
///   * windows: spring March-May, autumn August-October.
struct MtsColumnState {
  MtsColumnPhase phase = MtsColumnPhase::kNone;

  /// The lot the column was bought as (spring or autumn); invalid in kNone.
  LimitLotId lot;

  /// The campaign day the column reaches the village.
  std::uint32_t arrive_day = 0;

  /// The field camp it works from; invalid until it arrives.
  UnitId camp;

  /// Hectares worked this season, out of the limit.
  float worked_ha = 0.0F;

  /// The field the column has begun and not finished; invalid when none.
  /// Kept past kGone: the crew still owes only the hectares the column left.
  FieldId field;

  /// Hectares of `field` the column has worked, 0..area. The field's current
  /// phase owes its crew the rest only (mts_column.cpp): a phase chain is
  /// field-wide, and "a hectare at once" is carried by this share.
  float field_ha = 0.0F;
};

}  // namespace core

#endif  // CORE_COMMON_LIMIT_STATE_H_
