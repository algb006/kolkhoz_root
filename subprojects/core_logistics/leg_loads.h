// What a leg of the groom's plan carries, and the porters' view of the plan
// (#671; core_common/porter_leg.h). Split out of logistics_plan.cpp when the
// porter took that file past the thousand-line limit: the plan's clock
// decides WHERE and WHEN a cart or a walker goes; this file answers WHAT it
// carries there - the leg's resource and form, a walker's trip grams - and
// gives the layer the walkers on their legs.
// @threading SINGLE_THREADED for FillTripLoads (the decisions phase, with
// the plan's builder); PARALLEL_READONLY for the rest - pure reads.

#ifndef CORE_LOGISTICS_LEG_LOADS_H_
#define CORE_LOGISTICS_LEG_LOADS_H_

#include <vector>

#include "core_common/haul.h"
#include "core_common/ids.h"
#include "core_common/logistics_state.h"
#include "core_common/porter_leg.h"
#include "core_common/world_state.h"
#include "logistics_config.h"

namespace core {

/// @brief What a leg's task carries: a field's heap its reaped resource, a
///        stand `log`, a pit its dug resource, a works' supply (its front
///        leg), a district lot or a store being emptied the resource of MOST
///        grams. Invalid for a ride and for a task that is gone.
ResourceId LegResource(const WorldState& world, const CartLeg& leg, ResourceId log);

/// @brief Writes each leg's trip load (CartLeg::resource, CartLeg::carried):
///        the resource of every leg with a task that has none yet - a leg the
///        earlier plan had begun keeps what it was written with - and on a
///        carrier on foot's LOADED leg his trip's grams (WalkerTripGrams of
///        the resource's form, alone or in his pair). Called at the end of the
///        plan's build, the morning's and every re-plan's.
void FillTripLoads(const LogisticsConfig& config, const WorldState& world, GroomPlan& plan);

/// @brief Every carrier on foot of `completed`'s plan on the leg he walks at
///        its tick, appended to `out` in the plan's cart order
///        (ILogisticsSystem::CollectPorterLegs). A leg is his while
///        depart <= tick < arrive; a leg of no length is never reported (a
///        load the chain kept in its place, KeepInTheChain). Nothing for a
///        plan of another day.
void CollectPorterLegs(const LogisticsConfig& config,
                       const WorldState& completed,
                       std::vector<PorterLeg>& out);

}  // namespace core

#endif  // CORE_LOGISTICS_LEG_LOADS_H_
