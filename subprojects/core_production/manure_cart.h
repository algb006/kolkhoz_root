/// @file
/// @brief Manure carted heap -> field on sledges, the production half (#1056
///        piece C2; core_common/manure_state.h holds the rule and the state).
/// @threading SINGLE_THREADED
/// All calls run in the production decisions sub-step (phase 3) on the sim
/// thread: the booking at the year's turn (in PlanManure's place), the price
/// at the day's last tick, the settle every hour.
///
/// CONTRACT: declarations only. The body follows boss's word on the
/// fertility mark (core's seq 133) and C2-0's print.

#ifndef CORE_PRODUCTION_MANURE_CART_H_
#define CORE_PRODUCTION_MANURE_CART_H_

#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief THE YEAR'S DOSES BOOKED, in PlanManure's place and at its two
///        calls (tick 1, the year's turn): among the arable idle fields of the
///        rotation not manured this cycle, those below
///        FarmingConfig::manure_fertility_mark, nearest by road from the heap
///        first (ties poorest first, then by row), each gets its norm dose
///        (`manure_norm_kg_per_ha` x area) into its ManureDoseSeam while the
///        heap's unbooked manure lasts; a partial dose is a dose. NO grams
///        move.
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
///        before the furrow reads manure_applied): the `due` is cancelled - it
///        stays in the heap, the seam is cleared, and YearLedger::manure_late
///        counts it.
void CancelLateManure(WorldState& current, FieldRow& field);

}  // namespace core

#endif  // CORE_PRODUCTION_MANURE_CART_H_
