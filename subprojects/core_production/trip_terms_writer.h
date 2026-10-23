/// @file
/// @brief PRODUCTION WRITES THE TRIP's FACTS (#1056 C3; core_common/trip_terms.h
///        for the whole design): under CartTripModel::kPlanned, every seam a
///        cart prices at whole trips writes its task's TaskTripTerms from THE
///        SAME RATE, at the same line - so the groom's plan draws its trips
///        from the price's own facts (econ's ruling 2: the road bed and the
///        heap point), and writes the price from them (trip_pricing.h).
///
/// ONE HOME FOR THE WRITE, CALLED BY EACH PRICER (C3 body 2/6): the contract
/// (0d60a18d) declared a separate evening pass that re-derived each kind's
/// rate - a second home of every rate and of the room-limited grams left
/// (SettleLoad's `left` lives in field_haul.cpp). The pricers already hold
/// both at the line that writes the seam, so they record the facts there:
///   * kFieldHeap, kStandLogs, kSiteDig: SettleLoad (field_haul.cpp), from the
///     heap point, the stand, the pit;
///   * kStoreTransfer: SettleStoreEmptying's rate, from the store emptied;
///   * kDistrictLot: SettleDistrictLotHauling's rate, from the district centre;
///   * kSiteSupply: the works' front leg (supply_haul.cpp), from its sender;
///   * kMilkLeg: PriceMilkLegs, from the barn;
///   * kRickSupply: PriceRickSupply, from the sending rick.
/// A load priced on foot, by the fluid model, or with no grams gets all-zero
/// terms: the plan draws it as before C3 and writes no price.
///
/// @threading SINGLE_THREADED
/// Phase 4: production's settlements (the evening's, and the hour's where a
/// site's front leg changes) and the herd day's milk price.
#ifndef CORE_PRODUCTION_TRIP_TERMS_WRITER_H_
#define CORE_PRODUCTION_TRIP_TERMS_WRITER_H_

#include "core_common/geometry.h"
#include "core_common/haul.h"
#include "core_common/labor_state.h"
#include "core_common/trip_terms.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief The facts of a trip of `rate` from `load_point` for `grams_left`:
///        all zero unless the model is kPlanned and the rate is a cart's
///        whole-trip rate (HaulRate::produce_cart, CartDaysFor's own test).
TaskTripTerms TripTermsOf(const ProductionConfig& config,
                          Vec2 load_point,
                          const HaulRate& rate,
                          Grams grams_left);

/// @brief Writes TripTermsOf on the task of kind `kind` whose load `hauled`
///        names (HaulingWorkOf's form; logistics_rules.h, WorkServesTask). The
///        kind is asked too: one unit may be named by a store's emptying and a
///        works' supply, and the address alone would pick the first.
/// @note No such task yet (logistics makes a load's task at the next hour 0,
///       SyncTasks): nothing is written. A load's FIRST day is therefore drawn
///       as before C3, at the evening's fallback price; its terms are written
///       at its next pricing.
void WriteTripTermsFor(const ProductionConfig& config,
                       WorldState& world,
                       LogisticsLoadKind kind,
                       const WorkAssignment& hauled,
                       Vec2 load_point,
                       const HaulRate& rate,
                       Grams grams_left);

}  // namespace core

#endif  // CORE_PRODUCTION_TRIP_TERMS_WRITER_H_
