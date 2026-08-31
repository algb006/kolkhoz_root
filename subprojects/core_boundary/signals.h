/// @file
/// @brief Internal to core_boundary: the derived projections of the completed
/// state — the living signals of a unit and a field, where a resident is, and
/// which alarms stand.
/// @threading SINGLE_THREADED
/// Called only from the session's methods, which run on the sim thread
/// between steps (core_boundary/session.h). Pure reads of a completed state
/// that nothing is writing at the time; no state of their own, nothing to
/// synchronize.
///
/// These are the "living signals" the presentation must not compute for itself
/// (architecture §4; the catalogue, manual/design/presentation/live-signals.md
/// §11): counts that run ACROSS rows — who lives here, who works here, who is
/// on this field — plus where a person is. Everything the catalogue marks as a
/// plain field of a row (a pantry, a herd, a field's phase, a unit's stock) is
/// read straight from State() and needs nothing here.
///
/// Free functions of (config, state, id), pure: nothing is stored between
/// calls, and calling one twice on the same state gives the same answer. The
/// session holds them behind its methods because two of them need a balance
/// knob (boundary_config.h) and configuration belongs to a subsystem.
///
/// Cost: each is a sweep of the resident table, which is thousands of rows at
/// village scale and is asked for by a panel, not by a phase. Nothing here is
/// on the step's path.

#ifndef CORE_BOUNDARY_SIGNALS_H_
#define CORE_BOUNDARY_SIGNALS_H_

#include <vector>

#include "boundary_config.h"
#include "core_boundary/session.h"
#include "core_common/ids.h"
#include "core_common/world_state.h"

namespace core {

/// @brief The living signals of one unit, derived from `world` now.
/// @return A default UnitSignals (invalid id, neutral fields) when the unit
///         does not exist.
UnitSignals DeriveUnitSignals(const BoundaryConfig& config, const WorldState& world, UnitId unit);

/// @brief The living signals of one field, derived from `world` now.
FieldSignals DeriveFieldSignals(const WorldState& world, FieldId field);

/// @brief Where a resident is, derived from `world` now.
/// @note STUB while the labor model records no departure and arrival: at home
/// outside the solar window, at his assignment's place inside it, never on
/// the road (manual/70-boundary.md §10).
ResidentWhereabouts DeriveWhereabouts(const WorldState& world, ResidentId resident);

/// @brief Rebuilds the list of conditions standing in `world`, in a
/// deterministic order (by kind, then by subject id).
/// @note STUB: the alarm roster is task A3's — AlarmKind has one value and
/// this sweep therefore always yields nothing. It exists so that A3 has one
/// place to fill and the session's refresh path is already wired and tested.
void CollectAlarms(const WorldState& world, std::vector<Alarm>& alarms);

}  // namespace core

#endif  // CORE_BOUNDARY_SIGNALS_H_
