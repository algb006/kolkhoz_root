/// @file
/// @brief What the district's seed lot would deliver today, and at what price
/// (#1377; district.md l.84 «Зерно на сев, 5 т», l.129 the year's cap, l.131
/// the «Семян не хватает» lamp's second line).
/// @threading PARALLEL_READONLY
/// Plain data, built between steps by a pure read (ISession::SeedLotOffer).

#ifndef CORE_COMMON_SEED_LOT_VIEW_H_
#define CORE_COMMON_SEED_LOT_VIEW_H_

#include <cstdint>

#include "core_common/ids.h"
#include "core_common/order_state.h"
#include "core_common/quantities.h"

namespace core {

/// @brief The seed lot as the door would take it today: the crop, the grams,
/// the points, and the door's own refusal. The lamp's second line reads it
/// («или купить по лимиту: 5 т ячменя за 30 баллов»); the text is rpg's.
struct SeedLotView {
  /// The catalogue's seed lot; invalid when the catalogue carries none (then
  /// `refusal` is kNoSuchSubject and nothing else is set).
  LimitLotId lot;

  /// The most-short seed of the next sowing (the largest shortfall; a tie
  /// goes to the nearest sowing, then the lower id). Invalid when no seed is
  /// short.
  ResourceId resource;

  /// Grams the lot would deliver: the shortfall, at least the catalogue's
  /// minimum and at most its maximum. When refused by the year's cap, the
  /// grams the cap still allows (0 when none), as the refused order's
  /// `amount` names them.
  Grams grams = 0;

  /// Points the lot would cost: its catalogue price pro-rated by grams over
  /// the maximum, rounded up (2 t of a 5 t, 30-point lot: 12).
  std::int32_t points = 0;

  /// kNone when the order would be taken today. Otherwise the door's own
  /// answer: kGateClosed, kRuleForbids (no price; no seed short; the year's
  /// cap for that crop — `grams` names what it still allows), kNowhereToStore
  /// (no store takes the seed), kLimitShort (points short of `points`).
  OrderRefusal refusal = OrderRefusal::kRuleForbids;
};

}  // namespace core

#endif  // CORE_COMMON_SEED_LOT_VIEW_H_
