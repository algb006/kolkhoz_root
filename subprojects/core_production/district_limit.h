/// @file
/// @brief The district's limit in the simulation: the year's grant and burn,
/// buying a lot, and the district's cart arriving (district design §1, §4;
/// boss, parcels 208 and 211).
/// @threading SINGLE_THREADED
/// Every entry point runs from the production sub-step of the decisions slot
/// (phase 3) on the sim thread — the order when it is read, the cart at the
/// day's last tick, the grant at the year's turn — or at genesis. They write
/// the world's limit and the stores, so they can only live in a sequential
/// slot.

#ifndef CORE_PRODUCTION_DISTRICT_LIMIT_H_
#define CORE_PRODUCTION_DISTRICT_LIMIT_H_

#include <cstdint>

#include "core_catalog/limit_catalog.h"
#include "core_common/ids.h"
#include "core_common/order_state.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief Whether `lot` may be ordered at all in `epoch`, and if not, why, in
/// the order book's words (order_state.h, kOrderLimitLot).
/// @return kNone, kNoSuchSubject, kGateClosed or kRuleForbids; never
///         kLimitShort, which is the balance's question and not the lot's.
OrderRefusal LotOrderable(const LimitCatalog& catalog, LimitLotId lot, Epoch epoch);

/// @brief The raikom reputation's multiplier on the year's grant (district
/// design §5): 0–20 ×0.7, 21–40 ×0.85, 41–60 ×1.0, 61–80 ×1.2, 81–100 ×1.4.
/// @param reputation ChairmanState::raikom_reputation, 0..100; out of range
///        is taken to its nearer end.
float LimitReputationMultiplier(float reputation);

/// @brief The points granted for a year: (base by tier + plan_met_points if
/// the plan was delivered in full + the overfulfilment term) × the reputation
/// multiplier, rounded to whole points and never negative.
/// @param plan_fully_met Every position delivered at 100 % — not the
///        district's met share, which decides failure and trial (boss, parcel
///        211). False for the first year, which has had no verdict.
/// @param overfulfil_percent Percent over the plan. STUB: 0 until the core
///        can deliver above the plan (district design §9, surplus delivery).
std::int32_t YearLimitPoints(const LimitCatalog& catalog,
                             FarmStatusTier tier,
                             bool plan_fully_met,
                             float overfulfil_percent,
                             float reputation);

/// @brief Reads a kOrderLimitLot: checks the lot and the balance, takes the
///        points, and puts the lot's goods on a cart due on the day given by
///        limit_delivery_days plus a delay drawn from the world's random
///        stream.
/// @return The refusal, or kNone when bought.
OrderRefusal OrderLimitLot(const ProductionConfig& config,
                           WorldState& current,
                           const OrderRow& order);

/// @brief The carts at the day's last tick: every cart whose day has come
///        puts what it still carries through the store door; an empty cart
///        leaves the table.
void ArriveLimitDeliveries(const ProductionConfig& config, WorldState& current);

/// @brief The year's turn, right after the district's verdict: this year's
///        unspent points burn into the closing year's ledger, and the new
///        year's grant is made from that verdict.
/// @param plan_fully_met As YearLimitPoints.
void TurnLimitYear(const ProductionConfig& config, WorldState& current, bool plan_fully_met);

/// @brief The first year's grant, on the campaign's first tick — the same
///        moment the first winter's manure plan is made, since genesis hands
///        over a world and no system has run: the base fund alone, as no plan
///        has been judged yet (boss, parcel 211).
void GrantFirstLimitYear(const ProductionConfig& config, WorldState& current);

}  // namespace core

#endif  // CORE_PRODUCTION_DISTRICT_LIMIT_H_
