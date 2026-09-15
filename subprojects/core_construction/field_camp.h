/// @file
/// @brief Where a field camp may be marked — the district MTS's column camps
/// there (MTS design §1, "Полевой стан — юнит"; boss, parcel 449).
/// @threading SINGLE_THREADED
/// Called only from inside the construction sub-step of the decisions slot
/// (phase 3), on the sim thread, while a kBuildUnit is read.
///
/// THE RULE THE CORE CAN HOLD is two of the design's four: the camp stands
/// "не на пашне" and "близко к полям" — no arable field's contour takes in
/// its centre, and some field lies within the reach of it (1 km, STUB). The
/// other two, a slope of 5 % at most and not on the floodplain, are the
/// layer's: the core keeps no relief and no floodplain, as it keeps no water
/// edge for the beach (boss, parcels 447, 449).

#ifndef CORE_CONSTRUCTION_FIELD_CAMP_H_
#define CORE_CONSTRUCTION_FIELD_CAMP_H_

#include "construction_config.h"
#include "core_common/geometry.h"
#include "core_common/order_state.h"
#include "core_common/world_state.h"

namespace core {

/// @brief Whether a field camp may be marked at `position`.
/// @return kNone; kOnArable when an arable field's contour takes in the
///         position; kTooFarFromFields when no field lies within the reach.
OrderRefusal FieldCampPlacementRefusal(const ConstructionConfig& config,
                                       const WorldState& world,
                                       Vec2 position);

}  // namespace core

#endif  // CORE_CONSTRUCTION_FIELD_CAMP_H_
