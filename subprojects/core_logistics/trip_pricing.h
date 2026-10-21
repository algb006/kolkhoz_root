/// @file
/// @brief THE PLAN WRITES THE PRICE (#1056 C3; core_common/trip_terms.h for the
///        whole design): the groom's plan, under CartTripModel::kPlanned
///        (LogisticsConfig::cart_trip_model 3), draws WHOLE trips from each
///        task's TaskTripTerms, pools two fields' leftovers into one load,
///        takes back loads, and rewrites every cart-priced task's seam from
///        what it drew. Private to core_logistics.
///
/// THE ONE DOOR (econ's ruling 1): a task's seam, written here at the morning's
/// plan and every re-plan, is
///   (the round-trip hours of the whole trips drawn for it today
///    + its share of a pooled or back leg it rides
///    + FallbackCartDays's whole trips for the grams the plan did not reach)
///   ÷ LogisticsConfig::standard_day_hours,
/// written to both the seam's remaining and its written value (work_seam.h,
/// WorkSeamOf). Production's evening writes the same seam at whole trips
/// (the fallback alone) and the facts; nothing else writes a cart's price.
/// The book (YearLedger, C3's columns) is written here too: the day's drawn and
/// fallback cart-days (C3-g), pooled and back-load trips (C3-h).
///
/// THE DEVIATION FROM THE CONTRACT PAGE, NAMED: econ's §C3 put «its own task
/// and cargo form on the leg (+2)» for a back load. A back load here is an
/// ordinary loaded leg of the other task on this cart's chain, so the leg
/// needs no field for it; its form is LegCargoForm's of that task. The +2 are
/// CartLeg::pooled_task (the pool's second load on one leg) and the carried
/// pair on the task row (LogisticsTaskRow::carried_by, carried_share), which
/// the labour hour needs to drain a carried load at all
/// (work_seam.h, CoDrainCarriedTasks).
///
/// @threading SINGLE_THREADED
/// Phase 3 (decisions and assignment), inside the groom's plan, after
/// BuildGroomPlan has drawn the day's legs.
#ifndef CORE_LOGISTICS_TRIP_PRICING_H_
#define CORE_LOGISTICS_TRIP_PRICING_H_

#include <cstdint>

#include "core_common/geometry.h"
#include "core_common/quantities.h"
#include "core_common/world_state.h"
#include "logistics_config.h"

namespace core {

/// @brief True when the plan writes the price today: the model is 3 and the
///        settlement has carts (a village with no horse carts on foot, as
///        before C3, and nobody writes a cart's price).
bool PlanPricesCarts(const LogisticsConfig& config, const WorldState& world);

/// @brief A FIELD HEAP's POOL PARTNER (ruling 3): another open kFieldHeap
///        task whose leftover, with `leftover` of `task_row`'s, fits ONE cart
///        load of the resource (TaskTripTerms::cart_load; the same resource),
///        and whose heap lies so that first heap -> its heap -> the first's
///        receiver is at most (1 + LogisticsConfig::pool_detour_share) times
///        first heap -> receiver, by RoadKm. The nearest such by the detour,
///        then by row. Not a task already carried by another today.
/// @param task_row The first field's task row (its last part-load).
/// @param leftover That load's grams, below its cart_load.
/// @param receiver The first load's receiver (WorkReceiverOf).
/// @return The partner's row, or kNoRow (also when pool_detour_share is 0).
std::uint32_t PoolPartnerOf(const LogisticsConfig& config,
                            const WorldState& world,
                            std::uint32_t task_row,
                            Grams leftover,
                            Vec2 receiver);

/// @brief A BACK LOAD at a receiver (ruling 4; econ's C3.1 rule as written):
///        an open task with carting left whose load lies near `at` and whose
///        receiver lies near `next_start`, taken when the empty way `at` ->
///        its load plus the empty way its receiver -> `next_start` is SHORTER
///        than the way back it replaces, `at` -> `next_start` empty, plus that
///        task's own trip's empty half (both by RoadKm at the cart's empty
///        pace). The shortest saving first, then by row. Not a load carried on
///        foot, not a task already carried today.
/// @param at The receiver the cart stands at, its load delivered.
/// @param next_start Where its next trip starts (its own load again, or the
///        next load of its chain).
/// @return The back load's task row, or kNoRow.
std::uint32_t BackLoadOf(const LogisticsConfig& config,
                         const WorldState& world,
                         std::uint32_t carrier_row,
                         Vec2 at,
                         Vec2 next_start);

/// @brief THE ONE DOOR: rewrites every cart-priced open task's seam from
///        today's drawn plan (world.groom_plan) and its TaskTripTerms, writes
///        the carried pairs (LogisticsTaskRow::carried_by / carried_share) and
///        books the day's columns. Called after BuildGroomPlan and after every
///        re-plan; idempotent for one plan (a re-plan replaces the day's
///        figures, it does not add to them).
/// @pre PlanPricesCarts(config, world); otherwise it does nothing.
/// @post For every such task, its seam's remaining equals its written value
///       equals (drawn hours + carried hours + fallback hours) ÷
///       standard_day_hours (C3-d's identity, checked by the instrument).
void PriceTasksFromPlan(const LogisticsConfig& config, WorldState& world);

}  // namespace core

#endif  // CORE_LOGISTICS_TRIP_PRICING_H_
