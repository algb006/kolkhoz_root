/// @file
/// @brief THE DRAWN TRIP IS THE PRICE (#1056 C3; econ logistics-all-on §C3 and
///        «C3's four design rulings», 8607d0d; boss, core-boss-oats-b-pair
///        [33]-[35]): the facts a cart's trip of one load is drawn from, and the
///        whole-trip price of what the day's plan did not reach.
///
/// WHY. Until C3 the core had TWO DOORS to one number: production priced
/// every cart's seam by the gram (CartDaysFor, sixteen call sites) and the
/// labour hour drained it, while the groom's plan cut its trips out of that
/// seam's span (logistics_state.h, CartLeg: «the trips are the plan's drawing,
/// not the carting»). So the trips the layer draws were not the trips anyone
/// paid, and the plan measured a field's leg from the field's centre with no
/// road bed while the price measured it from the heap with the bed.
///
/// AFTER C3 (CartTripModel::kPlanned, transport.csv `cart_loaded`
/// `trip_model` 3):
///   * PRODUCTION WRITES THE FACTS, every evening, on each open task
///     (TaskTripTerms below; core_production/trip_terms_writer.h): where the
///     load lies, the round trip and its loaded half at the rate's bed and
///     gear, a cart's load of the resource, and the grams left to cart - the
///     same rate its seam was priced by, so the plan takes the price's facts
///     (ruling 2).
///   * THE PLAN WRITES THE PRICE, at the morning and every re-plan
///     (core_logistics/trip_pricing.h): each task's seam = the hours of the
///     whole trips the plan drew for it today + the FALLBACK for the grams it
///     did not reach (FallbackCartDays: whole trips at the same round trip),
///     ÷ the norm day's hours (ruling 1).
///   * THE LABOUR HOUR DRAINS AS BEFORE, and a task CARRIED ALONG another's
///     trips (a second field's leftover pooled into one load, a back load)
///     drains with its carrier by its share (work_seam.h,
///     CoDrainCarriedTasks; rulings 3-4).
/// Production's conversion of a drained seam into grams is unchanged.
///
/// @threading SINGLE_THREADED
/// Data and pure arithmetic: written by production's evening settlement
/// (phase 4) and read by the groom's plan (phase 3) and the labour hour.
#ifndef CORE_COMMON_TRIP_TERMS_H_
#define CORE_COMMON_TRIP_TERMS_H_

#include <cstdint>

#include "core_common/geometry.h"
#include "core_common/quantities.h"

namespace core {

/// @brief One load's trip, as its price sees it (C3; save 177 or the next
///        number free when C3 lands): on LogisticsTaskRow, written by
///        production's evening (WriteTaskTripTerms), read by the plan. All zero
///        means «not priced by a cart»: a load carried on foot, or no carts in
///        the settlement - the plan then draws it as before C3 and no price is
///        written by the plan.
struct TaskTripTerms {
  /// Where the load lies, the trip's start: a field's HEAP POINT (production's
  /// FieldHeapPoint, not the field's centre - ruling 2), the stand, the site's
  /// pit, the store emptied, the district centre, the heap of a dose.
  Vec2 load_point;

  /// One whole round trip, game hours: the loaded half, the way back empty,
  /// and both loading pauses (haul.h, RateOverKm), at the evening's road bed
  /// and gear (cart or sledge). The plan's trip halves are cut from it.
  float trip_hours = 0.0F;

  /// Of `trip_hours`, the loaded half with its two pauses.
  float trip_loaded_hours = 0.0F;

  /// A cart's load of this load's resource, grams (CartLoadGrams by its form).
  Grams cart_load = 0;

  /// What lies to be carted tonight, grams, limited by the receivers' room as
  /// the seam's own price was (SettleLoad's `left`).
  Grams grams_left = 0;
};

/// @brief Whole trips for `grams` at `cart_load` a trip: the ceiling.
/// @return 0 for no grams or no load.
std::uint32_t WholeTripsFor(Grams grams, Grams cart_load);

/// @brief THE FALLBACK (ruling 1): cart-days of `grams` the day's plan did not
///        reach, at whole trips of `terms.trip_hours`, over `standard_day_hours`
///        - transport's trip_model 1 arithmetic, one home for production's
///        evening and the plan.
/// @return 0 when `terms` carry no trip (all zero) or no grams.
float FallbackCartDays(const TaskTripTerms& terms, Grams grams, float standard_day_hours);

}  // namespace core

#endif  // CORE_COMMON_TRIP_TERMS_H_
