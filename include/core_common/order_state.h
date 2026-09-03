/// @file
/// @brief OrderRow — the chairman's order book: what came across the
/// boundary as a command, and how far the simulation has taken it.
/// @threading PARALLEL_READONLY
/// Rows live in WorldState::orders under the double-buffer discipline, and
/// every write is sequential.
///
/// WHO WRITES, and what is wired TODAY. The step engine APPENDS issued rows
/// and MARKS cancellations before phase 1, in arrival order (buffer-law
/// rule 2, core_sim/step.h). The consuming half is wired for the
/// construction kinds and only for those (project phase 2, task A2):
/// core_construction reads the book in its sub-step of the decisions slot
/// (phase 3) and settles kBuildUnit, kStartBuild, kUpgradeUnit and
/// kDemolishUnit — each to kDone or kRefused IN THE STEP IT IS READ, never
/// to kAccepted or kActive. The events slot (phase 7) then emits every
/// terminal row's event and REMOVES the row, so the book is empty again by
/// the end of the step that settled it. The work kinds are still
/// unconsumed: kAssignWork, kReleaseWork, kPauseUnit, kResumeUnit and
/// kSetRotation have no subsystem reading them (task O3), and the sweep
/// refuses them with kNoConsumer rather than letting them accumulate.
/// Parallel phases never touch the table — an order is a structural fact,
/// and structure changes only in sequential slots (buffer-law rule 6).
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
/// runs has no consumer: the events slot refuses it with
/// OrderRefusal::kNoConsumer (core_world/world.cpp, SweepOrderBook). That is
/// how an order kind whose mechanic has not arrived yet answers — with a
/// refusal the presentation can show, never with silence. It is live, and
/// the work kinds meet it every time they are issued until task O3.
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
/// waiting: the row is a section of the save format, learned by the codec in
/// task O1 of project phase 2 (VERSION_SAVE 3, core_save/save_rows.cpp).

#ifndef CORE_COMMON_ORDER_STATE_H_
#define CORE_COMMON_ORDER_STATE_H_

#include <cstdint>
#include <vector>

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

  /// MARK a new unit of `unit_type` at `position`: pegs and string, the
  /// plot taken, nothing spent (construction design §6). The row appears at
  /// level 0 with ConstructionPhase::kMarked and waits for kStartBuild.
  /// Refused with kGateClosed when the type's gate is shut, kTooClose when
  /// another unit's plot overlaps (unit rules §9), kNoSuchSubject for an
  /// unknown type. Consumer: core_construction.
  kBuildUnit,

  /// Demolish `unit`. A marked site goes at once and for free; a built
  /// unit is emptied first (unit rules §14 — its stock goes to the stores
  /// by the instant-delivery stub, the rest is lost, as the design says)
  /// and then dismantled with labour (construction design §12). Refused
  /// with kNotEmpty while a household lives or a herd stands there.
  /// Consumer: core_construction.
  kDemolishUnit,

  /// START the works on a marked site (`unit`): construction design §6 —
  /// "the works begin only on the chairman's command", never by themselves
  /// when materials appear. Refused with kRuleForbids unless the unit is
  /// kMarked. Consumer: core_construction.
  kStartBuild,

  /// Raise `unit` to its next level (unit rules §11). No marking phase — the
  /// plot is already there — so the site starts delivering at once. The
  /// unit keeps working at its current level meanwhile. Refused with
  /// kRuleForbids at the top of the ladder or while another site is in
  /// progress on it, kGateClosed when the next level's era has not come.
  /// Consumer: core_construction.
  kUpgradeUnit,

  /// Repair a standing unit (task A5; construction design §11, unit rules
  /// §15). Names `unit`. Opens a site on it — kDelivering for the spare
  /// parts, then kRepairing for the labour — and the unit works meanwhile.
  /// Cost scales with the wear at the moment of the order: a neglected
  /// repair is dearer, in parts and in man-days alike. Refused when the
  /// unit is not built, is a site already, has nothing to wear (has_wear
  /// = 0) or nothing worn (wear = 0), or is one of the start's old houses,
  /// which the canon says cannot be repaired, only replaced (housing design
  /// §10). Decided in the step it is read, like the other four.
  kRepairUnit,

  /// Appoint `resident` to the post `profession` at `unit` (task A7;
  /// manual/74-posts.md). NOT kAssignWork: that one puts a person on a
  /// day's work at a field or herd and outranks the accountant for as long
  /// as it stands; this one gives a person a PLACE — the groom of this
  /// yard, the storekeeper of this granary — that survives the day and the
  /// save and is his until a kDismiss. The two look alike and must not be
  /// merged: one names a job, the other a role. Validated in the step it
  /// is read and then kAccepted, cancellable, until the day's close, when
  /// the labor sub-step applies it — "a change of post only after the
  /// working day" (time design §11). A resident holding another post is
  /// MOVED by this order; a second kAppoint for a resident whose first is
  /// still kAccepted is kConflictsWithActive. Refused: no such resident
  /// (kNoSuchSubject); outside the post's age band, the wrong sex for it,
  /// or below its education threshold — all columns of professions.csv
  /// (kNotEligible); no such unit, a site, or a unit type and level that
  /// carry no such post per unit_staff.csv (kRuleForbids); every slot at
  /// the unit taken, or a `single_post` post already held anywhere in the
  /// village (kNoVacancy). Consumer: core_labor.
  kAppoint,

  /// Dismiss `resident` from the post he holds — the same kind of order as
  /// the appointment, at boss's insistence (2026-09-03): a post nobody can
  /// be removed from is a post for life, and the player notices on the
  /// first groom who takes to drink. Same timing as kAppoint: kAccepted,
  /// applied at the day's close. Refused with kRuleForbids when the
  /// resident holds no post. Consumer: core_labor.
  kDismiss,

  // Reserved, appended by their tasks and named here so the numbering is
  // planned rather than discovered: nomenclature (unit rules §6), transport
  // as part of orders (root decision 155, task A4), delegation (Epoch II).
  //
  // APPENDING A KIND MEANS RAISING kMaxOrderKind in core_save/save_rows.cpp
  // AND in core_boundary/journal_codec.cpp — two guards, one rule, and the
  // journal's was left behind at kDemolishUnit when A2 appended two kinds
  // (found by task A5's design pass; a journal carrying kStartBuild would
  // have been refused on decode):
  // the save codec validates the byte it read against the last enumerator,
  // and a guard left behind refuses every save carrying the new kind. There
  // is no sentinel to derive it from on purpose — ShapeIsValid in
  // core_boundary/session.cpp switches over this enum WITHOUT a default, so
  // a new kind is a compile error there until it is handled, and a sentinel
  // would have to be handled too. The same note stands over OrderRefusal.
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

  /// The unit type's gate is shut (unlocks design §1): its era has not
  /// come, or it opens by an event, quest or unit that has not happened.
  /// The presentation names the reason in words FROM ITS OWN COPY of
  /// unit_types.csv — the refused order's unit_type says which type, the
  /// type's `gate` and `gate_ref` say what opens it. The core carries no
  /// text and no dictionary (unlocks design §4; 70-boundary.md §7).
  kGateClosed,

  /// The plot would overlap another unit's: two units cannot stand closer
  /// than the sum of their radii (unit rules §9). The refusal event names
  /// the neighbour in SimEvent::unit.
  kTooClose,

  /// Demolition refused because something lives here: a household in a
  /// house, a herd at a barn (unit rules §14 — "the living is not
  /// demolished"; the stock, by contrast, is moved out, not refused).
  kNotEmpty,

  /// Appointment refused because every slot of that post at that unit is
  /// taken (task A7; the staff table of the design db says how many —
  /// one groom per farm, and it is the whole reason the refusal has a
  /// name of its own: "no room" at a post is a fact the presentation
  /// shows, not a rule it has to guess).
  kNoVacancy,

  // Appending a refusal means raising kMaxOrderRefusal in
  // core_save/save_rows.cpp — see the note over OrderKind above.
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

  /// For kAppoint: which post (task A7). It stands here, among the flavour
  /// fields and not down with the targets, for the same reason `work` does —
  /// both say WHICH KIND of a thing the order is about, not which thing —
  /// and because the four leading bytes leave a hole exactly this wide: the
  /// row stays 48 bytes and the book stays cheap to copy every step.
  ProfessionId profession;

  /// The completed tick the order was issued after (boundary stamp at the
  /// moment of IssueOrder); the row is applied at the start of the step
  /// that follows. The journal replays by this tick.
  Tick issued_tick = 0;

  // -- targets, by kind ------------------------------------------------------
  ResidentId resident;  ///< kAssignWork, kReleaseWork, kAppoint, kDismiss.

  UnitId unit;  ///< kPauseUnit, kResumeUnit, kDemolishUnit, kAppoint; kAssignWork at a unit.

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

/// @brief The batch the session has staged and the engine has not applied
/// yet: issued rows in issue order, then ids to cancel. Between two steps
/// this is the ONLY state that is neither in the completed world nor in
/// the journal — and a campaign is saved between steps, on pause, after
/// the player has handed out the day's orders. A save that dropped it would
/// punish the player for the unforeseeable (root principle), so the save
/// format carries it as a section beside the world (core_save/save.h) and
/// the session hands it back on load (core_boundary/session.h,
/// StagedBatch / ReplaceWorld). Boss decision of 2026-08-31, project phase
/// 2; the same struct is what ISimulation::StageOrders takes, as two spans.
struct StagedOrders {
  std::vector<OrderRow> issued;

  std::vector<OrderId> cancelled;
};

}  // namespace core

#endif  // CORE_COMMON_ORDER_STATE_H_
