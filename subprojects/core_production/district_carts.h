/// @file
/// @brief WHOSE TRANSPORT TO THE DISTRICT (routing
///        stage В5; district §4 «Логистика с районом», transport §12 «Задачи
///        с райцентром»): every operation with the district - a lot received
///        by the limit, or the plan delivered - names on its request whose
///        transport carries it; the district's cart keeps a calendar, own carts
///        a task in the common queue and a small bonus. Private to
///        core_production.
///
/// THE CORE BEFORE В5 (0.37.257): own carts only for timber lots
/// (LimitDeliveryRow::own_carts, set by the lot's goods; decision 279); every
/// other lot comes by the district's cart on its arrive_day; the plan's
/// delivery moves grain to the district with NO carting at all - instant on
/// the order (kDeliverPlan, DeliverPlanNow), or at the deadline (DeliverPlan);
/// no request carries a choice; no bonus. The district's cart for a delivery
/// was instant, so own transport could only be pure bonus - the harvest trap
/// of district §4 («надбавка за свой транспорт достаётся ценой уборки») had
/// no jaw.
///
/// ECON's THREE NUMBERS (econ-boss-v5-district-lines-2026-10-14 [1], taken by
/// boss in core-boss-v5-scope-2026-10-14 [3], written into district §4):
/// (b) THE DISTRICT's CART FOR THE PLAN KEEPS A CALENDAR, foreseeable, no
///     draw: ONE COLLECTION A GAME MONTH, on the month's last day (day 4 of
///     4): everything the standing request names that lies in the stores that
///     morning. In a MUD month (WeatherState::mud on that day) no collection -
///     it slips to the next month's. The year's last collection is the
///     deadline's day; the «до позиции не хватает N» warning moves one
///     collection earlier. LOTS TO THE VILLAGE: unchanged.
/// (a) THE BONUS, points only, no reputation: 10 % of the points the delivered
///     tonnes earn, by the share own carts carried - 10 % × plan_met_points ×
///     (own share of the plan's tonnes) + 10 % × the above-plan points × (own
///     share of the above-plan tonnes). The 10 % is a table cell
///     (world_params `own_delivery_bonus_share`, the body's home; boss's base;
///     0.1 by default until the export lands).
/// (c) THE RUN CHAIRMAN (tests/run/common, the canon): own carts for the
///     plan's delivery only on a day with no field in its harvest window and
///     no heap lying on a field; the district otherwise. The novice (host):
///     always the district. Ships with the stage; every later pair names it.
///
/// THE DOORS
/// 1. THE CHOICE ON THE REQUEST: kDeliverPlan and kOrderLimitLot read their
///    order's `enable` byte as «own transport» (1) or the district's (0) - no
///    new order field. A timber lot stays forced own (decision 279).
/// 2. THE PLAN's STANDING REQUEST (PlanState, saved): what the chairman asked
///    to deliver above the deadline's default, by position, and by whose
///    transport. kDeliverPlan with the district's transport RAISES the
///    request (collected on the month's last day, or slipped in the mud); with
///    own transport it opens the OWN DELIVERY: an outbound LimitDeliveryRow
///    (`outbound` 1), carted by the district lot's own seam from the store
///    that holds the first good to the district centre, drained by carters as
///    the lot's is, a logistics task in the common queue, stopped in the mud;
///    grams count to the plan AS THEY ARE CARTED (the evening's settlement).
///    No order at all: the deadline's delivery, as today (never slips).
/// 3. OWN CARTS FOR ANY LOT: kOrderLimitLot's `enable` 1 sets
///    LimitDeliveryRow::own_carts for any lot; the lot then waits at the
///    district centre from its arrive_day as the timber lot does (the same
///    seam, SettleDistrictLotHauling). Its destination: the store door's home
///    of its goods, not always the log pile.
/// 4. THE LOT WINDOW's HORSE PRICE (boss 2 October 2026, «принято и не
///    построено»): a read door - the cart-days and horse-days a lot WOULD take
///    by own carts today, before the order (LotOwnCartDays).
/// 5. THE BONUS: YearLimitPoints gains the own shares; YearLedger books the
///    plan's grams delivered by own carts (delivered_own) beside `delivered`.
///
/// THE SAVE (VERSION_SAVE 163; the contract predicted five fields, the body
/// built three - the own delivery rides a lot row, whose seam is saved):
///   * PlanState::requested, ResourceAmounts - the district's standing
///     request beyond the deadline's default; cleared at the year's turn;
///   * LimitDeliveryRow::outbound, a byte - the row is the plan's own
///     delivery going out, not a lot coming in;
///   * YearLedger::delivered_own, ResourceAmounts - the year's plan grams that
///     came by own carts;
///   * NOTHING for the slipped collection: the mud is read on the collection's
///     day (WeatherState::mud is saved already), the calendar is the clock's.
///
/// THE PAIR (econ's lines, set before the run; nine villages, four years):
/// I today | D the calendar, always the district | C the calendar + the canon's
/// rule | O always own.
///   1. BOTH JAWS: C's own deliveries on harvest-window days are 0 by its rule;
///      O's harvest left on the fields and heap-days not below C's - if O costs
///      nothing over C the bonus goes to 0 (and returns with whole trips).
///   2. THE CALENDAR's JAW: in D, plan positions failed that I met - each with
///      the warning lit at least one collection before; a failure with no prior
///      warning fails the stage.
///   3. SIZE: bonus points a village-year in C, median and worst, ≤ 8 % of the
///      year's points income.
///   4. SAFETY, C against I: houses at year 4 and construction inside ±2.5 %
///      (median, range, count below -10 %); harvest lost not above I's by more
///      than 10 % of I's own; no kolkhoz herd-evening underfed.
///   5. PRINTED: own-cart hours by month in C and O; the plan's tonnes by who
///      carted; the mud months' slipped collections.
///   Read against the FLUID carting (0.37.257's stated STUB): own carts are a
///   quarter cheaper than whole trips would make them - said with every print.
///
/// @threading SINGLE_THREADED
/// Production's orders (decisions slot) and its evening settlement, as the
/// district lot's seam today.

#ifndef CORE_PRODUCTION_DISTRICT_CARTS_H_
#define CORE_PRODUCTION_DISTRICT_CARTS_H_

#include <cstdint>

#include "core_common/haul.h"
#include "core_common/ids.h"
#include "core_common/order_state.h"
#include "core_common/quantities.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief The day of the district's collection in the month of `day`: the
///        month's last day. Pure calendar.
SimDay DistrictCollectionDay(SimDay day);

/// @brief Whether the district's cart collects TODAY: the month's last day,
///        and no mud on the district's road (WeatherState::mud). A mud day
///        slips the collection to the next month's.
bool DistrictCollectsToday(const WorldState& world);

/// @brief The collection: everything the standing request names that lies
///        in the stores this morning leaves for the district, counted to the
///        plan (PlanState::delivered, YearLedger::delivered). Called once a
///        day in the evening's settlement; does nothing on another day.
void SettleDistrictCollection(const ProductionConfig& config, WorldState& current);

/// @brief kDeliverPlan's consumer under В5 (replaces the instant
///        DeliverPlanNow): `own` 0 raises the district's standing request for
///        `resource` (all positions when invalid) by `amount` (0: what is owed);
///        `own` 1 raises the own delivery's and opens its seam.
/// @return The order's refusal - the same refusals DeliverPlanNow names
///         today (nothing owed, nothing in the stores, the people's food).
OrderRefusal RequestPlanDelivery(const ProductionConfig& config,
                                 WorldState& current,
                                 ResourceId resource,
                                 Grams amount,
                                 bool own);

/// THE OWN DELIVERY's SETTLEMENT IS THE DISTRICT LOT's (the body found): the
/// outgoing row is a LimitDeliveryRow with `outbound` 1, carted by the same
/// seam, labour job and logistics task as an own-carts lot; field_haul.cpp,
/// SettleDistrictLotHauling, takes the carted share from the stores and counts
/// it to the plan (delivered, delivered_own). The contract's separate
/// SettleOwnPlanDelivery was dropped for that reason - one machinery, both ways.

/// @brief The lot window's horse price: the cart-days a lot of `lot` would
///        take by the village's own carts today (its goods, its store, the
///        district's road), before the order. A read door for the office.
float LotOwnCartDays(const ProductionConfig& config, const WorldState& world, LimitLotId lot);

/// @brief The bonus points of a year (econ's (a)): bonus share × plan_met_points
///        × own share + bonus share × `overfulfil_points` × own share, the own
///        share read off the closing year's book (delivered_own ÷ delivered) -
///        ONE share for the plan's tonnes and the above-plan ones, the book not
///        telling which tonne was above the plan (a named simplification).
///        Rounded as YearLimitPoints rounds.
std::int32_t OwnDeliveryBonusPoints(const ProductionConfig& config,
                                    const WorldState& world,
                                    bool plan_fully_met,
                                    float overfulfil_points);

}  // namespace core

#endif  // CORE_PRODUCTION_DISTRICT_CARTS_H_
