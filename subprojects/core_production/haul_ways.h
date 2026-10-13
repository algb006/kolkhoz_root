/// @file
/// @brief The rate of a cart's way between two places and the year's book of
/// a produce cart's run — the two pieces every load's settlement shares
/// (field_haul.cpp, supply_haul.cpp). Private to core_production.
/// @threading SINGLE_THREADED
/// Called from the production sub-steps of the sequential slots only.

#ifndef CORE_PRODUCTION_HAUL_WAYS_H_
#define CORE_PRODUCTION_HAUL_WAYS_H_

#include "core_common/geometry.h"
#include "core_common/haul.h"
#include "core_common/ledger_state.h"
#include "core_common/quantities.h"
#include "core_common/road_route.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief The rate of carting from `from` to `destination` today: by the
///        road, the bed's condition and the carrier — a cart when a draught
///        horse is free, a carrier on foot when none is.
/// @param cart_mode How the load goes when a horse is free: a cart with
///        produce keeps to the roads (TravelMode::kCart), a cart with logs may
///        cross the felling's ground (kLogCart; roads design §11).
/// @param carried What a carrier on foot would carry there (#671): his trip
///        is the bundle for a rick, the sack for the rest (haul.h,
///        WalkerLoadGrams). Read only when no draught horse is free; invalid
///        is a sack.
/// @return The rate; for the produce cart with the way's metres off the road
///         at both ends, for the year's book.
HaulRate RateToward(const ProductionConfig& config,
                    const WorldState& world,
                    Vec2 from,
                    Vec2 destination,
                    TravelMode cart_mode = TravelMode::kCart,
                    ResourceId carried = ResourceId{});

/// @brief Books what a produce cart brought in today on the year's cart
///        columns (YearLedger::cart_trips and its siblings): the trips are
///        the grams over the cart's load, each one loaded way off the road
///        by `rate`. Nothing for a carrier on foot or a log cart.
/// @note Side effect: the current year's ledger.
void BookCartRun(const ProductionConfig& config,
                 WorldState& current,
                 CartLoadSource source,
                 const HaulRate& rate,
                 Grams moved);

}  // namespace core

#endif  // CORE_PRODUCTION_HAUL_WAYS_H_
