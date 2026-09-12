/// @file
/// @brief SimEvent — the step's outbox: what happened this step, for the
/// presentation to show and the fast-forward to be interrupted by.
/// @threading PARALLEL_READONLY
/// The outbox lives in WorldState::step_events under the double-buffer
/// discipline, and every write is sequential: the step engine CLEARS it
/// right after the copy of `previous` into `current` (buffer-law rule 2,
/// core_sim/step.h), sequential sub-steps append as things happen in them,
/// and the events slot (phase 6) appends what the parallel phases produced,
/// folded in row order — a parallel phase itself never appends (buffer-law
/// rule 5: no cross-row accumulators, and the outbox is one). Between steps
/// the boundary reads the completed step's outbox and moves it into its own
/// log (core_boundary/session.h); nothing in the simulation reads it back.
///
/// NOT SAVED. This is the second block nothing in the simulation reads (the
/// ledger is the first, ledger_state.h), and unlike the ledger it is not
/// history: it holds one step's worth and is cleared the next. The save
/// codec neither writes nor reads it; a loaded world starts with an empty
/// outbox. What the presentation has not yet shown is the presentation's
/// concern, not the world's.
///
/// EVENTS ARE TRANSITIONS, ALARMS ARE CONDITIONS. "A resident died" is an
/// event: it happened at a tick and cannot be re-derived from the state
/// after. "The herd is underfed" is not an event — it is true for as long
/// as the state says so, and it is derived from the state on demand
/// (Alarm in core_common/alarm_state.h; office design §13). An emitter that
/// finds itself appending the same event every step is reporting a
/// condition and should stop.
///
/// SEVERITY IS PER INSTANCE. The office design (§14) sorts what happens
/// into what only the summary mentions, what earns one HUD line, and what
/// breaks a fast-forward (time design §1: fire, a die-off, an accident, a
/// grave illness, an inspector's arrival). The same kind lands in different
/// rows — a death of old age is routine, a death of hunger is not — so the
/// emitter states the severity on each event rather than the kind fixing
/// it. Kinds are appended, never renumbered: the presentation may keep an
/// event log of its own across builds.

#ifndef CORE_COMMON_EVENT_STATE_H_
#define CORE_COMMON_EVENT_STATE_H_

#include <cstdint>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/ids.h"

namespace core {

/// @brief How loudly the presentation should say it (office design §14).
enum class EventSeverity : std::uint8_t {
  kRoutine = 0,   ///< Summary material only; no notification.
  kNotable,       ///< One HUD notification; never breaks a fast-forward.
  kInterrupting,  ///< Breaks a fast-forward and notifies (time design §1).

  /// NOT A VALUE: the number of them, for a consumer's mirror. Values are
  /// appended BEFORE it.
  kEventSeverityCount,
};

/// @brief What happened. Grouped by the sub-step that emits it; the fields
/// each kind fills are named on the kind, the rest stay invalid / zero.
enum class EventKind : std::uint8_t {
  kNone = 0,

  // -- people: the demography sub-step of the decisions slot -------------
  kResidentBorn,     ///< resident, family.
  kResidentDied,     ///< resident, family; amount = cause code of the emitter.
  kResidentArrived,  ///< resident, family — a migrant who came to stay.
  kResidentLeft,     ///< resident, family — the outflow.
  kWedding,          ///< resident (the bride), family (the new household).

  // -- land: the production decisions sub-step ------------------------------
  kFieldPhaseChanged,  ///< field; amount = the new FieldPhase value.
  kFieldHarvested,     ///< field, resource, amount (grams into the stores).
  kFieldLost,          ///< field — the crop lost to snow (farming design §6).

  // -- herds: the production decisions sub-step -----------------------------
  kHerdBorn,  ///< herd; amount = heads.
  kHerdDied,  ///< herd; amount = heads; severity says hunger from age.

  // -- labor: the assignments sub-step ---------------------------------------
  kWalkOff,  ///< resident — left work under the fatigue limit (unit rules §8).

  // -- the family exchange and the year ---------------------------------------
  kDistributionIssued,  ///< The monthly distribution went out (labor-payment §3).
  kRationIssued,        ///< family — the safety ration below the floor (§5).
  kYearClosed,          ///< The ledger rotated; amount = the year that closed.

  // -- the order book: the events slot sweep ---------------------------------
  //
  /// @no_emit the order is still the consumer's own: the sweep announces
  /// only terminal states (world.cpp, "accepted or active: still the
  /// consumer's"). A caller that issued an order knows it issued it, and
  /// hears back when it ENDS — telling it that its own order was accepted
  /// is an echo, not news. The two kinds stay in the enum because the state
  /// machine has those states and a numbering with holes in it is worse
  /// than a numbering with silences.
  kOrderAccepted,  ///< order.

  /// @no_emit the same reason as kOrderAccepted above: a state the issuer
  /// already knows it asked for.
  kOrderStarted,    ///< order.
  kOrderDone,       ///< order.
  kOrderRefused,    ///< order; amount = the OrderRefusal value.
  kOrderCancelled,  ///< order.

  // -- units -------------------------------------------------------------------
  kUnitPaused,      ///< unit.
  kUnitResumed,     ///< unit.
  kUnitBuilt,       ///< unit (task A2).
  kUnitDemolished,  ///< unit (task A2); the id is dead after this step.

  // -- wear (task A5) ---------------------------------------------------------
  kUnitRepaired,  ///< unit — a repair finished; wear is back at 0.

  /// unit — one of the start's old houses fell at 100 wear (start design
  /// §4): the id is dead after this step, and its household stands without
  /// a house until the demography sub-step rehouses it the next day.
  kUnitCollapsed,

  // -- posts (task A7) --------------------------------------------------------
  kAppointed,  ///< resident, unit; amount = the ProfessionId value. Applied at the day's close.
  kDismissed,  ///< resident, unit; amount = the ProfessionId value he held.

  /// The post is empty and NOBODY ordered it: its holder died or left
  /// (resident, unit; amount = the ProfessionId value he held). Boss's
  /// decision of 2026-09-03, and its reasoning is worth keeping: an empty
  /// post is not a standing trouble the player must clear — the place may
  /// simply not be needed any more — so it is an EVENT and not an alarm.
  /// The trouble, when there is one, arrives with its own alarm: a yard
  /// whose groom is gone stops tending the herd and raises kHerdStarving.
  /// Seventy posts each raising an alarm would be noise; each announcing
  /// itself once, on the day it happens, is news.
  ///
  /// A DISMISSAL does not raise this — it already announces itself with
  /// kDismissed, and the same fact twice is the noise the rule above exists
  /// to avoid. This kind means "it emptied by itself".
  kPostVacated,

  /// unit — the kolkhoz yard: the groom is in place and the horses came
  /// off the private yards, all at once (livestock design §5); amount =
  /// heads. Once per campaign, and kNotable: the day a third of the village
  /// is free to work again.
  kHorsesStabled,

  // -- the district's plan (2026-09-12) --------------------------------------

  /// The economic year closed and the district was satisfied; amount = how
  /// many met years stand in a row. Raised at the year's turn, after the
  /// delivery it judges.
  kPlanMet,

  /// The economic year closed short on at least one position (epochs design
  /// §8, "сорванный план"); amount = how many failed years stand in a row.
  kPlanFailed,

  /// The failed years have reached the district's limit — the CONDITION of
  /// "Под суд" (epochs design §8), raised once, on the day the count
  /// reaches it. amount = that count.
  ///
  /// THE CORE STOPS HERE ON PURPOSE. The signal, the commission, the case
  /// and the courtroom are the presentation's (boss, 2026-09-12: "подача
  /// концов не твоя; твоё — условие и событие"), and a core that removed
  /// the chairman itself would be deciding how the game ends.
  kPlanTrialDue,

  // Reserved for project phase 3 and appended by it: fire, epoch change,
  // an inspector's arrival, the decision card. Named so the numbering is
  // planned, not discovered.

  /// NOT A KIND, and never a value anybody stores or sends: the count, so a
  /// CONSUMER can static_assert the length of its own mirror.
  ///
  /// That is the whole reason, and it is why this one was missing. The
  /// counts were first asked of the SERIALIZABLE enums — the ones whose
  /// codecs range-check — but the reason was mirrors, and the two sets are
  /// not the same set. These are exactly the tables that have already
  /// drifted silently once, and they were the ones left without a guard
  /// (boss, 2026-09-04). Narrowing a rule by a property that was not its
  /// reason looks like tidiness and works like a hole.
  kEventKindCount,
};

/// @brief One thing that happened. Plain data, no text: the presentation
/// composes the sentence from the ids and the string tables. Every id that
/// a kind does not name stays invalid; `amount` means what the kind says.
struct SimEvent {
  /// The tick of the step that emitted it — the calendar tick of `current`
  /// after phase 1, which is the tick the completed state will carry.
  Tick tick = 0;

  EventKind kind = EventKind::kNone;

  EventSeverity severity = EventSeverity::kRoutine;

  ResidentId resident;

  FamilyId family;

  UnitId unit;

  FieldId field;

  HerdId herd;

  OrderId order;

  /// For kinds that move a resource: which one. Invalid otherwise.
  ResourceId resource;

  /// Grams, heads, a phase value, a refusal code — the kind says which.
  std::int64_t amount = 0;
};

/// @brief The outbox type used by WorldState: one step's events, in the
/// order they were appended, which is deterministic because every appender
/// is sequential.
using StepEventLog = std::vector<SimEvent>;

}  // namespace core

#endif  // CORE_COMMON_EVENT_STATE_H_
