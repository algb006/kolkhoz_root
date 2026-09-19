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
/// @param plan_fully_met Every position delivered at the district's met
///        share (PlanFullyDelivered through PositionDelivered, boss seq 103;
///        it read «at 100 %» until then). False for the first year, which has
///        had no verdict.
/// @param overfulfil_grain_tonnes Tonnes of grain equivalent over the plan,
///        less the short positions' deduction (PlanOverfulfilGrainTonnes,
///        district_plan.h; boss seq 88 — no longer gated on a plan met in
///        full), priced by the catalog's falling scale (OverfulfilPoints).
std::int32_t YearLimitPoints(const LimitCatalog& catalog,
                             FarmStatusTier tier,
                             bool plan_fully_met,
                             float overfulfil_grain_tonnes,
                             float reputation);

/// @brief The district cart's base term, in days, for a lot ordered today:
///        `limit_delivery_days`, divided by `mud_speed_factor` and rounded
///        on a day of РАСПУТИЦА (WeatherState::mud; boss seq 182) — 2 becomes
///        4. The random delay comes on top and is not stretched. Decided on
///        the order's day: a cart that set out dry arrives as it would have.
///        The MTS column's «does it arrive inside its window» asks the same
///        term, so a column ordered in the March mud can be refused as late.
std::uint32_t LimitBaseDeliveryDays(const ProductionConfig& config, const WorldState& world);

/// @brief Reads a kOrderLimitLot: checks the lot and the balance, takes the
///        points, and puts the lot's goods on a cart due on the day given by
///        LimitBaseDeliveryDays plus a delay drawn from the world's random
///        stream.
/// @param unstorable When not null and the answer is kNowhereToStore, set to
///        the first of the lot's goods no store of the village takes — the
///        refused row names WHAT (boss seq 159); left alone otherwise.
/// @return The refusal, or kNone when bought.
OrderRefusal OrderLimitLot(const ProductionConfig& config,
                           WorldState& current,
                           const OrderRow& order,
                           ResourceId* unstorable = nullptr);

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
/// @note THE REFUSAL FOR "NOWHERE TO PUT IT" IS BUILT AND LIVES IN
///       OrderLimitLot, not here: kNoRoomForStock, counting the roofs AND the
///       private yards. This note used to say it was a STUB and that the
///       sentence «некуда поставить — нельзя заказать» had no subject in this
///       core; that was true until 2026-09-16 and stopped being true with the
///       room check, and a stub marker outliving its stub is the one that
///       lies longest and most quietly.
void ArriveLivestock(const ProductionConfig& config, WorldState& current);

/// @brief Reads a kHandStock: hands `order.amount` head of `order.herd` back
///        to the district and credits the limit points they fetch.
/// @return The refusal, or kNone when handed over.
///
/// THE WAY OUT OF A HERD THERE IS NOTHING ELSE TO DO WITH (livestock design,
/// «Лишних лошадей сдают райкому»). The head leaves AT ONCE — no cart, no
/// waiting — which is deliberately not the mirror of the purchase, where the
/// head takes days to come.
///
/// WHAT IT PAYS. The share of the buying price for the band the head is in
/// (LimitCatalog::handover_share_*), rounded down, on the price of the lot
/// that SELLS that species. A kind the catalogue does not sell by the head,
/// or sells only as a batch, has no per-head price and is refused.
///
/// WHICH HEADS GO. The oldest first — adults before juveniles before
/// newborns, and within the adults from the old end (TakeOldestAdults).
/// The order names no head, and could not: a head is not an entity here.
///
/// @pre Runs in the production sub-step of the sequential decisions slot.
OrderRefusal OrderHandStock(const ProductionConfig& config,
                            WorldState& current,
                            const OrderRow& order);

/// @brief Electrification (era event 01), once a campaign: the three blockers
///        of electricity design §3, checked once a day.
///
/// THE THREE, AND EACH WITH ITS OWN REASON WRITTEN DOWN:
///   * ACCUMULATED LIMIT POINTS at or above `electrification_points_min` —
///     the measure of how far the farm has come, and the only one of the
///     three that the chairman earns rather than waits for;
///   * A YEAR LIVED since the start. «Раньше не придёт ни при каких успехах»,
///     and the design calls it «не задержка, а защита от собственной
///     жадности»: an event in the first spring would tempt the village into
///     spending points it needs for bread;
///   * AN OFFICE STANDING. Electrification is the first building the farm
///     ORDERS rather than raises, and an order needs an address, papers and
///     somebody for the district to talk to — not the corner of a church
///     annexe.
///
/// The district puts up the line and the substation itself, in a cut-scene,
/// so nothing is built here: the core raises `kElectrificationUnlocked` and
/// the layer and the script take it from there. Raised ONCE — the flag lives
/// in WorldState::era_events and is saved.
///
/// @pre Runs in the production sub-step of the sequential decisions slot.
void RunEraEvents(const ProductionConfig& config, WorldState& current);

/// @brief The year's turn, right after the district's verdict: this year's
///        unspent points burn into the closing year's ledger, and the new
///        year's grant is made from that verdict.
/// @param plan_fully_met As YearLimitPoints.
/// @param overfulfil_grain_tonnes As YearLimitPoints: PlanOverfulfilGrainTonnes
///        of the closing year, read before the verdict hands the next plan down.
void TurnLimitYear(const ProductionConfig& config,
                   WorldState& current,
                   bool plan_fully_met,
                   float overfulfil_grain_tonnes);

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
