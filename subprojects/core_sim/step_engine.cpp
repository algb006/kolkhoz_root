// Implementation of the step engine (include/core_sim/step.h, task O2): two
// state buffers, six phases with barriers, the enkiTS worker pool and the
// mandatory single-worker verification mode.
//
// Concurrency lives HERE and only here: the module's public surface is
// single-threaded (step.h @threading), and worker threads touch user code
// solely through IParallelPhase::RunItemRange under the buffer law. The
// scheduler is the sanctioned enkiTS; no raw std::thread, no locks in phase
// code.

#include <cassert>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "TaskScheduler.h"
#include "core_common/order_state.h"
#include "core_common/state_table.h"
#include "core_common/state_table_ops.h"
#include "core_sim/step.h"

namespace core {
namespace {

/// Maps the public worker-count contract to a live thread count:
/// 0 = one worker per hardware core minus one (leaving a core for the rest
/// of the game), never less than 1; anything else is taken literally.
std::uint32_t EffectiveWorkerCount(std::uint32_t requested) {
  if (requested != 0) {
    return requested;
  }
  const std::uint32_t hardware = enki::GetNumHardwareThreads();
  return hardware > 1 ? hardware - 1 : 1;
}

/// Bridges one parallel slot to the scheduler: enkiTS splits [0, m_SetSize)
/// into disjoint partitions and calls ExecuteRange from its workers. Under
/// buffer-law rules 4-7 the result is independent of the partitioning, so
/// scheduler chunking cannot influence the outcome.
class ParallelPhaseTask final : public enki::ITaskSet {
 public:
  void Configure(IParallelPhase& phase,
                 const WorldState& previous,
                 WorldState& current,
                 std::uint32_t item_count) {
    phase_ = &phase;
    previous_ = &previous;
    current_ = &current;
    m_SetSize = item_count;
  }

  /// NOEXCEPT, AND THAT IS THE WHOLE FIX FOR A HANG THAT HAD NO GUARD.
  ///
  /// An exception leaving RunItemRange used to unwind out through the
  /// scheduler's worker loop: `task_` stayed live on the workers, the
  /// barrier in WaitforTask was never satisfied, and the next step added
  /// the same task_ to the pipe again — in Release, with no assert to
  /// catch it, the pending counter walks below zero and the step hangs
  /// forever. A simulation that stops answering says nothing about why.
  ///
  /// The hazard is the CROSSING, not the phases: the project already
  /// forbids exceptions in hot code (root rules §4), and this makes that
  /// rule a fact at the one place where breaking it costs a deadlock
  /// instead of a stack trace. Declared here rather than on
  /// IParallelPhase::RunItemRange on purpose — putting it on the contract
  /// would make every phase author answer for bad_alloc, which is a
  /// decision about the whole engine and not this boundary's to take.
  ///
  /// An override may be more restrictive than the virtual it overrides;
  /// enki's own ExecuteRange is not noexcept, and that is exactly the seam
  /// this closes.
  void ExecuteRange(enki::TaskSetPartition range, std::uint32_t /*threadnum*/) noexcept override {
    phase_->RunItemRange(*previous_, *current_, range.start, range.end);
  }

 private:
  IParallelPhase* phase_ = nullptr;

  const WorldState* previous_ = nullptr;

  WorldState* current_ = nullptr;
};

class StepEngine final : public ISimulation {
 public:
  StepEngine(const WorldState& initial, const StepPhaseSet& phases, std::uint32_t worker_count)
      : previous_(initial),
        current_(initial),
        phases_(phases),
        worker_count_(EffectiveWorkerCount(worker_count)) {
    assert(phases_.time_and_weather != nullptr && phases_.needs != nullptr &&
           phases_.decisions != nullptr && phases_.production != nullptr &&
           phases_.metrics != nullptr && phases_.events != nullptr);
    if (worker_count_ > 1) {
      scheduler_.Initialize(worker_count_);
    }
  }

  void AdvanceStep() override {
    // Buffer law, rule 2: current starts as an exact copy of previous, then
    // takes what the boundary staged, before any phase sees it.
    current_ = previous_;
    ApplyStaged();
    phases_.time_and_weather->RunSequential(previous_, current_);
    RunParallelPhase(*phases_.needs);
    phases_.decisions->RunSequential(previous_, current_);
    RunParallelPhase(*phases_.production);
    RunParallelPhase(*phases_.metrics);
    phases_.events->RunSequential(previous_, current_);
    // Rule 8: the just-built state becomes the completed one.
    std::swap(previous_, current_);
  }

  void StageOrders(std::span<const OrderRow> issued, std::span<const OrderId> cancelled) override {
    staged_issued_.insert(staged_issued_.end(), issued.begin(), issued.end());
    staged_cancelled_.insert(staged_cancelled_.end(), cancelled.begin(), cancelled.end());
  }

  const WorldState& CompletedState() const override { return previous_; }

  void ResetWorld(const WorldState& initial) override {
    previous_ = initial;
    current_ = initial;
    // Staged rows name residents, units and fields of the world being
    // replaced; carrying them over would aim them at whatever id happens to
    // sit there now. A reset is a new campaign or a load — the book that
    // comes with it is the whole truth.
    staged_issued_.clear();
    staged_cancelled_.clear();
  }

  /// The engine owns no subsystem and therefore no predicate: it is a step
  /// machine over a phase set, and which conditions matter is a rule of the
  /// game, not of the machine. The assembled simulation (core_world) is
  /// where the fan-out lives; a bare engine — which is what unit tests and
  /// the determinism harness drive — answers with nothing, the same shape a
  /// world whose tables define nothing that can go wrong answers with.
  void CollectAlarms(std::vector<Alarm>& /*alarms*/) const override {}

  /// The bare engine has no weather table and no time system: every day
  /// ahead is clear and still, which is the same answer a table-less world
  /// gives for today. Not a stub to be filled — a world without weather has
  /// none.
  void CollectWeatherForecast(std::span<DayForecast> into) const override {
    for (DayForecast& day : into) {
      day = DayForecast{};
    }
  }

  /// The bare engine knows no subsystems, so it knows no rules: nobody can
  /// be ordered and the workforce is empty. StandardSimulation answers these
  /// for real by asking core_labor (task A8), exactly as it does for alarms.
  bool CanBeOrdered(ResidentId /*resident*/) const override { return false; }

  WorkforceCount Workforce() const override { return {}; }

  /// And no stock lights either: a forecast is a subsystem's rule, and the
  /// bare engine has no subsystems. It appends NOTHING rather than four dark
  /// lights — the session is what turns "nobody answered" into kNoData, and
  /// it does so once, in one place.
  void CollectStockForecast(std::vector<StockForecast>& /*lights*/) const override {}

  /// No subsystems, so no rate to forecast from. kNoData and not kNever:
  /// nothing here knows whether the unit wears, and saying "it never will"
  /// would be an answer this object has no business giving.
  Deadline WearDeadline(UnitId /*unit*/) const override {
    return NoDeadline(DeadlineKind::kNoData);
  }

 private:
  /// Buffer-law rule 2, the whole of it: empty the outbox of the step just
  /// completed, append the issued rows in arrival order (the table issues the
  /// ids, so they are exactly the ones the boundary promised the caller),
  /// then mark the cancellations. Issues come first on purpose — an order
  /// staged and taken back before the same step is still cancellable.
  /// A cancellation of an id that is gone, or of a row already kActive or
  /// terminal, is silently nothing: the order outran the chairman, and
  /// "work that began is finished, never taken back" (time design §11).
  void ApplyStaged() {
    current_.step_events.clear();
    for (const OrderRow& row : staged_issued_) {
      AppendRow(current_.orders, row);
    }
    for (const OrderId id : staged_cancelled_) {
      const std::uint32_t row = FindRow(current_.orders, id);
      if (row == kNoRow) {
        continue;
      }
      OrderStatus& status = current_.orders.rows[row].status;
      if (status == OrderStatus::kPending || status == OrderStatus::kAccepted) {
        status = OrderStatus::kCancelled;
      }
    }
    staged_issued_.clear();
    staged_cancelled_.clear();
  }

  /// One parallel slot, barrier included: WaitforTask returns only when
  /// every partition has run (the calling thread helps execute). The item
  /// count comes from the state being built: earlier sequential phases may
  /// have changed table shapes this step (step.h, ParallelItemCount doc).
  void RunParallelPhase(IParallelPhase& phase) {
    const std::uint32_t item_count = phase.ParallelItemCount(current_);
    if (item_count == 0) {
      return;
    }
    if (worker_count_ == 1) {
      // The verification mode: everything on the sim thread, one range.
      phase.RunItemRange(previous_, current_, 0, item_count);
      return;
    }
    task_.Configure(phase, previous_, current_, item_count);
    scheduler_.AddTaskSetToPipe(&task_);
    scheduler_.WaitforTask(&task_);
  }

  WorldState previous_;

  WorldState current_;

  StepPhaseSet phases_;

  std::uint32_t worker_count_;

  /// What the boundary staged since the last step, in call order.
  std::vector<OrderRow> staged_issued_;

  std::vector<OrderId> staged_cancelled_;

  enki::TaskScheduler scheduler_;

  ParallelPhaseTask task_;
};

}  // namespace

std::unique_ptr<ISimulation> CreateStepEngine(const WorldState& initial,
                                              const StepPhaseSet& phases,
                                              std::uint32_t worker_count) {
  return std::make_unique<StepEngine>(initial, phases, worker_count);
}

}  // namespace core
