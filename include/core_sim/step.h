/// @file
/// @brief The step-cycle contract: phase order, barriers, double buffering.
/// @threading SINGLE_THREADED
/// The label describes the public surface: the sim thread calls
/// AdvanceStep() and reads CompletedState() between calls, and nothing here
/// is reentrant. Worker threads exist only inside the engine implementation
/// and touch user code solely through IParallelPhase::RunItemRange under the
/// buffer law below; a phase implementation marked for a parallel slot must
/// be safe under exactly that law and nothing more. The implementation's
/// scheduler internals deliberately trip the analysis scoping's
/// label-vs-code check, so the engine module always gets the full
/// RACE/DEADLOCK analysis despite this label — the safe direction.
///
/// One step advances game time by one tick and runs SIX phases in a fixed
/// order with a barrier after each (architecture, §7е). Sequential slots run
/// on the sim thread; parallel slots are split over rows of that phase's unit
/// of parallelism (state model doc, §4). The order never changes at run time
/// and is not configurable — determinism rests on it.
///
/// THE BUFFER LAW — the one set of rules every phase obeys:
///   1. Two WorldState buffers exist: `previous` (the completed last step,
///      immutable for the whole step) and `current` (being built).
///   2. At step start the engine makes `current` an exact copy of `previous`,
///      and then — before phase 1, in arrival order — applies what the
///      boundary staged (StageOrders below). A command from the presentation
///      is a ROW OF THE ORDER BOOK, `WorldState::orders`: issued rows are
///      appended, cancellations marked (core_common/order_state.h,
///      manual/70-boundary.md). In the same place the outbox
///      `WorldState::step_events` is emptied — it describes the step just
///      completed, and the step being built fills it anew.
///   3. A sequential phase may read `previous` and `current` freely and write
///      any block of `current` it owns.
///   4. A parallel phase invocation owns items [begin_item, end_item) of the
///      phase's unit of parallelism — and with each item every row that
///      BELONGS to it, in any table (a family item owns its family row and
///      the resident rows of its members). In `current` it may write only
///      what it owns, and may read what it owns plus blocks finalized by
///      earlier phases of this step (e.g. calendar and weather after
///      phase 1). The ONE legal read of an unowned current row is a field
///      that no parallel phase writes — such as ResidentRow::family, used to
///      discover membership: the sequential decisions slot may move a
///      resident to another household, but nothing moves while a parallel
///      phase runs. Any other field of an unowned row is being written
///      concurrently and must come from `previous`. No locks, no atomics.
///   5. Parallel phases keep no cross-row accumulators; any aggregate over
///      rows is computed later, sequentially, in row order (float determinism
///      rule of the state model).
///   6. Rows are appended or removed only in sequential phases. A parallel
///      phase never changes the shape of any table.
///   7. The sequential world RNG advances only in sequential phases; parallel
///      code draws counter-style from (world_seed, tick, entity id).
///   8. After phase 7 the buffers swap; the just-built state becomes the
///      completed one.
///
/// Under rules 4–7 the result cannot depend on worker count, chunk size or
/// which worker ran which chunk: writes are disjoint and everything read is
/// immutable while it is read. That is why the single-thread run is required
/// to match the multi-thread run bit for bit — a divergence is a violated
/// rule, not noise (core rules, §10).

#ifndef CORE_SIM_STEP_H_
#define CORE_SIM_STEP_H_

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/deadline.h"
#include "core_common/ids.h"
#include "core_common/labor_state.h"
#include "core_common/order_state.h"
#include "core_common/stock_forecast.h"
#include "core_common/world_state.h"

namespace core {

// ---------------------------------------------------------------------------
// The six phases
// ---------------------------------------------------------------------------

/// @brief The phases of one simulation step, in their fixed execution order.
/// The enum value is the execution index. The unit of parallelism of each
/// parallel slot is fixed by the state model (manual/52-state-model.md, §4)
/// and repeated in StepPhaseSet's field docs.
enum class StepPhase : std::uint8_t {
  kTimeAndWeather = 0,  ///< Sequential. Advances calendar and weather.
  kNeeds = 1,           ///< Parallel over families: food, rest, cold, mood.
  kDecisions = 2,       ///< Sequential. Assignments, births/deaths, structure.
  kProduction = 3,      ///< Parallel over fields: cycles, growth.
  // kLogistics WAS 4 AND IS GONE (2026-09-05), with the slot it named in
  // StepPhaseSet below — the reasons are written out there. THE ROSTER
  // OUTLIVED THE SLOT BY A DAY: the removal was made where it was noticed,
  // in the struct, and this enum twenty lines above it kept a phase the
  // engine no longer runs, kept the count at seven, and the unit test kept
  // guarding the seven. A contract nobody executes still lies to whoever
  // reads it, and a static_assert on the wrong number is a guard pointed at
  // nothing (found by the delivery's own analysis pass, 2026-09-05).
  //
  // Renumbering is safe HERE, and only because this enum is not carried in a
  // save and not mirrored across a build boundary: the value is the
  // execution index, and the execution order genuinely changed. When a model
  // of goods in flight brings the phase back it comes back at the end of the
  // roster, and its return is an EVENT.
  kMetrics = 4,  ///< Parallel over families: satisfaction aggregates.
  kEvents = 5,   ///< Sequential. Checks, incidents, extension point.
};

inline constexpr std::uint32_t kStepPhaseCount = 6;

// ---------------------------------------------------------------------------
// Phase plug-in contracts
// ---------------------------------------------------------------------------

/// @brief Contract of a phase that runs on the sim thread, alone.
/// One implementation fills one sequential slot of StepPhaseSet. The engine
/// calls RunSequential exactly once per step, between the barriers of the
/// neighboring phases.
class ISequentialPhase {
 public:
  virtual ~ISequentialPhase() = default;

  /// @brief Runs the phase's whole work for this step.
  /// @param previous The completed last step; immutable, read anything.
  /// @param current  The step being built; read and write per the buffer law
  ///                 (rules 3, 6, 7 of the file header).
  virtual void RunSequential(const WorldState& previous, WorldState& current) = 0;
};

/// @brief Contract of a phase whose work is split over rows of one table.
/// One implementation fills one parallel slot of StepPhaseSet. Per step the
/// engine calls ParallelItemCount once, then covers [0, count) with
/// RunItemRange invocations from its workers — each range exactly once,
/// ranges disjoint, chunking chosen by the engine. Correctness must not
/// depend on chunk boundaries or on which worker runs which chunk; under the
/// buffer law it cannot.
class IParallelPhase {
 public:
  virtual ~IParallelPhase() = default;

  /// @brief Number of parallel items this step: the row count of the phase's
  /// unit of parallelism, read from `current` — the step being built. Its
  /// table shapes are final for this phase: parallel phases never change
  /// shape (buffer-law rule 6), and the sequential phases that run earlier
  /// in the step (e.g. demography in the decisions slot) have already made
  /// their structural changes. Counting from the previous step would
  /// mis-size any phase that runs after a structural one.
  virtual std::uint32_t ParallelItemCount(const WorldState& current) const = 0;

  /// @brief Processes rows [begin_item, end_item) of the phase's table.
  /// @param previous The completed last step; immutable, read anything.
  /// @param current  The step being built. Write only the owned rows; read
  ///                 the owned rows and blocks finalized by earlier phases
  ///                 (buffer-law rules 4, 5, 7).
  /// @note Called from worker threads. No locks, no atomics, no shape
  ///       changes, no cross-row accumulation, no sequential RNG.
  virtual void RunItemRange(const WorldState& previous,
                            WorldState& current,
                            std::uint32_t begin_item,
                            std::uint32_t end_item) = 0;
};

// ---------------------------------------------------------------------------
// Wiring
// ---------------------------------------------------------------------------

/// @brief The six phase implementations of one simulation, by slot.
/// Typed by slot so a parallel implementation cannot land in a sequential
/// slot or vice versa. Pointers are non-owning: the wiring code (stage 1,
/// task O2) owns the subsystem objects and must keep them alive for the
/// simulation's lifetime. Every slot must be filled — a subsystem that does
/// not exist yet plugs in a STUB implementation, never a null (plan, §11а).
struct StepPhaseSet {
  ISequentialPhase* time_and_weather = nullptr;

  IParallelPhase* needs = nullptr;

  ISequentialPhase* decisions = nullptr;

  IParallelPhase* production = nullptr;

  // THE LOGISTICS SLOT WAS HERE, AND IT IS GONE (2026-09-05). It ran every
  // tick over zero items because transport did not become a subsystem — it
  // became the eighth kind of work (phase-2 plan A4, decision 155), so
  // nothing ever travelled between units for a phase to advance.
  //
  // A PHASE IS WORK, NOT A PLACE. An empty one still costs its barrier —
  // "everybody finished, then onward" — and the barrier is the expensive
  // half; what it was waiting for was nothing at all. And from outside a
  // phase that runs and does nothing cannot be told from one that is
  // broken, which is the same shape as a guard no test ever reaches.
  //
  // Keeping the slot for a future model of goods in flight would have been
  // a stub with no named gap behind it — the shadow of a stub rather than
  // one. When such a model arrives the phase comes back, and its return is
  // an EVENT, the way a change of a threading label is (root rules §10).
  IParallelPhase* metrics = nullptr;

  ISequentialPhase* events = nullptr;
};

// ---------------------------------------------------------------------------
// The simulation engine
// ---------------------------------------------------------------------------

/// @brief The step engine: owns the two state buffers and turns the crank.
/// Constructed by a factory of the core_sim implementation (stage 1, task O2)
/// from an initial WorldState, a StepPhaseSet and a worker count. A worker
/// count of 1 is the mandatory verification mode: its results must equal any
/// other worker count's exactly, and the balance runs compare the two.
class ISimulation {
 public:
  virtual ~ISimulation() = default;

  /// @brief Runs one full step: copy forward, phases 1–6 with barriers, swap.
  /// Advances game time by exactly one tick (kTicksPerDay ticks make a day —
  /// core_common/calendar.h). Blocks until the step is complete.
  virtual void AdvanceStep() = 0;

  /// @brief Stages what the next AdvanceStep applies to `current` before
  /// phase 1, in this order: `issued` rows appended to the order book (ids
  /// issued by the table, in order), then `cancelled` ids marked kCancelled
  /// where the row is kPending or kAccepted. Buffer-law rule 2.
  /// @note Called between steps on the sim thread; the spans are copied, so
  /// the caller's storage may go away at once. Repeated calls accumulate in
  /// call order and are applied together by the next step. ResetWorld drops
  /// whatever is staged — those rows named entities of the replaced world.
  virtual void StageOrders(std::span<const OrderRow> issued,
                           std::span<const OrderId> cancelled) = 0;

  /// @brief The last completed state.
  /// Valid until the next AdvanceStep() or ResetWorld() call. This is what
  /// tests, the balance run and (later, through the boundary queue) the
  /// presentation read between steps.
  virtual const WorldState& CompletedState() const = 0;

  /// @brief Replaces the world entirely: both buffers become `initial`.
  /// The way tests and the balance run seed or rewind a world. In-game state
  /// changes go through commands at the step boundary, never through this.
  virtual void ResetWorld(const WorldState& initial) = 0;

  /// @brief Appends every alarm standing in CompletedState() to `alarms` —
  /// the union of the subsystems' predicates, asked in the fixed order of
  /// the decisions slot (manual/54-modules.md §3: labor, residents,
  /// production, construction), each over the completed state with its own
  /// configuration. The SECOND extension of this contract the boundary
  /// asked for (StageOrders was the first, manual/70-boundary.md §8), and
  /// for the same reason: what the presentation needs and only the
  /// subsystems can compute. A pure read; nothing is stored, and asking
  /// twice on the same state appends the same alarms twice.
  /// @note Called between steps on the sim thread. The order of the result
  ///       is the fan-out order and then each predicate's own — the caller
  ///       sorts (core_boundary/session.h promises kind, then subject id).
  virtual void CollectAlarms(std::vector<Alarm>& alarms) const = 0;

  /// @brief Whether `resident` could be given a work order at all, asked of
  /// the subsystem that owns the rule (task A8). Fans out exactly as
  /// CollectAlarms does, and for the same reason: the answer belongs to
  /// whoever applies it every morning, not to whoever wants to draw it.
  /// @note Called between steps on the sim thread.
  virtual bool CanBeOrdered(ResidentId resident) const = 0;

  /// @brief How many of the settlement can be put to work, and how many of
  /// those are standing about at this moment.
  /// @note Called between steps on the sim thread, like its neighbours.
  virtual WorkforceCount Workforce() const = 0;

  /// @brief Appends every stock light the wired subsystems own, in the
  /// fan-out order of the decisions slot. Fans out exactly as CollectAlarms
  /// does and for the same reason: the forecast belongs to whoever owns the
  /// consumption, not to whoever wants to draw it.
  ///
  /// A KIND NOBODY ANSWERS IS NOT MISSING — it is unanswered, and the
  /// session says so with StockLight::kNoData rather than leaving a gap for
  /// a reader to fill with a default. Firewood is that kind today: no
  /// consumption rate for it exists anywhere yet. When one arrives, the
  /// subsystem that owns it starts answering and the light comes on with no
  /// change here.
  /// @note Called between steps on the sim thread.
  virtual void CollectStockForecast(std::vector<StockForecast>& lights) const = 0;

  /// @brief How long this unit has before its wear reaches the end of the
  /// scale, at today's rate. Fans out to core_construction, which owns the
  /// rate; the bare engine knows no subsystems and answers kNoData.
  /// @note Called between steps on the sim thread.
  virtual Deadline WearDeadline(UnitId unit) const = 0;

  /// @brief The precipitation of the next `into.size()` days, tomorrow
  /// first, filled in place.
  ///
  /// IT IS A QUERY AND NOT A MEMORY. The weather is a pure function of
  /// (world_seed, day) — no history is kept anywhere — so the days ahead are
  /// simply evaluated, exactly as the days behind would be. That is what
  /// makes a forecast free here and what made it worth building the
  /// generator's memory into the generator rather than into an accumulator
  /// (time_system.cpp).
  ///
  /// THE DESIGN'S LIMIT IS THREE DAYS and it belongs to the design, not to
  /// this signature: nothing here stops a caller asking for ten, and the
  /// boundary is where the three is spent (session.h).
  ///
  /// The bare engine has no weather and leaves every entry kNone — the same
  /// answer a table-less world gives.
  /// @note Called between steps on the sim thread.
  virtual void CollectPrecipitationForecast(std::span<Precipitation> into) const = 0;
};

/// @brief Creates the step engine over an initial world.
/// @param initial      The starting world; copied into both buffers.
/// @param phases       One implementation per slot, all six non-null; the
///                     caller owns the phase objects and keeps them alive for
///                     the engine's lifetime (the set itself is copied).
/// @param worker_count 0 = one worker per hardware core minus one; 1 = the
///                     mandatory verification mode. Results are identical for
///                     every value — see the buffer law above.
/// @note Create the engine on the thread that will drive it: the scheduler
/// binds its thread 0 to the creating thread, and AdvanceStep must be called
/// from that same (sim) thread.
/// Implemented in core_sim (stage 1, task O2). Most callers want the wired
/// core_world factory instead (core_world/world.h); this one exists for unit
/// tests of the engine itself and for custom phase sets.
std::unique_ptr<ISimulation> CreateStepEngine(const WorldState& initial,
                                              const StepPhaseSet& phases,
                                              std::uint32_t worker_count);

}  // namespace core

#endif  // CORE_SIM_STEP_H_
