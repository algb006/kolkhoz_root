// Implementation of the step engine (include/core_sim/step.h, task O2): two
// state buffers, seven phases with barriers, the enkiTS worker pool and the
// mandatory single-worker verification mode.
//
// Concurrency lives HERE and only here: the module's public surface is
// single-threaded (step.h @threading), and worker threads touch user code
// solely through IParallelPhase::RunItemRange under the buffer law. The
// scheduler is the sanctioned enkiTS; no raw std::thread, no locks in phase
// code.

#include <cassert>
#include <memory>
#include <utility>

#include "TaskScheduler.h"
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

  void ExecuteRange(enki::TaskSetPartition range, std::uint32_t /*threadnum*/) override {
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
           phases_.logistics != nullptr && phases_.metrics != nullptr && phases_.events != nullptr);
    if (worker_count_ > 1) {
      scheduler_.Initialize(worker_count_);
    }
  }

  void AdvanceStep() override {
    // Buffer law, rule 2: current starts as an exact copy of previous.
    // (External commands are applied here once their format exists —
    // a phase-2 boundary decision, STUB for now.)
    current_ = previous_;
    phases_.time_and_weather->RunSequential(previous_, current_);
    RunParallelPhase(*phases_.needs);
    phases_.decisions->RunSequential(previous_, current_);
    RunParallelPhase(*phases_.production);
    RunParallelPhase(*phases_.logistics);
    RunParallelPhase(*phases_.metrics);
    phases_.events->RunSequential(previous_, current_);
    // Rule 8: the just-built state becomes the completed one.
    std::swap(previous_, current_);
  }

  const WorldState& CompletedState() const override { return previous_; }

  void ResetWorld(const WorldState& initial) override {
    previous_ = initial;
    current_ = initial;
  }

 private:
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
