/// @file
/// @brief Laying a road or path the player drew (kLayRoad; roads design §9,
///        «Грунтовка ничего не стоит»; delivery 7c): the draft traced again on
///        the step's world by the same tracer the preview used, and a path or
///        a dirt road written into the road table at once.
/// @threading SINGLE_THREADED
/// Called from ConstructionSystem::ConsumeOrders, the sequential decisions
/// slot of phase 3; writes WorldState::roads and road_index and appends one
/// event.
///
/// THE EVENT COMES AFTER THE WRITE (ue's condition, road_draft.h): kRoadLaid
/// is appended once the row and the index are written, in the same tick, so
/// Roads() answers it by the time the layer reads it.
///
/// WHAT IT DOES NOT DO YET, and who will:
///   * gravel, asphalt and asphalt with walks become road work (7e); until
///     then their laying is refused kNoConsumer — STUB, named;
///   * a draft that traced clean in the preview and not on the step's world
///     (a unit raised, a road laid in between) is refused kRuleForbids, and
///     PreviewRoad on the same points says which block; the refusal itself
///     carries no RoadDraftRefusal.

#ifndef CORE_CONSTRUCTION_ROAD_LAYING_H_
#define CORE_CONSTRUCTION_ROAD_LAYING_H_

#include "core_common/ids.h"
#include "core_common/order_state.h"
#include "core_construction/construction_system.h"

namespace core {

struct WorldState;

/// @brief Lays the road `order` draws, when it traces clean.
/// @param trace The world's tracer; empty in a world assembled without one
///        (unit tests of this module), and then every laying is refused
///        kNoConsumer.
/// @param order_id The order's id, for the event.
/// @param order Its `road` is set to the road laid, so the order's own event
///        names it (world.cpp's sweep copies it).
/// @return kNone when laid; kNoConsumer (no tracer, or a surface that is road
///         work, 7e); kRuleForbids (the trace was refused).
OrderRefusal LayRoad(const RoadTracer& trace,
                     WorldState& current,
                     OrderId order_id,
                     OrderRow& order);

}  // namespace core

#endif  // CORE_CONSTRUCTION_ROAD_LAYING_H_
