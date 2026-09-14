/// @file
/// @brief The night's outings of the quiet trades — who went out, doing what,
/// where and in which hours (crime design §7, §9; "Ночной промысел в ядре
/// Эпохи I — числами"; boss, parcel 346).
/// @threading PARALLEL_READONLY
/// Plain data. Written only by the residents' decisions sub-step (phase 3) on
/// the sim thread: the night's rows are laid down in the hour the trades go
/// out, and yesterday's are dropped then. Read by anyone between steps — the
/// host reads the place and the hours of an outing through this table, the
/// event saying only that it happened. SAVED (save format 39).
///
/// WHY A ROW AND NOT THE EVENT. The host's scene needs who, what, where and
/// when (host 93, №19–21). The event carries who and what; a position has no
/// place in SimEvent, and widening every event of the game by a point for one
/// kind of it would cost every consumer. So the event names the outing and
/// this row describes it, for the moonlit night it belongs to.

#ifndef CORE_COMMON_NIGHT_TRADE_STATE_H_
#define CORE_COMMON_NIGHT_TRADE_STATE_H_

#include <cstdint>

#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_common/resident_state.h"
#include "core_common/state_table.h"

namespace core {

/// @brief One resident out on his trade tonight.
struct NightOutingRow {
  ResidentId resident;

  NightTrade trade = NightTrade::kNone;

  /// The campaign day the night began on — the moonlit day of the month.
  std::uint32_t day = 0;

  /// Where he is: his own gate for a distiller, a spot of
  /// tables/night_fishing_spots.csv for a net fisher, a square of the old
  /// forest for a hunter. Metres from the map's south-west corner.
  Vec2 position;

  /// The hour he goes out, 0..23, on `day`; and the hour he is back, on the
  /// next day when it is smaller than `hour_out`.
  std::uint8_t hour_out = 0;
  std::uint8_t hour_back = 0;
};

/// @brief The night's outings, in the order they were laid down.
using NightOutingTable = StateTable<NightOutingId, NightOutingRow>;

/// @brief What the distillers have carried off the kolkhoz stores this
/// calendar month, and whether the village has come to complain yet (crime
/// design §7, "Утечка сырья… числами"; boss, parcel 364). SAVED.
struct NightTheftTally {
  /// Grams of raw material taken since the first day of `month_index`.
  Grams stolen_this_month = 0;

  /// The month the tally counts: year × 12 + month, from the campaign's
  /// calendar. A day of another month starts the tally again.
  std::uint32_t month_index = 0;

  /// 0/1: kStoreLeakComplaint has been raised — once a campaign.
  std::uint8_t complaint_raised = 0;
};

}  // namespace core

#endif  // CORE_COMMON_NIGHT_TRADE_STATE_H_
