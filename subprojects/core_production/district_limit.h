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
///
/// GOODS, THE TWO MTS COLUMNS, AND — SINCE THE LIVESTOCK WINDOW — STOCK.
/// A vehicle, a person and a `choice` lot still refuse with kRuleForbids and
/// still say STUB, because they have windows of their own that nobody has
/// built. Stock no longer does, and the difference is worth naming in the
/// contract rather than in a commit message: the livestock refusal was read
/// as the district's ROLE for a day and a half («живое у него не покупают»),
/// on the strength of a report of mine that described this very `if` as a
/// rule of the world. The catalogue had priced a horse at 70 points from
/// epoch I the whole time.
///
/// @return kNone, kNoSuchSubject, kGateClosed or kRuleForbids; never
///         kLimitShort, which is the balance's question and not the lot's.
///         A livestock lot refuses with kRuleForbids while its head count is
///         nil — the batches, whose size is unwritten — exactly as a goods
///         lot with no amount does.
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

/// @brief The bought stock at the day's last tick: every arrival whose day
///        has come stands in the village, and its row leaves the table.
///
/// WHERE THE HEAD GOES. It joins the kolkhoz herd of its kind — the herd row
/// under a roof with room first, then any kolkhoz herd of that kind, and if
/// the village has none, a new herd row is made. A head with no roof is
/// BILLETED at the private yards like any other, which is not a failure
/// state: it is what the start canon does with all sixteen horses from the
/// first morning (herd_system.cpp, RunBilleting), and it costs yield rather
/// than the animal.
///
/// WHAT IT ADDS TO. kAdultStart goes to `adult_count`, to
/// `adult_male_count` when `male`, and adds the kind's adult-entry age to
/// `adult_age_game_years_total`. kYoung goes to `newborn_count` and climbs
/// the cohort ladder from there.
///
/// @pre Runs in the production sub-step of the sequential decisions slot,
///      after ArriveLimitDeliveries and before the herd day, so a head that
///      arrives this morning eats tonight.
/// @note THE ORDER'S REFUSAL FOR "NOWHERE TO PUT IT" IS NOT HERE AND IS NOT
///       ANYWHERE YET — STUB, and the gap is named rather than filled.
///       Both design documents say «некуда поставить — нельзя заказать», and
///       in this core that sentence has no subject: kolkhoz stock has no
///       ceiling at all, because a head without a roof billets instead of
///       being refused. A refusal written against an unlimited yard would
///       never fire once, and an unfireable rule is the next thing somebody
///       reports as a rule of the world. Waiting on boss (resume thread,
///       parcel 4).
void ArriveLivestock(const ProductionConfig& config, WorldState& current);

/// @brief The year's turn, right after the district's verdict: this year's
///        unspent points burn into the closing year's ledger, and the new
///        year's grant is made from that verdict.
/// @param plan_fully_met As YearLimitPoints.
void TurnLimitYear(const ProductionConfig& config, WorldState& current, bool plan_fully_met);

/// @brief The district MTS's column, every tick after the field phases move
///        (limit_state.h, MtsColumnState — the contract is written there).
///        Each tick the field it has begun keeps its crew to the hectares the
///        column left; at the day's last tick the
///        column on the road arrives at the field camp or, with no camp by
///        the window's end, never comes; a working column takes the fields by
///        the brigade's queue nearest the camp and works its hectares; it
///        leaves at its limit or at the window's end. Emits
///        kMtsColumnArrived, kMtsColumnLeft and kMtsColumnNotArrived.
/// @pre The kOrderLimitLot of a `service` lot has put the column on the road.
void RunMtsColumn(const ProductionConfig& config, WorldState& current);

/// @brief The first year's grant, on the campaign's first tick — the same
///        moment the first winter's manure plan is made, since genesis hands
///        over a world and no system has run: the base fund alone, as no plan
///        has been judged yet (boss, parcel 211).
void GrantFirstLimitYear(const ProductionConfig& config, WorldState& current);

}  // namespace core

#endif  // CORE_PRODUCTION_DISTRICT_LIMIT_H_
