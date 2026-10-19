/// @file
/// @brief Manure carted, the production half (#1056 piece C2; core_common/
///        manure_state.h holds the rule and the state).
/// @threading SINGLE_THREADED
/// All calls run in the production decisions sub-step (phase 3) on the sim
/// thread: the herd day lays the yards' manure, the prices at the day's last
/// tick, the settles every hour, the booking at the year's turn.
///
/// STUB (C2; econ §C2.1, 5f7f1a6; transport design §12, stage C's row):
/// DECLARATIONS WITH NO BODY, AND NOTHING CALLS THEM - manure moves as on main
/// (herd_system.cpp into the heap; PlanManure at the year's turn). Do not
/// define them on their own: the return conditions are core_common/
/// manure_state.h's, and the body is priced in game hours again before it is
/// written.

#ifndef CORE_PRODUCTION_MANURE_CART_H_
#define CORE_PRODUCTION_MANURE_CART_H_

#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief The herd day's manure into its YARD's stock under
///        FarmingConfig::manure_carted, up to the yard's buffer
///        (livestock.csv `manure_yard_buffer_days` of its own herds' daily
///        manure); the excess is lost into YearLedger::manure_lost. Without
///        the switch: the heap, as before C2 (herd_system.cpp).
/// @return The grams laid in the yard (the excess not counted).
Grams LayManureInYard(const ProductionConfig& config,
                      WorldState& current,
                      std::uint32_t yard_row,
                      Grams manure);

/// @brief THE YARDS' REMOVAL PRICE, at the day's last tick for tomorrow's
///        placement: every yard holding manure gets its ManureOutSeam to the
///        heap - on foot where the walk holds the day's trips, else by cart.
void PriceManureOut(const ProductionConfig& config, WorldState& current);

/// @brief THE YARDS' REMOVAL SETTLE, hourly and at the last tick (before the
///        price): the share drained moves yard -> heap.
void SettleManureOut(const ProductionConfig& config, WorldState& current);

/// @brief THE YEAR'S DOSES BOOKED (PlanManure's own choice and order, farming
///        §9): each chosen field's ManureDoseSeam gets the heap, its dose and
///        its plough slot's month; NO grams move.
void BookManureDoses(const ProductionConfig& config, WorldState& current);

/// @brief THE FIELDS' DOSE PRICE, at the day's last tick: every field with a
///        dose due gets tomorrow's cart-days from the heap.
void PriceManureDoses(const ProductionConfig& config, WorldState& current);

/// @brief THE FIELDS' DOSE SETTLE, hourly and at the last tick: the share
///        drained moves heap -> field, raising FieldRow::manure_applied by
///        what arrived.
void SettleManureDoses(const ProductionConfig& config, WorldState& current);

/// @brief A FIELD WHOSE PLOUGHING OPENS WITH ITS DOSE NOT ALL DELIVERED
///        (called where its phase leaves kIdle, field_work.cpp OpenPlowing):
///        the rest is cancelled - it stays in the heap, the booking is undone,
///        and YearLedger::manure_late counts it.
void CancelLateManure(WorldState& current, FieldRow& field);

}  // namespace core

#endif  // CORE_PRODUCTION_MANURE_CART_H_
