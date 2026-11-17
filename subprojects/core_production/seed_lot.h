/// @file
/// @brief The district's seed lot (#1377; district.md l.84 «Зерно на сев,
/// 5 т», 30 points, the most-short crop of the next sowing; l.129 the year's
/// cap is the village's seed need; l.131 the lamp's second line; boss's
/// answers, core-boss-1377-seed-lot [2]).
/// @threading SINGLE_THREADED
/// The terms are a pure read; the purchase writes the limit and the cart, in
/// the decisions slot, through the lot door (OrderLimitLot).

#ifndef CORE_PRODUCTION_SEED_LOT_H_
#define CORE_PRODUCTION_SEED_LOT_H_

#include <cstdint>

#include "core_common/ids.h"
#include "core_common/seed_lot_view.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief The catalogue's seed lot: the first row of kind kSeed.
/// @return Invalid when the catalogue carries none.
LimitLotId SeedLotOf(const LimitCatalog& catalog);

/// @brief THE DOOR'S OWN QUESTION for a seed lot, asked in the door's order:
/// the lot (LotOrderable: kGateClosed, kRuleForbids for no price), the crop
/// (the largest shortfall of SeedShortByResource — the kSeedShort lamp's own
/// sum — less the seed the district's carts still carry, a lot's or a loan's;
/// a tie to the nearest sowing by DaysToSowingEnd, then the lower id; none
/// short: kRuleForbids, grams 0), the grams (the shortfall clamped to the
/// catalogue's least and most), the year's cap for that crop (bought by the
/// lot this year + the grams ≤ its seed need on the order day, the need with
/// its rot; above it kRuleForbids with `grams` the cap still allows), the
/// store (kNowhereToStore) and the points (pro-rated over the most, rounded
/// up; kLimitShort below them).
/// @param points_left The points the purchase would be paid from.
/// @pre `lot` names a kSeed row, or LotOrderable refuses it.
SeedLotView SeedLotTermsToday(const ProductionConfig& config,
                              const WorldState& world,
                              LimitLotId lot,
                              std::int32_t points_left);

/// @brief SeedLotTermsToday for the catalogue's seed lot and this year's
/// points: ISession::SeedLotOffer, the lamp's second line.
/// @return `lot` invalid and kNoSuchSubject when the catalogue has none.
SeedLotView SeedLotOfferOf(const ProductionConfig& config, const WorldState& world);

/// @brief Buys the seed lot on `terms` (SeedLotTermsToday answered kNone):
/// debits the points, books the grams in the year book (LimitState::
/// seed_lot_bought), and sends the district's cart with the seed.
/// @pre `terms.refusal` is kNone.
void BuySeedLot(const ProductionConfig& config,
                WorldState& current,
                const SeedLotView& terms,
                std::uint32_t arrive_day);

}  // namespace core

#endif  // CORE_PRODUCTION_SEED_LOT_H_
