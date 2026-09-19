/// @file
/// @brief What a unit being taken down held, on its way to the stores
/// (unit_state.h, kDemolishing; boss, host-econ-shops seq 22-23).
/// @threading SINGLE_THREADED
/// Runs from the production sub-step of the decisions slot (phase 3) on the
/// sim thread, at the day's last tick beside the other settlements that put
/// goods through the stores' door (stock_ops.h, DeliverToStores) — a
/// sequential slot, since it writes the shared stores.
///
/// ONE DOOR. Until 0.34.9 construction moved a demolished unit's stock out at
/// the order through a door of its own: into the first unit with room by
/// mass, whatever that unit kept, and what did not fit was booked lost that
/// instant — 40 t of firewood and 90 t of logs of the manor ruins could go in
/// one click with the stores full, which the player could not foresee
/// (design principle «не наказывать за непредвидимое»). The stock now stays
/// on the site and goes through the stores' door, into its home, by its room;
/// what has no room waits there and kDemolitionStockWaiting says so.

#ifndef CORE_PRODUCTION_DEMOLITION_STOCK_H_
#define CORE_PRODUCTION_DEMOLITION_STOCK_H_

#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief Puts what every kDemolishing site holds through the stores' door,
/// resource by resource, as room allows; what does not fit stays on the site.
/// Books nothing lost.
/// @pre The day's last tick, sequential slot.
void SettleDemolitionStock(const ProductionConfig& config, WorldState& current);

/// @brief One kDemolitionStockWaiting per kDemolishing site still holding
/// stock (alarm_state.h).
void CollectDemolitionAlarms(const WorldState& world, std::vector<Alarm>& alarms);

}  // namespace core

#endif  // CORE_PRODUCTION_DEMOLITION_STOCK_H_
