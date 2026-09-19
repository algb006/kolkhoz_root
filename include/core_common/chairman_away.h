/// @file
/// @brief Whether the chairman is away in the district, and the one door
/// that turns the village's orders away while he is (district-trip.md §1;
/// boss seq 206; ChairmanState::away_*).
/// @threading SINGLE_THREADED
/// Read by the decisions slot (phase 3) on the sim thread: the gate runs
/// first in it, before any consumer takes an order.

#ifndef CORE_COMMON_CHAIRMAN_AWAY_H_
#define CORE_COMMON_CHAIRMAN_AWAY_H_

#include "core_common/order_state.h"
#include "core_common/world_state.h"

namespace core {

/// @brief True from the tick he leaves to the tick he is back.
bool ChairmanAway(const WorldState& world);

/// @brief Whether an order of `kind` is one of the district's own doors,
/// accepted while he is away (boss seq 206: the plan's bargain, the limit's
/// lots). Every other kind is an order to the village.
bool DistrictDoor(OrderKind kind);

/// @brief Refuses every pending order to the village with kChairmanAway
/// while he is away. Touches nothing when he is at home.
/// @pre The decisions slot, before any consumer.
void RefuseVillageOrdersWhileAway(WorldState& current);

}  // namespace core

#endif  // CORE_COMMON_CHAIRMAN_AWAY_H_
