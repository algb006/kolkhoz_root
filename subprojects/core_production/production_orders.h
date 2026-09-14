/// @file
/// @brief The production half of the order book: pause and resume, the
/// sealed funds, the rotation chain, and the dispatch to the fellings, the
/// digging, the limit and the field removal (task A8 and after).
/// @threading SINGLE_THREADED
/// Runs at the top of the production sub-step of the decisions slot (phase 3)
/// on the sim thread, and settles every order it owns in the step it is read.
///
/// WHY ITS OWN FILE. production_system.cpp had reached 1362 lines against
/// the 1000 limit (boss, parcel 332); the order verbs moved out whole,
/// unchanged.

#ifndef CORE_PRODUCTION_PRODUCTION_ORDERS_H_
#define CORE_PRODUCTION_PRODUCTION_ORDERS_H_

#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief Every pending order of a production kind is carried out or
/// refused, in the order book's order; other kinds are left alone.
void ConsumeProductionOrders(const ProductionConfig& config, WorldState& current);

}  // namespace core

#endif  // CORE_PRODUCTION_PRODUCTION_ORDERS_H_
