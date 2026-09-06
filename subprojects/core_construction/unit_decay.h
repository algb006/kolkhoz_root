/// @file
/// @brief What happens to a unit WITHOUT an order: wear, collapse terms and
/// the stink zone.
/// @threading SINGLE_THREADED
/// The queries are called BETWEEN steps off the completed buffer;
/// MoveStinkZones is called once a day from inside the construction
/// sub-step of the decisions slot (phase 3). Never from a worker thread.
///
/// THE SEAM IS "DOES IT NEED AN ORDER" (boss, 2026-09-06). A building is
/// raised, upgraded, repaired and pulled down only because the chairman said
/// so — that half stays in construction_system.cpp and every one of its
/// entry points begins with an OrderRow. Nothing here waits for anybody: a
/// roof rots, a hut falls in and a tannery goes on smelling whether or not
/// an order was ever given. The two halves answer different questions and
/// the file that held both had passed the project's hard limit of 1000
/// lines — but the limit only said one was due; the meaning says where.
///
/// Everything here takes the parsed configuration by const reference and
/// owns no state. Only MoveStinkZones takes a mutable world, and it writes
/// exactly one field of it.

#ifndef CORE_CONSTRUCTION_UNIT_DECAY_H_
#define CORE_CONSTRUCTION_UNIT_DECAY_H_

#include <cstdint>

#include "construction_config.h"
#include "core_common/deadline.h"
#include "core_common/stink.h"
#include "core_common/world_state.h"

namespace core {

/// @brief Is this the start's old house — the one type that falls from wear?
bool TypeIsOldHouse(const ConstructionConfig& config, UnitTypeId type);

/// @brief How much faster than its class's term this unit wears: the type's
///        own pace times the step's. 1.0 = the class's term unchanged.
float WearPace(const BuildType& type, std::uint8_t level);

/// @brief Years from new to a full scale at this level; 0 when the ladder
///        names none. `in_use` picks the shorter of the two terms.
float WearYears(const BuildType& type, std::uint8_t level, bool in_use);

/// @brief Is anybody living or working in this unit today? A building crew
///        does not count — a site is worked ON, not worked IN.
bool InUse(const WorldState& world, std::uint32_t row);

/// @brief Hours of road, one way, from the nearest dwelling to `place`;
///        0 when the settlement has no house at all.
float NearestDwellingHours(const ConstructionConfig& config,
                           const WorldState& completed,
                           const Vec2& place);

/// @brief The full stink radius of a source of this type standing at this
///        level, in metres — the ladder narrows it rung by rung.
float FullRadiusFor(const ConstructionConfig& config, const BuildType& type, std::uint8_t level);

/// @brief How long this unit has before its wear reaches the scale's end.
Deadline WearDeadline(const ConstructionConfig& config, const WorldState& completed, UnitId unit);

/// @brief The worst band reaching `point` when every source is at its FULL
///        zone — what a placement preview is judged against.
StinkStrength StinkFullAt(const ConstructionConfig& config,
                          const WorldState& completed,
                          Vec2 point);

/// @brief The worst band reaching `point` TODAY, from the zones as they have
///        actually grown — what a nose meets.
StinkStrength StinkNowAt(const ConstructionConfig& config, const WorldState& completed, Vec2 point);

/// @brief Moves every source's zone one day towards its full radius, or away
///        from it while the source is idle. Writes UnitRow::stink_radius_m
///        and nothing else.
void MoveStinkZones(const ConstructionConfig& config, WorldState& current);

}  // namespace core

#endif  // CORE_CONSTRUCTION_UNIT_DECAY_H_
