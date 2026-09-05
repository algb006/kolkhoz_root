/// @file
/// @brief The seam a work assignment drains, and the phase-to-kind rule.
/// @threading PARALLEL_READONLY
/// Pure reads of a world plus one assignment. The mutable overload hands
/// back a pointer into the state being written and is for the sequential
/// labour sub-step alone; the const one is for anybody asking what a man is
/// doing, including the boundary between steps.
///
/// WHY IT IS SHARED AND NOT A PRIVATE HELPER OF THE LABOUR MODULE. The seam
/// answers two questions with one body: "how much is there left to drain"
/// (labour) and "is there anything to work with at all" — which is exactly
/// the difference between kIdle and kBlocked, the one part of an idleness
/// signal the player can act on (resident_activity.h). A second copy of
/// these seven cases would drift the day somebody adds a work kind, and the
/// two answers would disagree about the same man in the same hour.

#ifndef CORE_COMMON_WORK_SEAM_H_
#define CORE_COMMON_WORK_SEAM_H_

#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/labor_state.h"
#include "core_common/land_state.h"
#include "core_common/world_state.h"

namespace core {

/// @brief The work a field in this phase is waiting for; kNone when the
/// field is growing, idle or fallow.
constexpr WorkKind KindOfPhase(FieldPhase phase) {
  switch (phase) {
    case FieldPhase::kPlowing:
      return WorkKind::kPlowing;
    case FieldPhase::kHarrowing:
      return WorkKind::kHarrowing;
    case FieldPhase::kSowing:
      return WorkKind::kSowing;
    case FieldPhase::kHarvest:
      return WorkKind::kHarvest;
    case FieldPhase::kIdle:
    // Not a phase, and it waits for no work: handled beside the
    // phases that wait for none, so this switch can stay without a
    // default and keep a new phase a compile error.
    case FieldPhase::kFieldPhaseCount:
    case FieldPhase::kGrowing:
      return WorkKind::kNone;
  }
  return WorkKind::kNone;
}

/// @brief The seam this assignment drains, or nullptr when its target is
///        gone or has moved on to work of another kind.
/// @note nullptr is not "nothing left to do": it is "there is no longer
///       anything here to do it TO". A seam that exists and reads zero is
///       the other half of the same story, and the two are told apart by
///       the caller, not here.
const float* WorkSeamOf(const WorldState& world, const WorkAssignment& work);

/// @brief The same seam, writable. Sequential callers only — the labour
///        sub-step drains it hourly in row order.
float* WorkSeamOf(WorldState& world, const WorkAssignment& work);

/// @brief Where the work of this assignment is done: the field's centre, the
///        site, or the unit the herd stands at.
/// @return false when the target is gone — the same case WorkSeamOf answers
///         with nullptr, and for the same reason.
bool WorkPlaceOf(const WorldState& world, const WorkAssignment& work, Vec2& place);

/// @brief Where a resident's day starts and ends: his family's house.
/// @return false for a family without a house. Genesis gives every family
///         one, so this guards hand-built worlds rather than a normal case.
bool HomePositionOf(const WorldState& world, FamilyId family, Vec2& home);

}  // namespace core

#endif  // CORE_COMMON_WORK_SEAM_H_
