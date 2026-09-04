/// @file
/// @brief EmitEvent — the one way a subsystem says that something happened.
/// @threading SINGLE_THREADED
/// The outbox is a plain vector on the current state and is appended only by
/// sequential code (buffer-law rule 5): the time slot, the decisions slot,
/// the events slot. A parallel phase must not call this — its order would
/// depend on the scheduler, and the step's events would stop being
/// reproducible.
///
/// WHY IT IS A SHARED HELPER AND NOT SEVEN LINES PER FILE. On 2026-09-05 the
/// host counted the call sites: of twenty-nine event kinds, eleven were
/// written and eighteen were not. The gap was not difficulty — the file that
/// announces a vacated post creates a child two hundred lines lower and says
/// nothing — it was that every emitter was a local copy, so the mechanism
/// got applied FROM MEMORY rather than by rule, and memory covered an
/// arbitrary subset. One helper with one name is also what lets a check walk
/// the enum and ask which kinds have a site (scripts/event_sites.py).

#ifndef CORE_COMMON_EMIT_EVENT_H_
#define CORE_COMMON_EMIT_EVENT_H_

#include "core_common/event_state.h"
#include "core_common/world_state.h"

namespace core {

/// @brief Appends one event to the step's outbox.
/// @param current  The state being written this step; its calendar tick
///                 stamps the event, so the phase that emits decides which
///                 tick it belongs to and no later pass has to guess.
/// @param kind     What happened.
/// @param severity How loudly it should be said.
/// @return A reference to the appended event, so the caller can fill in the
///         subjects it has — `EmitEvent(w, k, s).resident = id;` — instead
///         of the helper growing an overload per combination of ids. The
///         reference is valid until the next emit of the step.
/// @note Sequential slots only (see @threading). Call it where the thing
///       HAPPENS: an event assembled at the end of a step loses the tick it
///       belongs to, and with it the order the player is shown.
inline SimEvent& EmitEvent(WorldState& current,
                           EventKind kind,
                           EventSeverity severity = EventSeverity::kRoutine) {
  SimEvent event;
  event.tick = current.calendar.tick;
  event.kind = kind;
  event.severity = severity;
  current.step_events.push_back(event);
  return current.step_events.back();
}

}  // namespace core

#endif  // CORE_COMMON_EMIT_EVENT_H_
