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
///        sledge day (SledgeDay) each field with a dose due gets its
///        cart-days from the heap at the sledge's gear and the manure form's
///        load share. Not a sledge day: no field is priced.
void PriceManureDoses(const ProductionConfig& config, WorldState& current);

/// @brief THE FIELDS' DOSE SETTLE, hourly and at the last tick (before the
///        price): the share of a seam's carting drained since the last settle
///        moves that share of its `due` heap -> field, raising
///        FieldRow::manure_applied by what arrived and booking it in
///        YearLedger::manure_carted.
void SettleManureDoses(const ProductionConfig& config, WorldState& current);

/// @brief A FIELD WHOSE PLOUGHING OPENS WITH ITS DOSE NOT ALL DELIVERED
///        (called where its phase leaves kIdle, field_work.cpp OpenPlowing,
///        before the furrow reads manure_applied): the seam is cleared and
///        YearLedger::manure_late counts the `due` the carts did not take.
///        With FarmingConfig::manure_late_spread_free (STUB 1; econ's way
///        (2)) that `due` goes onto the field free from the heap, as much as
///        the heap holds, and raises manure_applied; with 0 it stays in the
///        heap, as C2 option 2 first had it.
void CancelLateManure(const ProductionConfig& config, WorldState& current, FieldRow& field);

}  // namespace core

#endif  // CORE_PRODUCTION_MANURE_CART_H_
