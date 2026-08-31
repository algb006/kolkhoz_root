/// @file
/// @brief OrderRow — the chairman's order book: what came across the
/// boundary as a command, and how far the simulation has taken it.
/// @threading PARALLEL_READONLY
/// Rows live in WorldState::orders under the double-buffer discipline, and
/// every write is sequential. The step engine APPENDS issued rows and MARKS
/// cancellations before phase 1, in arrival order (buffer-law rule 2,
/// core_sim/step.h); the one subsystem that consumes a kind moves its rows
/// through the statuses below inside its own sub-step of the decisions slot
/// (phase 3); the events slot (phase 7) emits the order's events and REMOVES
/// rows that reached a terminal status. Parallel phases never touch the
/// table — an order is a structural fact, and structure changes only in
/// sequential slots (buffer-law rule 6).
///
/// WHY AN ORDER IS STATE AND NOT A MESSAGE (project phase 2, task A1;
/// manual/70-boundary.md). The design defers almost everything the chairman
/// decides: a reassignment takes effect from the next working day, a unit
/// stops when its cycle ends, a construction site opens when the crew
/// arrives — and while an order waits it is visible ("status: pending",
/// time design §11) and may be cancelled without trace. A thing that waits,
/// is shown and can be taken back is state: it must survive a save, be
/// copied with the buffers and compared by the determinism check. So a
/// command from the presentation does not DO anything; it puts a row in
/// this book, and the subsystem whose rules apply reads the book when its
/// moment comes. The boundary itself knows two verbs — issue and cancel —
/// and no order semantics at all.
///
/// LIFECYCLE, and who moves a row (one consumer per kind, named on the kind):
///
///     issued ──► kPending ──► kAccepted ──► kActive ──► kDone
///                   │             │            │
///                   │             │            └──► kRefused (rule broke mid-way)
///                   │             └──► kRefused | kCancelled
///                   └──► kRefused | kCancelled
///
///   * kPending   — appended by the engine this step; nobody has read it.
///   * kAccepted  — the consumer validated it and waits for its moment
///                  (the next morning, the end of a cycle). Cancellable.
///   * kActive    — execution began; from here the work is finished, not
///                  taken back (time design §11: "работа началась — только
///                  довести до конца"). Not cancellable.
///   * kDone, kRefused, kCancelled — terminal. The events slot emits the
///                  matching event and removes the row in the same step.
///
/// A row still kPending when the events slot of the step it was applied in
/// runs has NO consumer in the wired simulation: the events slot refuses it
/// with OrderRefusal::kNoConsumer. That is how an order kind whose mechanic
/// has not arrived yet (construction before task A2) answers — with a
/// refusal the presentation can show, never with silence.
///
/// THE ONE APPENDER. Only the step engine appends to this table, and it
/// appends the staged rows in the order they were staged. Ids are therefore
/// predictable — next_id_value, then +1, +2 — and the boundary hands the
/// caller the id a row WILL carry at the moment of issue, before the step
/// that creates the row has run. A subsystem that wanted to raise an order
/// of its own (an Epoch-III specialist's proposal, delegation design §6)
/// would need a different table; this one is the chairman's.
///
/// SAVED. A campaign saved with an order waiting must resume with it
/// waiting: the row is part of the save format (VERSION_SAVE moves when the
/// codec learns it — the implementing task's first line of work).

#ifndef CORE_COMMON_ORDER_STATE_H_
#define CORE_COMMON_ORDER_STATE_H_

#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/labor_state.h"
#include "core_common/state_table.h"

namespace core {

/// @brief What the chairman ordered. Each kind names the subsystem that
/// consumes it; a kind no wired subsystem consumes is refused by the events
/// slot (see @file). Values are stable: kinds are appended, never renumbered
/// or reused — a journal written by an older build must still read.
enum class OrderKind : std::uint8_t {
  kNone = 0,

  /// Put `resident` on `work` at `field` / `herd` from the next working day
  /// (time design §11: one job per day, a change takes effect after the
  /// current day). The chairman's standing order outranks the accountant's
  /// morning placement for that resident — a boss decision of 2026-08-31
  /// grounded in delegation design §7 (management by exception), with its
  /// bounds fixed there: the order holds until kDone or a kReleaseWork,
  /// not for one day; and it revokes nothing — one overridden placement
  /// leaves the accountant's delegation as it was. Consumer: core_labor.
  kAssignWork,

  /// Release `resident` from a standing kAssignWork order: back to the
  /// accountant's placement from the next working day. Consumer: core_labor.
  kReleaseWork,

  /// Stop production at `unit` — at once if no cycle is running, otherwise
  /// when the running cycle ends (unit rules §5). Consumer: core_production.
  kPauseUnit,

  /// Resume a paused `unit`; work restarts the next day. Consumer:
  /// core_production.
  kResumeUnit,

  /// Set the three-year rotation of `field` to rotation_year0..2 (farming
  /// design §7). Consumer: core_production.
  kSetRotation,

  /// Open a construction site: a unit of `unit_type` at `position` (task A2;
  /// construction design). Refused with kNoConsumer until A2 lands.
  kBuildUnit,

  /// Demolish `unit` (unit rules §14: empty it first — the consumer refuses
  /// a unit with stock or residents). Consumer: A2.
  kDemolishUnit,

  // Reserved, appended by their tasks and named here so the numbering is
  // planned rather than discovered: nomenclature (unit rules §6), transport
  // as part of orders (root decision 155, task A4), delegation (Epoch II).
};

/// @brief Where an order stands. Terminal statuses are removed by the events
/// slot in the step they are reached, after the matching event goes out.
enum class OrderStatus : std::uint8_t {
  kPending = 0,  ///< Applied this step; no consumer has read it yet.
  kAccepted,     ///< Validated, waiting for its moment. Cancellable.
  kActive,       ///< Execution began. Finished, never taken back.
  kDone,         ///< Terminal.
  kRefused,      ///< Terminal; `refusal` says why.
  kCancelled,    ///< Terminal; cancelled while kPending or kAccepted.
};

/// @brief Why an order was refused. The presentation turns the code into a
/// string (db/strings.db); the core never carries text.
enum class OrderRefusal : std::uint8_t {
  kNone = 0,
  kNoConsumer,           ///< No wired subsystem handles this kind (yet).
  kNoSuchSubject,        ///< A required id names no live entity.
  kNotEligible,          ///< The subject may not do this (a child at the plough).
  kConflictsWithActive,  ///< The one-active-task rule (time design §11).
  kRuleForbids,          ///< Some other rule of the consumer; its event says which.
};

/// @brief One order. Plain data; `kind` says which target fields are read,
/// the rest stay at their invalid defaults. Sized for the save codec's
/// tripwire like every row.
struct OrderRow {
  OrderKind kind = OrderKind::kNone;

  OrderStatus status = OrderStatus::kPending;

  OrderRefusal refusal = OrderRefusal::kNone;

  /// For kAssignWork: the kind of work. kNone otherwise.
  WorkKind work = WorkKind::kNone;

  /// The completed tick the order was issued after (boundary stamp at the
  /// moment of IssueOrder); the row is applied at the start of the step
  /// that follows. The journal replays by this tick.
  Tick issued_tick = 0;

  // -- targets, by kind ------------------------------------------------------
  ResidentId resident;  ///< kAssignWork, kReleaseWork.

  UnitId unit;  ///< kPauseUnit, kResumeUnit, kDemolishUnit; kAssignWork at a unit.

  FieldId field;  ///< kAssignWork (field kinds), kSetRotation.

  HerdId herd;  ///< kAssignWork (kHerdCare).

  UnitTypeId unit_type;  ///< kBuildUnit.

  CropId rotation_year0;  ///< kSetRotation; invalid = fallow that year.

  CropId rotation_year1;

  CropId rotation_year2;

  Vec2 position;  ///< kBuildUnit.
};

/// @brief The order book type used by WorldState.
using OrderTable = StateTable<OrderId, OrderRow>;

}  // namespace core

#endif  // CORE_COMMON_ORDER_STATE_H_
