/// @file
/// @brief Manure carted heap -> field on sledges, the production half (#1056
///        piece C2; core_common/manure_state.h holds the rule and the state).
/// @threading SINGLE_THREADED
/// All calls run in the production decisions sub-step (phase 3) on the sim
/// thread: the booking (tick 1, the year's turn and every sledge day's last
/// tick), the price at the day's last tick, the settle every hour.

#ifndef CORE_PRODUCTION_MANURE_CART_H_
#define CORE_PRODUCTION_MANURE_CART_H_

#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief THE DOSES BOOKED, in PlanManure's place (tick 1, the year's turn)
///        and on every sledge day from the first snow after the harvest
///        (boss, the #1056 thread [140]): among the arable idle fields of the
///        rotation not manured this cycle, and those whose standing booking
///        is short of the norm, the fields below
///        FarmingConfig::manure_fertility_mark nearest by road from the heap
///        first (ties poorest first, then by row) are booked up to their norm
///        dose (`manure_norm_kg_per_ha` x area) while the heap's unbooked
///        manure lasts, and only then the rest by the same order; a partial
///        dose is a dose. A standing booking stands until its field's
///        ploughing. NO grams move.
/// @note The heap's unbooked manure is its stock less every standing
///       booking's `due`.
void BookManureDoses(const ProductionConfig& config, WorldState& current);

/// @brief THE FIELDS' DOSE PRICE, at the day's last tick for the next
///        morning's placement: every seam's carting is cleared, then on a
///        sledge day (SledgeDay) the standing doses, nearest by road first,
///        are priced at the sledge's gear as far as the team's oats over the
///        ladder less the winter's hold buy their cart-days (C2.2): whole,
///        in part (`priced` the grams bought), or not at all - and
///        TractionWatch carries the two oat numbers and the wait. Not a
///        sledge day: no field is priced.
void PriceManureDoses(const ProductionConfig& config, WorldState& current);

/// @brief THE WINTER'S HOLD (C2.2, «Сани по пайку»; manure_state.h), grams of
///        the team's work feed: the winter's other carting's horse-days a day
///        - the closed book's snow-day horse-backed days less its manure
///        cart-days, by its snow days; in year 1 every adult horse - times
///        the days from tomorrow to the first ploughing day (the earliest
///        spring crop's sowing window), at a horse-day's full work oats.
/// @note A reading, not a holder: nothing is moved or sealed. Every day to
///       the plough counts at the snow days' rate, snow or not.
Grams WinterHoldGrams(const ProductionConfig& config, const WorldState& world);

/// @brief THE FIELDS' DOSE SETTLE, hourly and at the last tick (before the
///        price): the share of a seam's carting drained since the last settle
///        moves that share of its `due` heap -> field, raising
///        FieldRow::manure_applied by what arrived and booking it in
///        YearLedger::manure_carted.
void SettleManureDoses(const ProductionConfig& config, WorldState& current);

/// @brief A FIELD WHOSE PLOUGHING OPENS WITH ITS DOSE NOT ALL DELIVERED
///        (called where its phase leaves kIdle, field_work.cpp OpenPlowing,
///        before the furrow reads manure_applied): the `due` is cancelled - it
///        stays in the heap, the seam is cleared, and YearLedger::manure_late
///        counts it.
void CancelLateManure(WorldState& current, FieldRow& field);

}  // namespace core

#endif  // CORE_PRODUCTION_MANURE_CART_H_
