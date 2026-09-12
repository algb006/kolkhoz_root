/// @file
/// @brief OrderRow — the chairman's order book: what came across the
/// boundary as a command, and how far the simulation has taken it.
/// @threading PARALLEL_READONLY
/// Rows live in WorldState::orders under the double-buffer discipline, and
/// every write is sequential.
///
/// WHO WRITES, and what is wired TODAY. The step engine APPENDS issued rows
/// and MARKS cancellations before phase 1, in arrival order (buffer-law
/// rule 2, core_sim/step.h). The consuming half is wired for EVERY kind
/// since 2026-09-12:
/// THERE ARE THREE CONSUMERS NOW, all in sub-steps of the decisions slot
/// (phase 3), and each settles its own kinds to kDone or kRefused IN THE
/// STEP THE ROW IS READ — never to kAccepted or kActive, except where a kind
/// says otherwise:
///   * core_construction — kBuildUnit, kStartBuild, kUpgradeUnit,
///     kDemolishUnit, kRepairUnit;
///   * core_production — kPauseUnit, kResumeUnit, kUnsealFund and
///     kSetRotation;
///   * core_labor — kAssignWork, kReleaseWork, kAppoint and kDismiss, the
///     last two applied at the day's close rather than at once.
/// The events slot (phase 6) then emits every terminal row's event and
/// REMOVES the row, so the book is empty again by the end of the step that
/// settled it.
///
/// NOTHING IS UNCONSUMED ANY MORE, and this paragraph named kSetRotation as
/// the last one until 2026-09-12. It got its consumer that evening — the
/// player's lever for telling a field what to grow, and the reason
/// ninety-three of the start's hundred and sixty-three hectares had lain
/// unworked through every thirty-year run the project had measured: work is
/// opened off the rotation, and nothing could give a field one after genesis.
///
/// The sweep's kNoConsumer is not dead for that. It is the guard for a kind
/// ADDED WITHOUT A CONSUMER, which is the mistake that otherwise leaves no
/// trace at all — an order accepted, answered by nobody, and quietly gone.
///
/// This paragraph said "wired for the construction kinds and only for those"
/// until 2026-09-12, listing the pause verbs among the unconsumed while the
/// kind entries below named core_production as their consumer — the file
/// disagreeing with itself, which is how a reader comes away certain of the
/// wrong half. A CONSUMER LIST IS A CONTRACT, and one short by two modules
/// is the shape in which a fourth consumer lands unchallenged. It was short
/// by two kinds that morning and by one that evening — the delivery that
/// gave kSetRotation a consumer announced the fact in the paragraph below
/// and left the list two lines above unchanged (analysis, 0.17.96). A list
/// repaired by hand stays short until the next reader counts it.
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
#include "core_common/quantities.h"
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
  /// design §7). An empty slot is a FALLOW year and is a legal thing to
  /// order; ALL THREE EMPTY is the chairman taking his word back, and the
  /// field goes back to unassigned (land_state.h, rotation_assigned). The
  /// crop named FIRST is the one the next sowing puts in the ground, whatever
  /// month the order arrives — the chain is a cycle and the consumer sets its
  /// phase. Refused with kNoSuchSubject for a field that is not there,
  /// kNoSuchCrop for a slot naming a crop this build does not carry, and
  /// kWrongLand for a meadow, which is mown where it grew and is never sown
  /// at all. Consumer: core_production.
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
  /// (kNotEligible); no such unit (kNoSuchSubject, as in construction — a
  /// named thing that is gone answers the same whichever book it lies in);
  /// a site, or a unit type and level that
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

  /// UNSEAL A FUND (resources design §6): take `amount` of `resource` out of
  /// the sealed `fund` and let the automation touch it. The design's own
  /// emergency door — "нечем кормить людей — можно взять из резерва плана и
  /// даже из семенного" — and it is a door the chairman opens by NAMING A
  /// FIGURE, because the same paragraph calls it "осознанный выбор, а не
  /// незаметная утечка".
  ///
  /// ONE VERB FOR BOTH FUNDS, not two: the design gives the same door to the
  /// plan reserve and to the seed fund, and a second verb for the second
  /// fund would be one decision with two homes (boss, 2026-09-12).
  ///
  /// THE CORE INVENTS NO CONSEQUENCE. It subtracts, and that is all: the
  /// grain becomes issuable, so come the delivery there is less in the store
  /// and the plan falls short, or come the sowing there is less seed. The
  /// design already calls those consequences natural and deferred, and
  /// anything added here would be a punishment nobody wrote down.
  ///
  /// Settled in the step it is read, like the pause verbs. Consumer:
  /// core_production.
  kUnsealFund,

  // Reserved, appended by their tasks and named here so the numbering is
  // planned rather than discovered: nomenclature (unit rules §6), transport
  // as part of orders (root decision 155, task A4), delegation (Epoch II).
  //
  // APPEND BEFORE kOrderKindCount. That is the whole rule, and it is a fact
  // about this enum rather than an instruction about two other files.
  //
  // It used to be the instruction: "raise kMaxOrderKind in
  // core_save/save_rows.cpp AND in core_boundary/journal_codec.cpp". Two
  // homes, one rule, and the journal's copy sat at kDemolishUnit for two
  // whole tasks after A2 appended two kinds — found by a design pass rather
  // than by anything that runs, and a journal carrying kStartBuild would
  // have been refused on decode. A rule whose only mechanism is a comment is
  // not a mechanism, it is an intention, and an intention fails no check
  // because it takes part in none (boss, 2026-09-04).
  //
  // The count is safe to add even though ShapeIsValid in
  // core_boundary/session.cpp switches over this enum WITHOUT a default: a
  // new REAL kind is still a compile error there until it is handled, which
  // is the tripwire that note was protecting. The sentinel is handled once,
  // beside kNone, and refused for the same reason.
  //
  // The same holds over OrderStatus and OrderRefusal.

  /// NOT A VALUE, and never written to a save or read from one: the
  /// codecs range-check 0..kOrderKindCount-1 and this is what they check against.
  /// Values are appended BEFORE it — that is the whole rule, and it is a
  /// fact here rather than an instruction somewhere else. A length
  /// written out by hand beside an enum drifts, and four of them already
  /// had (journal_codec.cpp).
  kOrderKindCount,
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

  /// NOT A VALUE, and never written to a save or read from one: the
  /// codecs range-check 0..kOrderStatusCount-1 and this is what they check against.
  /// Values are appended BEFORE it — that is the whole rule, and it is a
  /// fact here rather than an instruction somewhere else. A length
  /// written out by hand beside an enum drifts, and four of them already
  /// had (journal_codec.cpp).
  kOrderStatusCount,
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

  /// The field cannot carry this kind of work AT ALL, and never will: a
  /// meadow is mown and never ploughed, harrowed or sown (land design, the
  /// meadow branch of the production day).
  ///
  /// It named unraised land too until 2026-09-12, and that reading is gone
  /// with the land kind behind it: the design says an overgrown field is a
  /// LOOK and not a state, ploughed by the same norm as any other ground
  /// (farming design, «Вспашка одна на любую землю»: "целина, залежь,
  /// задерневшее поле, пашня, которую держат двадцать лет подряд — норма
  /// вспашки одна").
  ///
  /// THE QUOTATION HERE WAS WRONG UNTIL THE SECOND ANALYSIS PASS CHECKED IT
  /// against the document — close in substance, invented in form, and inside
  /// quotation marks, which is the part that makes it a defect rather than a
  /// paraphrase. A reader searching the design for the sentence would not
  /// have found it.
  ///
  /// THE POINT IS THE WORD "NEVER", AND IT IS WHY THIS HAS A NAME OF ITS
  /// OWN rather than falling into kRuleForbids. An order whose field has
  /// simply moved on to another phase today is REFUSED BY NOTHING and stands
  /// — the phase comes round again, and "no work today" is honest silence.
  /// This one never comes round. An accepted order that can never fire, and
  /// says nothing, is exactly "do not punish the unforeseeable" (root rules
  /// §7): the chairman sees a man standing idle and can learn the reason
  /// from nowhere, because the reason is permanent and has no sign.
  ///
  /// The refused order carries the field and the work kind already, so the
  /// presentation reads WHICH field and WHICH work off the order row and
  /// needs no dictionary from the core — the same shape as kGateClosed.
  /// EXCEPT FOR ITS NEWEST CALLER: a kSetRotation row names a field and
  /// carries WorkKind::kNone, so there the presentation has the field and
  /// nothing else to read. The word "never" still fits — a meadow is mown
  /// where it grew and can carry no sowing ever — but the sentence above
  /// describes the work orders only (analysis, 0.17.96).
  kWrongLand,

  /// The district has not named this year's plan yet, so a SHARE of it does
  /// not exist to open.
  ///
  /// RAISED BY AN EMPTY `plan.due` AND NOT BY THE CALENDAR, which is wider
  /// than the window it was written for. In a running campaign that window
  /// is the eight game days of forty-eight between the year's turn, where
  /// JudgePlan clears the vector, and the first day of spring, where
  /// AnnouncePlan fills it again (boss, 2026-09-12; district design §9). But
  /// a settlement that worked NO ARABLE last year, or whose tables name no
  /// plan positions or a zero share, is announced an empty plan and gets this
  /// answer all year — which is still TRUE, and still the most useful thing
  /// that can be said, but a reader expecting "only in February" would be
  /// wrong about it.
  ///
  /// THE CROPS IN THE ROTATION SLOTS ARE NO LONGER THE TEST, and this
  /// sentence named them until 2026-09-13: the norm came off the crop
  /// standing in each field's year0, so a settlement that sowed nothing owed
  /// nothing. It is off the area worked last year and the district's own
  /// positions now, which is what makes "sowing less does not owe less" true
  /// (production_system.cpp, AnnouncePlan).
  ///
  /// A NAME OF ITS OWN BECAUSE THE OTHER TWO READINGS ARE BOTH FALSE. The
  /// fund is not empty — the grain is in the stores where it always was. The
  /// rule does not forbid it — unsealing the reserve in a hungry winter is
  /// exactly what the design says a chairman may do. What is missing is the
  /// NUMBER the share is taken from, and a chairman told "rule forbids"
  /// would go looking for a rule that does not exist.
  ///
  /// And the move it teaches is a real one: wait for the spring
  /// announcement. A refusal a player can act on is the difference between
  /// difficulty and irritation (root rules §7 — a problem must be
  /// preventable, and an unintelligible one cannot be).
  kNoPlanYet,

  /// A slot of a rotation names a crop this build's table does not carry.
  ///
  /// SPLIT OFF kNoSuchSubject ON 2026-09-12, and for the reason kGateClosed
  /// and kWrongLand have names of their own: THE TWO CASES HAVE DIFFERENT
  /// REPAIRS. No such field — choose another field. No such crop — choose
  /// another crop. One code answering for both tells the chairman that
  /// something was missing and leaves him to guess which, which is "do not
  /// punish the unforeseeable" turned inside out (root rules §7).
  ///
  /// THE ORDER ROW CARRIES ALL THREE SLOTS AND THE REFUSAL NAMES NO ONE OF
  /// THEM. The presentation can say "one of these crops is not in this
  /// build" and can find it by checking the three against its own copy of
  /// crops.csv, which is exactly the check the core just made — but the core
  /// does not hand it the answer. Naming the slot would be a field on the
  /// refusal, and nothing has asked for one yet.
  ///
  /// IT IS A LAYER AND A CORE DISAGREEING ABOUT THE CROP TABLE, never a
  /// player's mistake: the boundary checks the SHAPE of an order and the
  /// roster is core_production's knowledge. An empty slot is not this — it
  /// is a fallow year, and three of them are the chairman taking his word
  /// back (land_state.h, rotation_assigned).
  kNoSuchCrop,

  // A REFUSAL APPENDED HERE NEEDS NOTHING DONE IN core_save: kMaxOrderRefusal
  // is derived from the count below and raises itself. This comment used to
  // say the opposite — "means raising kMaxOrderRefusal in save_rows.cpp" —
  // and it was stale: the hand-written constant it names has not existed
  // since the MEM-002 fix. A reader obeying it would have gone looking for a
  // number that is not there, and the honest way to obey it is to write one,
  // which is the very drift the counts were introduced to end.
  //
  // WHAT IT DOES COST is outside this tree: the graphics layer keeps a
  // MIRROR of these reasons — the mirror lives over there, and the fact that
  // it exists is recorded here, in core_save/save_rows.cpp, which says the
  // layer once "had eight refusal reasons against our nine and had no way to
  // notice". So appending one is a boundary event and the layer has to be
  // told. Old saves keep opening either way — their values stay inside the
  // widened range.

  /// NOT A VALUE, and never written to a save or read from one: the
  /// codecs range-check 0..kOrderRefusalCount-1 and this is what they check against.
  /// Values are appended BEFORE it — that is the whole rule, and it is a
  /// fact here rather than an instruction somewhere else. A length
  /// written out by hand beside an enum drifts, and four of them already
  /// had (journal_codec.cpp).
  kOrderRefusalCount,
};

/// @brief Which of the sealed funds an order unseals (resources design §6).
/// The ladder has three rungs and only the top two are sealed: the kolkhoz
/// fund is what the automation already spends, so there is nothing in it to
/// unseal.
enum class FundKind : std::uint8_t {
  /// Not a fund order. The value every other kind of order carries.
  kNone = 0,

  /// The seed fund — next year's sowing. Unsealing it risks the spring.
  kSeed,

  /// The plan reserve — what is still owed to the district out of this
  /// year's harvest. Unsealing it risks the autumn's delivery.
  kPlanReserve,

  /// The fodder fund — the working stock's year of feed grain, oats and
  /// barley (resources design §6, third rung; boss's decision of
  /// 2026-09-12). Unsealing it risks the SPRING SOWING, and risks it
  /// slowly: horses that wintered on hay alone still plough, they plough
  /// for longer, and the chairman learns the price of his decision from the
  /// calendar rather than from a window.
  ///
  /// It sits BELOW the plan on purpose: in a poor year the district takes
  /// first and the horse goes thin before the delivery falls short.
  kFodder,

  /// NOT A VALUE: the count, for the codecs' range check. Append before it.
  kFundKindCount,
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
  /// and because the four leading bytes leave a hole exactly this wide.
  ///
  /// THE ROW IS 64 BYTES SINCE 2026-09-12, not the 48 this sentence claimed
  /// until then: kUnsealFund's fund, resource and amount took it past the
  /// alignment boundary. Corrected here rather than left as a stale aside
  /// because BOTH hand-counted wire lengths stand on this prose — the save
  /// codec's tripwire and the journal's kOrderBytes — and a comment naming
  /// the old size is the second home of a fact whose first home moved.
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

  /// kUnsealFund: which fund is being opened. kNone on every other kind.
  FundKind fund = FundKind::kNone;

  /// kUnsealFund: what is being taken out of it.
  ResourceId resource;

  /// kUnsealFund: how much, in grams. THE FIGURE IS THE CHAIRMAN'S and the
  /// order carries it rather than meaning "as much as is needed": resources
  /// design §6 calls the unsealing "осознанный выбор, а не незаметная
  /// утечка", and a door that opens by itself to whatever width is wanted is
  /// the leak that sentence refuses.
  Grams amount = 0;
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
