/// @file
/// @brief ISession — the boundary of the core: the one object through which
/// a presentation drives a campaign. Time in, orders in; state, signals and
/// events out. Project phase 2, task A1 (manual/70-boundary.md).
/// @threading SINGLE_THREADED
/// Every method is called from ONE thread — the thread that created the
/// session, which is the sim thread: the step engine binds its scheduler to
/// it (core_sim/step.h), and in the game that thread is the engine's game
/// thread. Orders are issued, state is read and events are drained between
/// steps on that same thread, so the session needs no queue and no lock, and
/// the analysis may skip RACE and DEADLOCK here. This is a decision, not an
/// omission: the fast-forward norm (a game day in ≤2 s without rendering,
/// phase-2 plan §1) is met by slicing steps over frames on the game thread
/// (AdvanceUntil's budget), which is an order of magnitude cheaper than a
/// second thread with a state queue between them. Should the simulation
/// ever move to its own thread, this label changes — an event, like any
/// interface change — and the queue the architecture names (§7е, moodycamel)
/// goes behind these same signatures: IssueOrder becomes the producer side,
/// State() the consumer's snapshot. Nothing a caller writes today changes.
///
/// WHAT THE BOUNDARY IS. Architecture §4 fixes the shape: state and events
/// go down, commands come up through a queue, data crosses and objects do
/// not, the core computes and the presentation reads, and nothing calls
/// back into the core from the renderer. This header is that shape made
/// concrete, and it is deliberately small — twenty-six methods, counting
/// each overload separately, two codec functions, one factory:
///
///     time      AdvanceStep, AdvanceUntil
///     read      Stamp, State, MapSideMeters, SignalsOfUnit, SignalsOfField,
///               WhereaboutsOf, ActiveAlarms, CanBeOrdered, Workforce,
///               StockLights, WeatherForecast, WearDeadline
///     orders    IssueOrder, CancelOrder
///     events    Events, AcknowledgeEvents — the default reader;
///               OpenEventReader, Events(reader), AcknowledgeEvents(reader,
///               count), CloseEventReader — any further reader
///     record    TakeJournal, ReplaceWorld (two forms), StagedBatch
///
/// (Fourteen at first; seventeen after task A2 added the second
/// ReplaceWorld, StagedBatch and MapSideMeters; twenty-one since the event
/// log gained readers; twenty-three since task A8 added the two workforce
/// questions; twenty-four since the stock lights, twenty-five with the wear
/// deadline — all additions, which is
/// what the contract's minor number is for; 70-boundary.md §6.)
///
/// TWO CONSUMERS OF EVENTS. In the game the presentation creates and holds
/// the session and drains the event log for its HUD and its fast-forward
/// summary; the host (project phase 3, the script runtime) gets a reference
/// and subscribes its scripts to the same events — a resident died, an
/// order was refused. One window with one acknowledgement cannot serve
/// both: whoever acknowledged first would have moved the window for the
/// other, and an event would be seen once, by one of them, or by neither.
/// So the log has READERS, each with its own cursor, and holds an event
/// until every open reader has acknowledged it. The two unqualified
/// methods, Events and AcknowledgeEvents, ARE one of those readers — the
/// default one, opened with the session, never closed — so a program with
/// a single consumer is written exactly as before and behaves byte for
/// byte as before. The alternatives — the presentation forwarding events
/// to the host, or the host reading without acknowledging — both rest on a
/// call-order discipline between two other components that the core cannot
/// check and that fails silently; a decision of core with boss, 2026-09-03
/// (mailbox thread core-boundary-for-host).
///
/// The read model is WorldState itself — the core's public data, already
/// plain structs by the state-model law — plus the handful of DERIVED
/// projections the presentation must not compute for itself (architecture
/// §4: "what the core must hand over"; the living-signals catalogue,
/// manual/design/presentation/live-signals.md §11, names the sources): the
/// living signals of a unit and of a field, where a resident is, which
/// alarms stand. Those are pure functions of the state and the balance
/// tables; they are methods here because two of them need a table knob
/// (the life speed-up, the infant age), and a subsystem holds
/// configuration. Everything else the catalogue lists is a raw field of the
/// state — a pantry, a herd row, a field's phase — and is read from State()
/// as it is; the fields that later phase-2 tasks add (wear, capacity,
/// construction progress, a logistics task) arrive the same way.
///
/// ORDERS, NOT CALLS. A command does nothing; it puts a row in the order
/// book (core_common/order_state.h) and the subsystem whose rules apply
/// picks it up in its own sub-step. The session stages issued rows and
/// cancellations, and hands them to the step engine at the next
/// AdvanceStep, which applies them to `current` before phase 1 in arrival
/// order — buffer-law rule 2, and the ONE extension of ISimulation this
/// boundary asks for (manual/70-boundary.md §8: ISimulation::StageOrders).
/// The presentation never sees a subsystem, and a subsystem never sees the
/// presentation: the book is the seam, and it is state.
///
/// DETERMINISM. Same seed, same tables, same orders at the same ticks —
/// same world, bit for bit. The session stamps each issued order with the
/// completed tick it was issued after and its position in that tick's
/// batch, records both in the journal, and applies the batch in that order.
/// A journal replayed against the save it started from reproduces the
/// campaign step for step; the run tool is its first user, a bug report
/// its second. The journal is optional: a session that never calls
/// TakeJournal is exactly as deterministic, it merely cannot be replayed.
///
/// LIFETIMES, stated once. Everything a reader gets back — the state
/// reference, the spans of events and alarms — is valid until the next
/// call to AdvanceStep, AdvanceUntil or ReplaceWorld; an events span also
/// only until the next AcknowledgeEvents or CloseEventReader OF ANY READER,
/// because either may trim the log's front and move what is left. The
/// presentation copies out what it keeps and holds nothing across a step;
/// the UE side's own rule ("no pointers into the core", ue/CLAUDE.md §2)
/// is the same rule seen from the other bank.

#ifndef CORE_BOUNDARY_SESSION_H_
#define CORE_BOUNDARY_SESSION_H_

#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/calendar.h"
#include "core_common/event_state.h"
#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/order_state.h"
#include "core_common/quantities.h"
#include "core_common/stink.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "core_tables/stub_tables.h"

namespace core {

class ITableSet;  // core_tables/tables.h — the live balance tables.

// ---------------------------------------------------------------------------
// Read side: the stamp and the derived projections
// ---------------------------------------------------------------------------

/// @brief Names a completed state, so a reader can tell whether anything
/// changed since it last looked without comparing worlds.
struct StateStamp {
  /// The calendar tick of the completed state (WorldState::calendar.tick).
  Tick tick = 0;

  /// Steps completed since the session was created or the world replaced,
  /// monotone. Two stamps with equal serials name the same state; a tick
  /// alone would not, because ReplaceWorld may land on any tick.
  std::uint64_t serial = 0;
};

/// @brief The living signals of one unit — the facts the presentation draws
/// from and must not compute for itself (architecture §4; metrics design
/// §15; unit rules §15; housing design §19). Every field is derived from
/// the completed state each time it is asked for; nothing here is stored.
/// The catalogue of signals (phase-2 plan, task В3) grows this struct by
/// APPENDING fields; a field whose source system has not arrived is a
/// STUB at its neutral value and says so.
struct UnitSignals {
  UnitId unit;

  /// Members of the household that lives here (house-kind units): the
  /// laundry on the line, two pieces per resident (housing design §19).
  /// Zero for units nobody lives in.
  std::uint16_t residents_living = 0;

  /// Residents assigned to work here today — the barn crew, through the
  /// herd that stands here. A field is not a unit: its crew is
  /// FieldSignals::residents_working.
  std::uint16_t residents_working = 0;

  /// Residents of the household under the infant age (life-cycle design
  /// §1: eighteen biological months) — the diapers on the line.
  std::uint8_t infants = 0;

  /// Wear, 0..100 — UnitRow::wear as it stands, the number the layer's
  /// four keyframes blend on (architecture §7б: wear_00 / 50 / 75 / 100)
  /// and the office's mice read (office design §5). Grows since task A5
  /// (manual/73-wear-and-repair.md); 0 for a type with nothing to wear.
  Metric wear = 0.0F;

  /// Air temperature inside, degrees Celsius. STUB: equals the outdoor
  /// temperature until heating exists (heating design; a later task).
  float indoor_temperature_celsius = 0.0F;

  /// 0/1: production stopped by a kPauseUnit order (unit rules §5). Real
  /// since task A8. What the pause stops in the slice is the wear: a
  /// stopped unit does not age. Unit WORK cycles do not exist yet, so
  /// nothing else about the unit changes when this turns 1.
  std::uint8_t paused = 0;

  /// 0/1: the unit stands and does not work at all until it is restored —
  /// UnitRow::dead. Today only the start scene sets it, and the wrecked
  /// water mill of the first morning is what it is for.
  ///
  /// A SECOND FIELD BESIDE `wear` AND NOT A HIGH WEAR, for the reason the
  /// row itself gives: wear tops out at "a ruin that still works", so a
  /// hundred per cent would have told the layer to draw the mill grinding.
  std::uint8_t dead = 0;

  /// Fresh marks of children's pranks — the broken panes that do not move
  /// the wear scale (crime design §3). STUB: 0 until project phase 3.
  std::uint8_t prank_marks = 0;
};

/// @brief The living signals of one field — "people on the field or none"
/// (live-signals catalogue §7): whether the order stands or is being
/// worked. The field's own facts — phase, crop, land kind, work left — are
/// its row in State(); this is only what has to be counted across rows.
struct FieldSignals {
  FieldId field;

  /// Residents assigned to this field today (ResidentRow::work).
  std::uint16_t residents_working = 0;
};

/// @brief Where a resident is, as far as the core knows.
enum class Whereabouts : std::uint8_t {
  kUnknown = 0,  ///< No such resident, or nothing recorded (STUB value).
  kAtHome,       ///< At the household's house.
  kAtWork,       ///< At the assignment's place: a field, a herd's unit.
  kOnTheRoad,    ///< Between `from` and `to`; the leg's ticks say when.
  kAway,         ///< Out of the settlement (a later phase: trips, school).

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
  kWhereaboutsCount,
};

/// @brief The unified chronometer seen from the presentation's side (time
/// design §3): the core says "left at t0, arrives at t1, from here to
/// there", and the frame draws the walk between them. Derived on request;
/// the movement facts themselves are the labor model's (a phase-2 task
/// records departure and arrival on the assignment). STUB until then: the
/// session answers kAtHome outside the assignment's hours and kAtWork
/// inside them, never kOnTheRoad.
struct ResidentWhereabouts {
  ResidentId resident;

  Whereabouts place = Whereabouts::kUnknown;

  /// The place, by kind: the house or the barn (`unit`), the field
  /// (`field`), the herd (`herd`). Ids the place does not need stay invalid.
  UnitId unit;

  FieldId field;

  HerdId herd;

  /// For kOnTheRoad: the leg. Positions in map metres; ticks on the
  /// calendar clock. For every other place `to` is where the resident is
  /// and the rest is unused.
  Vec2 from;

  Vec2 to;

  Tick departed = 0;

  Tick arrives = 0;
};

// ---------------------------------------------------------------------------
// Fast-forward
// ---------------------------------------------------------------------------

/// @brief Where a fast-forward stops on its own (time design §1: until
/// dark, until morning, N days, until an event). No hourly preset by
/// design.
enum class FastForwardTargetKind : std::uint8_t {
  kTick = 0,      ///< Until the completed tick reaches `tick`.
  kNextSunrise,   ///< Until the next sunrise as the core lays the day out.
  kNextSunset,    ///< Until the next sunset.
  kFirstEventOf,  ///< Until an event of `event_kind` is emitted.

  /// NOT A VALUE: the number of them, for a consumer's mirror. Values are
  /// appended BEFORE it.
  kFastForwardTargetKindCount,
};

/// @brief The stop condition of one AdvanceUntil.
struct FastForwardTarget {
  FastForwardTargetKind kind = FastForwardTargetKind::kTick;

  Tick tick = 0;  ///< For kTick.

  EventKind event_kind = EventKind::kNone;  ///< For kFirstEventOf.
};

/// @brief Why AdvanceUntil returned.
enum class FastForwardOutcome : std::uint8_t {
  kTargetReached = 0,

  /// An event of EventSeverity::kInterrupting was emitted. It is among the
  /// events of the LAST step run — scan Events() back from the end to find
  /// it; the step may have emitted routine events after it, and dropping or
  /// reordering those to make it the very last element would lose facts the
  /// summary needs. The presentation lands the player in the world next to
  /// it (office design §14) and decides whether to call again.
  kInterrupted,

  /// The step budget ran out first. Call again next frame with the same
  /// target: the slicing that keeps the game thread responsive.
  kBudgetSpent,

  /// NOT A VALUE: the number of them, for a consumer's mirror. Values are
  /// appended BEFORE it.
  kFastForwardOutcomeCount,
};

struct FastForwardReport {
  FastForwardOutcome outcome = FastForwardOutcome::kTargetReached;

  std::uint32_t steps_run = 0;
};

// ---------------------------------------------------------------------------
// The journal
// ---------------------------------------------------------------------------

enum class JournalVerb : std::uint8_t {
  kIssue = 0,
  kCancel,

  /// NOT A VALUE, and never written to a save or read from one: the
  /// codecs range-check 0..kJournalVerbCount-1 and this is what they check against.
  /// Values are appended BEFORE it — that is the whole rule, and it is a
  /// fact here rather than an instruction somewhere else. A length
  /// written out by hand beside an enum drifts, and four of them already
  /// had (journal_codec.cpp).
  kJournalVerbCount,
};

/// @brief One thing the presentation did, as the session recorded it:
/// enough to do it again at the same moment. Replay is "advance until the
/// stamp's tick equals `tick`, then IssueOrder / CancelOrder in `sequence`
/// order" — the run tool does that; the session only records.
struct JournalEntry {
  /// The completed tick the action was taken after (StateStamp::tick at
  /// the time of the call).
  Tick tick = 0;

  /// Position within that tick's batch, from 0. Issue and cancel share
  /// one counter, so the batch order is total.
  std::uint32_t sequence = 0;

  JournalVerb verb = JournalVerb::kIssue;

  /// For kIssue: the row as staged (status kPending, issued_tick set).
  OrderRow order;

  /// For kIssue: the id the row was promised; for kCancel: the id cancelled.
  OrderId order_id;
};

/// @brief The eight magic bytes every journal file begins with.
inline constexpr std::string_view kJournalMagic = "KLHZJRNL";

/// @brief Encodes journal entries into bytes: the magic, kSaveFormatVersion
/// (an OrderRow is a state row, so the save format's number governs its
/// layout), the entry count, then the entries field by field with the save
/// codec's encoding rules (core_save/save.h). Deterministic: the same
/// entries give the same bytes.
/// @note A journal carries NO dictionaries and remaps nothing: definition
/// ids go out as they are. It is replayed against the save it was written
/// beside, and therefore against the same tables. A table reordered between
/// writing and replay is a case the save survives (core_save remaps by key)
/// and the journal does not — which is why a journal is a replay and
/// debugging artifact and never a second save.
std::vector<std::byte> EncodeJournal(std::span<const JournalEntry> entries);

/// @brief Decodes a journal; refuses a wrong magic or format number with
/// the reason in `error` (when non-null) and leaves `entries` untouched.
/// All or nothing, like a load.
bool DecodeJournal(std::span<const std::byte> bytes,
                   std::vector<JournalEntry>* entries,
                   std::string* error);

// ---------------------------------------------------------------------------
// Readers of the event log
// ---------------------------------------------------------------------------

/// @brief Names one reader of the session's event log — a cursor the
/// session keeps, not an entity of the world: it is never saved, never
/// journaled, and means nothing to another session. Issued by
/// ISession::OpenEventReader in call order, 1, 2, 3…, never reused within
/// a session, so a stale id names a closed reader and not a new one. The
/// default reader — the one behind the unqualified Events and
/// AcknowledgeEvents — has NO id: it cannot be addressed here, and it
/// cannot be closed. Comparison is defaulted so ids can be keys.
struct EventReaderId {
  /// 0 is "no reader" — what a default-constructed id says, and what
  /// OpenEventReader never returns.
  std::uint32_t value = 0;

  friend constexpr auto operator<=>(const EventReaderId&, const EventReaderId&) = default;
};

// ---------------------------------------------------------------------------
// The session
// ---------------------------------------------------------------------------

/// @brief Everything CreateSession needs.
struct SessionConfig {
  /// Balance tables; non-owning — the caller keeps them alive for the
  /// whole lifetime of the session, as it already does for the simulation
  /// (core_world/world.h). Read for the two knobs behind the derived
  /// signals: tables/life.csv, keys `life_speedup` and `infant_age_months`.
  /// A table set without them keeps the canonical defaults (four times the
  /// calendar, eighteen months) ONLY when the field below says so; by
  /// default a set without life.csv refuses the session by name. A present
  /// but malformed cell refuses it either way.
  const ITableSet* tables = nullptr;

  /// Whether a set WITHOUT the tables this session reads is legitimate.
  /// THE DEFAULT IS THE REFUSAL, as everywhere this choice lives in a config
  /// struct (core_tables/stub_tables.h): a caller that says nothing gets the
  /// answer that cannot quietly lie. Until 2026-09-08 the sentence above was
  /// the whole policy — a missing life.csv kept the canonical defaults in
  /// silence, which is the same silence that cost a day of measurements on
  /// the weather.
  StubTables stub_tables = StubTables::kRefused;

  /// The assembled simulation, usually CreateStandardSimulation's; owned by
  /// the session from here on. The session drives it and nothing else
  /// may: a caller that keeps calling AdvanceStep on the simulation behind
  /// the session's back desynchronises the stamp and the journal.
  std::unique_ptr<ISimulation> simulation;
};

/// @brief A running campaign as the presentation sees it. One per
/// simulation; created on the sim thread and used only there (see @file).
class ISession {
 public:
  virtual ~ISession() = default;

  // -- time -------------------------------------------------------------------

  /// @brief Runs one step: stages the orders issued and cancelled since
  /// the last step into the engine, then ISimulation::AdvanceStep — copy,
  /// orders applied before phase 1, the six phases, swap. Then moves the
  /// completed step's outbox into the event log and refreshes the alarms.
  /// Blocks until done. Game speed is the caller's business: speed changes
  /// how often this is called, never what it computes (time design §1).
  virtual void AdvanceStep() = 0;

  /// @brief Runs steps until `target` is met, an interrupting event is
  /// emitted or `step_budget` steps have run, whichever comes first — the
  /// fast-forward (time design §1). Nothing is read and nothing is drained
  /// per step: events accumulate in the log, the state is refreshed once
  /// at return, and the per-step cost is the simulation's alone, which is
  /// how the ≤2 s-per-day norm is kept.
  /// @param step_budget Maximum steps this call; 0 = no budget (the
  ///        headless tool). The game passes a per-frame budget and calls
  ///        again on kBudgetSpent. With no budget a kFirstEventOf target
  ///        runs until that event comes — which is what the caller asked
  ///        for, and why the game passes a budget.
  /// @return What stopped it and how many steps ran. steps_run may be 0
  ///         when the target is already met, which only a kTick target in
  ///         the past can be: the other three are all "the next one" and
  ///         always cost at least one step.
  /// @note The target is checked BEFORE the interrupt: a step that both
  /// reaches the target and emits an interrupting event returns
  /// kTargetReached, because the fast-forward is over either way and
  /// kInterrupted would send the caller back for a run it has finished.
  /// The event is in Events() regardless.
  /// @note Events ACCUMULATE across the whole call — nothing is drained per
  /// step, which is what keeps the per-step cost the simulation's alone — so
  /// a long unbudgeted run holds every event of every step it ran until the
  /// caller acknowledges them. A year of steps is a few thousand events, and
  /// the presentation that folds them into a summary acknowledges at once;
  /// a caller that never does is asking the session to remember a campaign.
  /// @note kFirstEventOf counts only the events of THIS call's steps — a
  /// kind already in the log from before is not what "until the first event
  /// of" asks about — and EventKind::kNone matches nothing. kNextSunrise
  /// and kNextSunset stop at the tick that CONTAINS sunrise or sunset for
  /// that day's daylight (core_common/day_window.h), the same window the
  /// working day is cut from.
  virtual FastForwardReport AdvanceUntil(const FastForwardTarget& target,
                                         std::uint32_t step_budget) = 0;

  // -- read -------------------------------------------------------------------

  /// @brief The stamp of the completed state: compare serials to know
  /// whether a re-read is needed.
  virtual StateStamp Stamp() const = 0;

  /// @brief Side of the square map in metres, from tables/map.csv — what
  /// the presentation sizes its terrain from.
  ///
  /// A METHOD AND NOT A CONSTANT, and the reason is worth the line: the core
  /// used to export `kMapSizeMeters`, the map grew from ten kilometres to
  /// twelve, and the header kept saying ten while the start layout drove
  /// past the edge. Maps differ in size, so the number belongs to the loaded
  /// data and not to the build. The one home of it is db/map.db.
  /// @return 0 when the table set declares no map. Zero is not a size: it
  ///         says the session does not know, so that a caller cannot be
  ///         handed a plausible wrong number.
  virtual float MapSideMeters() const = 0;

  /// @brief The completed state — ISimulation::CompletedState through the
  /// session. Valid until the next AdvanceStep, AdvanceUntil or
  /// ReplaceWorld; never held across them (see @file, LIFETIMES). The
  /// presentation reads tables and rows straight from it — the office's
  /// lists, the map's units and fields — and copies what it keeps.
  virtual const WorldState& State() const = 0;

  /// @brief The living signals of `unit`, derived from State() now.
  /// @return A default UnitSignals (invalid id) for a unit that does not
  ///         exist; every field at its neutral value.
  virtual UnitSignals SignalsOfUnit(UnitId unit) const = 0;

  /// @brief The living signals of `field`, derived from State() now.
  /// @return A default FieldSignals (invalid id) for a field that does not
  ///         exist.
  virtual FieldSignals SignalsOfField(FieldId field) const = 0;

  /// @brief Where `resident` is, derived from State() now.
  /// @return place == kUnknown for a resident that does not exist.
  virtual ResidentWhereabouts WhereaboutsOf(ResidentId resident) const = 0;

  /// @brief Whether `resident` could be given a work order at all — of
  /// working age, alive, with a household to start the day from.
  /// @return false for a resident that does not exist.
  /// @note The age threshold comes from life.csv through the labor
  ///       subsystem's own configuration, so this answer moves when the
  ///       table moves. That is the whole reason the question is asked here
  ///       rather than of a birthday.
  virtual bool CanBeOrdered(ResidentId resident) const = 0;

  /// @brief The two workforce numbers over the whole settlement, derived
  /// from State() now.
  virtual WorkforceCount Workforce() const = 0;

  /// @brief The conditions standing in the completed state — the roster
  /// and the fields each kind fills are core_common/alarm_state.h.
  /// Recomputed after every step and after ReplaceWorld by asking the
  /// simulation (ISimulation::CollectAlarms: the subsystems' predicates
  /// over State() with their own configuration; the session computes no
  /// rule itself), then sorted by kind and, within a kind, by the subject
  /// id (AlarmSubjectValue) — the same list for the same state whatever
  /// the row order underneath, so a panel can diff it. Each subject
  /// appears at most once per kind. Valid until the next step or
  /// ReplaceWorld. Empty for a world whose tables define nothing that can
  /// go wrong, which is what a table-less unit-test world is.
  virtual std::span<const Alarm> ActiveAlarms() const = 0;

  /// @brief The four stock lights of the completed state, ALWAYS four and
  /// always in StockKind order — food, feed, firewood, seed.
  ///
  /// Recomputed after every step and after ReplaceWorld by asking the
  /// simulation (ISimulation::CollectStockForecast); the session computes no
  /// rule and holds no threshold, exactly as with alarms.
  ///
  /// A KIND NOBODY ANSWERED COMES BACK AS StockLight::kNoData WITH A REASON,
  /// never as a gap and never as green. Firewood is that kind today: no
  /// consumption rate for it exists in any table, and the reason says so, so
  /// that the presentation can tell "the design has not given the numbers"
  /// from "the core did not finish the sum". A missing light drawn as green
  /// would teach the player to trust it and would lie once — in the first
  /// winter, which is the winter the whole mechanism exists for.
  ///
  /// The number beside the colour is the point of the second reader: the
  /// panel compares it with the date and paints, the story reads the days
  /// and decides whether to set a quest today or wait. One calculation, two
  /// readings — never two calculations (office design §5).
  ///
  /// Valid until the next step or ReplaceWorld.
  virtual std::span<const StockForecast> StockLights() const = 0;

  /// @brief The weather of the next three days: tomorrow, the day after,
  /// the third. Always three, always in that order.
  ///
  /// THREE IS THE DESIGN'S NUMBER AND NOT A LIMIT OF THE MODEL. The
  /// generator can be asked about any day — the weather is a pure function
  /// of (world_seed, day), so a day ahead costs what a day behind costs —
  /// and the design spends exactly three, because three is what it promises
  /// the player: a threat that can be seen coming is a decision, and one
  /// that cannot is the "punishment for the unforeseeable" the red line
  /// forbids (farming design §6).
  ///
  /// TWO NAMES PER DAY AND NO NUMBERS. The strip draws an icon for the day
  /// and, beside it, an icon for the wind; temperature and cloud are not
  /// here because nothing is drawn from them, and a value on the boundary
  /// that nobody draws is a value somebody eventually derives something
  /// from.
  ///
  /// THE WIND IS A SECOND ICON AND NOT A NINTH NAME. A windy clear day would
  /// otherwise have to be called "wind" and lose the "clear" — and the wind
  /// is what the quest layer may order separately for exactly that reason
  /// (Кожаный босс, 2026-09-05).
  ///
  /// IT USED TO BE PRECIPITATION, and the quest layer is what changed it: a
  /// quest may order a named day, an ordered day must enter this forecast
  /// like any other, and "rain" and "thunderstorm" are one Precipitation and
  /// two names. A promise to the player does not distinguish who chose the
  /// storm, so what is ordered and what is forecast have to be the same
  /// alphabet.
  ///
  /// It is a FORECAST, not a promise about the run: it is exactly what those
  /// days will be, because they are already determined. There is no model
  /// error to hide and none is claimed.
  ///
  /// Valid until the next step or ReplaceWorld.
  virtual std::span<const DayForecast> WeatherForecast() const = 0;

  /// @brief How long `unit` has before its wear reaches the end of the
  /// scale, at the rate it is wearing today (unit rules §15).
  ///
  /// THE SAME NUMBER FOR BOTH READERS, and that is why it is here rather
  /// than in the panel. The HUD's wear layer paints from it; the story reads
  /// the days and can set a quest WHILE THE HOUSE IS STILL STANDING — the
  /// difference between reacting and warning. `wear = 92` does not say "four
  /// days", and a threshold on the level paints two units alike whose terms
  /// and paces differ.
  ///
  /// Answers with days, or with one of the three refusals that are not the
  /// same refusal (core_common/deadline.h): never, not applicable, no data.
  /// A unit that does not exist is kNotApplicable.
  /// @note Between steps; the answer describes State().
  virtual Deadline WearDeadline(UnitId unit) const = 0;

  /// @brief How badly it stinks at a point of the map, in the four bands the
  /// design speaks in (water design §4). Two questions, two doors:
  ///
  ///   * `StinkFullAt` — AT WORST, the zone at its full radius. This is what
  ///     a PLAN is judged against: the preview drawn under a unit being
  ///     placed, the houses that turn red inside it, the refusal to raise a
  ///     dwelling where there is nothing to breathe. The design demands the
  ///     full radius here in so many words, so that the build-up cannot
  ///     mislead the player into a spot that will stink later.
  ///   * `StinkNowAt` — TODAY, the zone as it has actually grown. This is
  ///     what a NOSE meets: the chairman coughs in a strong band and winces
  ///     in a medium one, the flies are drawn to bad air, and a scene script
  ///     asks whether it smells by this house.
  ///
  /// ONE NUMBER COULD NOT ANSWER BOTH without lying to one of them, which is
  /// why they are two calls and not a call with a flag.
  ///
  /// A BAND AND NOT A NUMBER, because all four readers are live signals and
  /// none of them wants a percentage: publishing one would hand the
  /// threshold to the reader, and a threshold chosen on the far side of the
  /// seam is a second answer to a mechanic, living where nobody will find
  /// it.
  ///
  /// Wind does not enter either answer and never will — the design refuses a
  /// wind rose by decision, not by omission.
  /// @note Between steps; the answers describe State().
  virtual StinkStrength StinkFullAt(Vec2 point) const = 0;

  virtual StinkStrength StinkNowAt(Vec2 point) const = 0;

  /// @brief How tall this person is, in METRES. 0 for a child and for
  /// anybody who is not there.
  ///
  /// THE ONE DERIVED NUMBER OF THE FIGURE, and the core computes it because
  /// the core is the only place that holds both halves: the person's own
  /// deviation, which is a fraction, and the base of their sex, which is a
  /// table row. The graphics layer does not read this — it scales bone by
  /// the FRACTION — and the story layer cannot: a script sees neither.
  ///
  /// WHAT IT IS FOR, by name: "is the wife taller than her husband". The
  /// design asks the question and a quest stands on the answer, so somebody
  /// has to compare two people; of the three places that could, two are the
  /// wrong side of a seam (boss, 2026-09-06).
  ///
  /// ADULTS ONLY, said out loud rather than left to be discovered: the world
  /// carries a base height for a man and for a woman and none for the steps
  /// of childhood, so a child's height in metres is a question nobody can
  /// answer. The FRACTION is valid at every age — it is a fact about the
  /// person, not about their present size — and it is in the state for
  /// anyone who wants it.
  /// @note Between steps; the answer describes State().
  virtual float ResidentHeightMeters(ResidentId resident) const = 0;

  // -- orders -----------------------------------------------------------------

  /// @brief Stages an order for the next step. The session fills the
  /// bookkeeping (status kPending, refusal kNone, issued_tick = the
  /// current stamp's tick), records the entry in the journal and promises
  /// the id: the one the engine WILL give the row when it appends it —
  /// predictable because the engine is the book's only appender and
  /// appends in staging order (order_state.h).
  /// @param order The request: `kind` and the targets that kind reads.
  ///        The session checks only SHAPE — kind is not kNone, the ids the
  ///        kind requires are not the invalid id; whether the order makes
  ///        sense is the consumer's verdict and comes back as an event
  ///        (kOrderAccepted / kOrderRefused) after the step.
  /// @return The promised id; the invalid id when the shape check failed,
  ///         in which case nothing was staged or journaled.
  virtual OrderId IssueOrder(const OrderRow& order) = 0;

  /// @brief Cancels an order. Both the staged-and-not-yet-applied case and
  /// the one already in the book are STAGED AS A CANCELLATION — nothing is
  /// ever dropped from the batch — and the engine marks the row before the
  /// next phase 1, which succeeds only while it is kPending or kAccepted
  /// (order_state.h). An order issued and cancelled between the same two
  /// steps therefore still becomes a row: it appears kCancelled and the
  /// events slot removes it in the step it was born in. Either way the
  /// journal records the cancel.
  /// @note Dropping it from the batch instead would break the two promises
  /// above it. IssueOrder hands out the id the row WILL carry, predictable
  /// only because the engine appends in staging order — remove one entry
  /// and every id promised after it names a different order, which is the
  /// one thing entity ids exist to prevent. And the authoritative outcome
  /// of a cancel is its EVENT: with no row there is no kOrderCancelled, so
  /// a caller written to the paragraph below would wait for ever, and a
  /// cancel would have two protocols instead of one. The cost of keeping
  /// it — a row that lives for one step — buys a single path for both
  /// cases; a boss decision of 2026-08-31, and no trace survives it (the
  /// row never reaches a save, so "cancelled without trace", time design
  /// §11, still holds).
  /// @return false when the id is unknown or the row (as of State()) is
  ///         already kActive or terminal; nothing is staged then. true
  ///         means staged; the authoritative outcome is the event
  ///         (kOrderCancelled, or nothing if the consumer started it in
  ///         the same step — the race the design accepts, time design §11).
  virtual bool CancelOrder(OrderId order) = 0;

  // -- events -----------------------------------------------------------------
  //
  // The log is one sequence, appended by every step; a READER is a cursor
  // into it. Events(…) is the window from a reader's cursor to the end,
  // AcknowledgeEvents(…) moves that cursor forward, and the log keeps every
  // event that at least one open reader has not yet acknowledged — its
  // front is trimmed to the slowest cursor, and never further. So no reader
  // can lose an event to another reader, and the number of events HELD is
  // bounded by the slowest reader alone: a reader that stops acknowledging
  // holds the whole log from that point on. That is the one cost of the
  // design, stated and not enforced — close a reader that is done, and
  // drain the default reader if nothing else does (see OpenEventReader).
  // The MEMORY the log occupies is bounded more loosely, by the high-water
  // mark of the session: trimming erases from the front and the vector
  // keeps its capacity, which is the right trade for a container refilled
  // every step and never the reason to shrink it.

  /// @brief The default reader's window: everything emitted since ITS last
  /// acknowledgement, oldest first, across as many steps as have run — the
  /// material of a HUD line (kNotable), of the summary after a fast-forward
  /// (all of them), of the interruption (kInterrupting, among the last
  /// step's events, searched from the end). Valid until the next step,
  /// ReplaceWorld, or an AcknowledgeEvents or CloseEventReader of any
  /// reader (see @file, LIFETIMES).
  virtual std::span<const SimEvent> Events() const = 0;

  /// @brief Moves the default reader's cursor past the first `count` events
  /// of Events() — the ones the presentation has shown or folded into its
  /// summary. `count` above the window's size acknowledges the whole
  /// window. Whether the acknowledged events are freed depends on the
  /// other readers; whether they are gone from THIS window does not.
  virtual void AcknowledgeEvents(std::size_t count) = 0;

  /// @brief Opens a further reader of the log, with its cursor at the
  /// CURRENT END: it will see what is emitted from now on, and nothing
  /// older — an unacknowledged backlog belongs to the readers that were
  /// open when it accrued, and a subscriber does not inherit another's
  /// window. Ids are issued in call order, 1, 2, 3…, and never reused.
  /// Readers survive ReplaceWorld (they are the consumer's subscriptions,
  /// not the world's state); the log itself is dropped by it, so every
  /// cursor stands at the start of the new, empty log afterwards.
  /// @note The default reader is always open and holds the log like any
  ///       other. A program whose only consumer is a reader opened here —
  ///       a headless host run — must therefore also drain the default one
  ///       (AcknowledgeEvents(Events().size()) after each step) or the log
  ///       grows for the whole run; in the game the presentation drains it
  ///       and no one else needs to. A program with a single consumer keeps
  ///       using the default reader and never calls this.
  virtual EventReaderId OpenEventReader() = 0;

  /// @brief That reader's window: from its cursor to the end of the log,
  /// oldest first, with the same content and the same lifetime rule as
  /// Events(). Two readers' windows overlap wherever both have yet to
  /// acknowledge — the same SimEvent objects, seen from two cursors.
  /// @return An empty span for an id that is not open (never issued, or
  ///         closed); asserted in Debug as a caller error.
  virtual std::span<const SimEvent> Events(EventReaderId reader) const = 0;

  /// @brief Moves that reader's cursor past the first `count` events of
  /// Events(reader); `count` above the window's size acknowledges the
  /// whole window. Only this reader's window shrinks; every other reader
  /// still sees what it has not acknowledged itself.
  /// @note An id that is not open is a caller error: asserted in Debug,
  ///       ignored otherwise. Nothing is acknowledged on anyone's behalf.
  virtual void AcknowledgeEvents(EventReaderId reader, std::size_t count) = 0;

  /// @brief Closes a reader: its window is gone, the events only it was
  /// holding may be freed, and its id names nothing from now on — it is
  /// not reissued. The consumer that opened a reader closes it when it is
  /// done, and before it lets go of the session.
  /// @note Closing an id that is not open is a caller error: asserted in
  ///       Debug, ignored otherwise. The default reader cannot be closed.
  virtual void CloseEventReader(EventReaderId reader) = 0;

  // -- record -----------------------------------------------------------------

  /// @brief Moves the journal out: every IssueOrder and CancelOrder since
  /// the session was created, the world replaced or the journal last
  /// taken, in order. The presentation writes it beside its save with
  /// EncodeJournal; a session that never calls this keeps growing it by a
  /// few dozen bytes per order, which is nothing for a campaign and
  /// something for a bot run — the run tool takes it every year.
  virtual std::vector<JournalEntry> TakeJournal() = 0;

  /// @brief Replaces the world entirely — the way a load lands
  /// (core_save::LoadWorldFromFile, then this). Forwards to
  /// ISimulation::ResetWorld; drops the staged batch and the event log —
  /// both described the world being replaced — while every open event
  /// reader stays open with its cursor at the start of the now empty log
  /// (OpenEventReader); and recomputes the alarms
  /// for the new one, because ActiveAlarms names the conditions standing in
  /// State() and must never describe a different world; starts a new stamp
  /// serial from 0; the journal is NOT cleared — a replay that spans a load is the caller's to cut.
  /// The loaded world's order book is whatever the save carried; its outbox is empty by the save
  /// format's rule.
  virtual void ReplaceWorld(const WorldState& initial) = 0;

  /// @brief ReplaceWorld, and then the batch the save carried beside the
  /// world becomes the staged batch — as if every IssueOrder and
  /// CancelOrder of it had just been made again, in order, with the ids
  /// they were promised. That is exactly what a campaign saved on pause
  /// after the day's orders looks like when it resumes (order_state.h,
  /// StagedOrders). The journal records nothing for them: they were
  /// recorded when they were first issued, in the journal that went with
  /// that save.
  /// @pre `staged` was saved with `initial`: its issued rows are waiting for
  ///      the ids [next_id_value, next_id_value + issued.size()), and every
  ///      id it cancels is either a row of that world's order book or one of
  ///      those promises. Asserted in Debug; in Release a batch from another
  ///      world is applied as it is, and every id of it then names whatever
  ///      entity of the loaded world happens to wear that number.
  /// @note `staged` MAY alias StagedBatch(): the implementation takes its
  ///       copy before the reset, so ReplaceWorld(world, StagedBatch()) — the
  ///       natural spelling of "reload the world and keep what is staged" —
  ///       does keep it. Nothing else survives the reset.
  virtual void ReplaceWorld(const WorldState& initial, const StagedOrders& staged) = 0;

  /// @brief What is staged and not yet applied — the batch the next step
  /// will hand to the engine. A save made between steps writes this beside
  /// State() (core_save::EncodeWorld with a batch) so that nothing the
  /// player ordered on pause is lost; a session with nothing staged returns
  /// an empty batch. Valid until the next IssueOrder, CancelOrder, step or
  /// ReplaceWorld.
  virtual const StagedOrders& StagedBatch() const = 0;
};

/// @brief Creates the session over an assembled simulation.
/// @pre config.tables != nullptr and config.simulation != nullptr — both
///      asserted in Debug; the default-constructed config is a template.
/// @note Create it on the thread that created the simulation and drive it
/// only from there (see @file). The session reads its two knobs from the
/// tables once, here; a missing life table means the documented defaults,
/// a malformed one is refused like every subsystem factory refuses.
/// @return nullptr on refusal; the reason is logged.
std::unique_ptr<ISession> CreateSession(SessionConfig config);

}  // namespace core

#endif  // CORE_BOUNDARY_SESSION_H_
