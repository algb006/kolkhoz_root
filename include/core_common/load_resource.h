/// @file
/// @brief What resource a logistics load is, read off the world: for the
///        cart's drawn leg, and for the trip of a carrier on foot.
/// @threading PARALLEL_READONLY
/// Pure functions of the world passed in.
///
/// WHY IN core_common (#671, the second delivery; 0.37.240). The groom's plan
/// asked this alone until now (core_logistics/leg_loads.cpp, LegResource). A
/// walker's trip weighs by his load's form, and three modules count that
/// trip: production writes the load's seam, labour drains it and pays by it,
/// logistics cuts the day into trips. One reading of «which resource is this
/// load» for the three.

#ifndef CORE_COMMON_LOAD_RESOURCE_H_
#define CORE_COMMON_LOAD_RESOURCE_H_

#include "core_common/ids.h"
#include "core_common/labor_state.h"
#include "core_common/logistics_state.h"
#include "core_common/world_state.h"

namespace core {

/// @brief The resource of most grams in `amounts`; invalid when all are
///        nought. What a load of many resources is named by.
ResourceId MostGramsOf(const ResourceAmounts& amounts);

/// @brief The resource of `task`'s load as a CART carries it: a field heap's
///        reaped resource, a stand's logs (`log`), a dig's resource; for the
///        loads of many resources - a district lot, a store being emptied, a
///        works' front supply leg - the one of most grams.
/// @return Invalid when the load's subject is gone or holds nothing.
ResourceId TaskLoadResource(const WorldState& world, const LogisticsTaskRow& task, ResourceId log);

/// @brief The resource of `task`'s load as a CARRIER ON FOOT takes it - ONE
///        RESOURCE A TRIP (transport design §2, «Как носят»): as
///        TaskLoadResource, except a works' supply, where it is the FIRST
///        resource by id the front leg still holds - the one production's
///        settle moves first (supply_haul.cpp), not the one of most grams.
ResourceId WalkerLoadResource(const WorldState& world,
                              const LogisticsTaskRow& task,
                              ResourceId log);

/// @brief WalkerLoadResource of the load `work` hauls, read off the work's
///        OWN subject (its field, stand, pit, lot or unit) - not off a task
///        row, which the morning's pricing cannot count on existing. A unit
///        being emptied is a store transfer; any other hauling on a unit is
///        its works' supply (as WorkSeamOf reads it).
/// @return Invalid when `work` is no hauling or its subject is gone.
ResourceId WalkerLoadResourceOfWork(const WorldState& world,
                                    const WorkAssignment& work,
                                    ResourceId log);

}  // namespace core

#endif  // CORE_COMMON_LOAD_RESOURCE_H_
