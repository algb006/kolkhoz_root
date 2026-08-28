// Unit test of core_sim: contract shape and the O2 step engine.
// The engine is driven with test-local phases; parallel writes land in
// PlanState::due (pre-sized here), one row per item — disjoint by the buffer
// law, so 1 worker and N workers must produce bit-identical worlds.

#include <cstdint>
#include <iostream>
#include <type_traits>

#include "core_common/calendar.h"
#include "core_common/random.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"

static_assert(core::kStepPhaseCount == 7, "the step cycle has seven fixed phases");
static_assert(static_cast<int>(core::StepPhase::kTimeAndWeather) == 0,
              "time-and-weather opens the step");
static_assert(static_cast<int>(core::StepPhase::kEvents) == core::kStepPhaseCount - 1,
              "events close the step");
static_assert(std::is_abstract_v<core::ISequentialPhase>, "ISequentialPhase is a contract");
static_assert(std::is_abstract_v<core::IParallelPhase>, "IParallelPhase is a contract");
static_assert(std::is_abstract_v<core::ISimulation>, "ISimulation is a contract");
static_assert(std::has_virtual_destructor_v<core::ISimulation>,
              "engine implementations are destroyed through the interface");

namespace {

int Expect(bool condition, const char* label) {
  if (condition) {
    return 0;
  }
  std::cout << "FAIL: " << label << '\n';
  return 1;
}

/// Test stand-in for the time phase: advances the tick and refreshes caches,
/// exactly what the engine needs to move the clock.
class TestClockPhase final : public core::ISequentialPhase {
 public:
  void RunSequential(const core::WorldState& previous, core::WorldState& current) override {
    current.calendar.tick = previous.calendar.tick + 1;
    core::RefreshCalendarCaches(current.calendar);
  }
};

/// Test parallel phase: writes a counter hash of (seed, tick, item) into the
/// item's own row of plan.due. Disjoint writes, no shared state — the model
/// parallel workload of the buffer law.
class TestHashWritePhase final : public core::IParallelPhase {
 public:
  std::uint32_t ParallelItemCount(const core::WorldState& previous) const override {
    return static_cast<std::uint32_t>(previous.plan.due.size());
  }

  void RunItemRange(const core::WorldState& /*previous*/,
                    core::WorldState& current,
                    std::uint32_t begin_item,
                    std::uint32_t end_item) override {
    // The calendar block is finalized by phase 1 of this step — reading it
    // from `current` is legal (buffer-law rule 4).
    const std::uint64_t tick = current.calendar.tick;
    for (std::uint32_t item = begin_item; item < end_item; ++item) {
      current.plan.due[item] = static_cast<core::Grams>(
          core::CounterHashBits(current.world_seed, tick, item, 0) & 0x7FFFFFFFU);
    }
  }
};

class TestNoopSequential final : public core::ISequentialPhase {
 public:
  void RunSequential(const core::WorldState& /*previous*/, core::WorldState& /*current*/) override {
  }
};

class TestNoopParallel final : public core::IParallelPhase {
 public:
  std::uint32_t ParallelItemCount(const core::WorldState& /*previous*/) const override { return 0; }

  void RunItemRange(const core::WorldState& /*previous*/,
                    core::WorldState& /*current*/,
                    std::uint32_t /*begin_item*/,
                    std::uint32_t /*end_item*/) override {}
};

/// Runs `steps` engine steps over a world with `items` parallel rows and the
/// given worker count; returns the completed world for comparison.
core::WorldState RunEngine(std::uint32_t items, std::uint32_t steps, std::uint32_t worker_count) {
  core::WorldState initial;
  initial.world_seed = 20260828;
  initial.plan.due.assign(items, 0);

  TestClockPhase clock;
  TestHashWritePhase hash_write;
  TestNoopSequential noop_sequential;
  TestNoopParallel noop_parallel;
  const core::StepPhaseSet phases{
      .time_and_weather = &clock,
      .needs = &hash_write,
      .decisions = &noop_sequential,
      .production = &noop_parallel,
      .logistics = &noop_parallel,
      .metrics = &noop_parallel,
      .events = &noop_sequential,
  };
  const auto engine = core::CreateStepEngine(initial, phases, worker_count);
  for (std::uint32_t step = 0; step < steps; ++step) {
    engine->AdvanceStep();
  }
  return engine->CompletedState();
}

}  // namespace

int main() {
  int failures = 0;
  constexpr std::uint32_t kItems = 1000;
  constexpr std::uint32_t kSteps = 50;

  // Single-worker reference run.
  const core::WorldState single = RunEngine(kItems, kSteps, 1);
  failures += Expect(single.calendar.tick == kSteps, "50 steps advance 50 ticks");
  failures +=
      Expect(single.calendar.day == kSteps / core::kTicksPerDay, "the day cache tracks the tick");
  failures += Expect(single.plan.due.size() == kItems, "the parallel table kept its size");
  bool rows_match = true;
  for (std::uint32_t item = 0; item < kItems; ++item) {
    const auto expected = static_cast<core::Grams>(
        core::CounterHashBits(single.world_seed, kSteps, item, 0) & 0x7FFFFFFFU);
    rows_match = rows_match && single.plan.due[item] == expected;
  }
  failures += Expect(rows_match, "every parallel row holds its last-step hash");

  // The determinism requirement: 1 worker == N workers, bit for bit.
  const core::WorldState parallel = RunEngine(kItems, kSteps, 4);
  failures += Expect(parallel.calendar.tick == single.calendar.tick,
                     "worker count does not change the clock");
  failures += Expect(parallel.plan.due == single.plan.due,
                     "1 worker and 4 workers produce identical worlds");

  // Auto worker count (0 = cores - 1) must match too.
  const core::WorldState auto_workers = RunEngine(kItems, 10, 0);
  const core::WorldState reference = RunEngine(kItems, 10, 1);
  failures += Expect(auto_workers.plan.due == reference.plan.due,
                     "auto worker count matches the verification mode");

  // ResetWorld rewinds both buffers.
  core::WorldState initial;
  initial.world_seed = 1;
  TestClockPhase clock;
  TestNoopSequential noop_sequential;
  TestNoopParallel noop_parallel;
  const core::StepPhaseSet phases{
      .time_and_weather = &clock,
      .needs = &noop_parallel,
      .decisions = &noop_sequential,
      .production = &noop_parallel,
      .logistics = &noop_parallel,
      .metrics = &noop_parallel,
      .events = &noop_sequential,
  };
  const auto engine = core::CreateStepEngine(initial, phases, 1);
  engine->AdvanceStep();
  engine->AdvanceStep();
  failures += Expect(engine->CompletedState().calendar.tick == 2, "two steps, two ticks");
  engine->ResetWorld(initial);
  failures += Expect(engine->CompletedState().calendar.tick == 0, "reset rewinds the clock");
  engine->AdvanceStep();
  failures +=
      Expect(engine->CompletedState().calendar.tick == 1, "the engine runs on after a reset");

  if (failures == 0) {
    std::cout << "unit_core_sim: all checks passed\n";
  }
  return failures;
}
