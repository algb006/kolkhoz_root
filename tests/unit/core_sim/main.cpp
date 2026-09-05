// Unit test of core_sim: contract shape and the O2 step engine.
// The engine is driven with test-local phases; parallel writes land in
// PlanState::due (pre-sized here), one row per item — disjoint by the buffer
// law, so 1 worker and N workers must produce bit-identical worlds.

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <type_traits>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/event_state.h"
#include "core_common/order_state.h"
#include "core_common/random.h"
#include "core_common/state_table_ops.h"
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
  std::uint32_t ParallelItemCount(const core::WorldState& current) const override {
    return static_cast<std::uint32_t>(current.plan.due.size());
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

/// Test stand-in for a sub-step that reports: drops one event into the
/// step's outbox. Two steps must still leave exactly one — the engine empties
/// the outbox on the way in (buffer-law rule 2).
class TestEventEmitterPhase final : public core::ISequentialPhase {
 public:
  void RunSequential(const core::WorldState& /*previous*/, core::WorldState& current) override {
    core::SimEvent event;
    event.tick = current.calendar.tick;
    event.kind = core::EventKind::kOrderAccepted;
    current.step_events.push_back(event);
  }
};

class TestNoopSequential final : public core::ISequentialPhase {
 public:
  void RunSequential(const core::WorldState& /*previous*/, core::WorldState& /*current*/) override {
  }
};

class TestNoopParallel final : public core::IParallelPhase {
 public:
  std::uint32_t ParallelItemCount(const core::WorldState& /*current*/) const override { return 0; }

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
      .metrics = &noop_parallel,
      .events = &noop_sequential,
  };
  const auto engine = core::CreateStepEngine(initial, phases, worker_count);
  for (std::uint32_t step = 0; step < steps; ++step) {
    engine->AdvanceStep();
  }
  return engine->CompletedState();
}

/// Runs the order-book scenario on `worker_count` workers over a world with
/// parallel rows, so the staging path is exercised WITH the scheduler live
/// rather than only in the single-worker mode. Returns the completed world.
core::WorldState RunStagedBook(std::uint32_t worker_count) {
  core::WorldState initial;
  initial.world_seed = 20260831;
  initial.plan.due.assign(256, 0);

  TestClockPhase clock;
  TestHashWritePhase hash_write;
  TestNoopSequential noop_sequential;
  TestNoopParallel noop_parallel;
  TestEventEmitterPhase emitter;
  const core::StepPhaseSet phases{
      .time_and_weather = &clock,
      .needs = &hash_write,
      .decisions = &noop_sequential,
      .production = &noop_parallel,
      .metrics = &noop_parallel,
      .events = &emitter,
  };
  const auto engine = core::CreateStepEngine(initial, phases, worker_count);

  core::OrderRow assign;
  assign.kind = core::OrderKind::kAssignWork;
  assign.resident = core::ResidentId{7};
  core::OrderRow pause;
  pause.kind = core::OrderKind::kPauseUnit;
  pause.unit = core::UnitId{3};

  engine->StageOrders(std::span<const core::OrderRow>(&assign, 1), {});
  engine->StageOrders(std::span<const core::OrderRow>(&pause, 1), {});
  engine->AdvanceStep();

  const core::OrderId first_order = engine->CompletedState().orders.row_ids[0];
  engine->StageOrders(std::span<const core::OrderRow>(&assign, 1),
                      std::span<const core::OrderId>(&first_order, 1));
  engine->AdvanceStep();
  engine->AdvanceStep();
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

  // -- the order book and the outbox (the boundary, manual/70-boundary.md) --
  {
    core::WorldState book_world;
    TestEventEmitterPhase emitter;
    const core::StepPhaseSet book_phases{
        .time_and_weather = &clock,
        .needs = &noop_parallel,
        .decisions = &noop_sequential,
        .production = &noop_parallel,
        .metrics = &noop_parallel,
        .events = &emitter,
    };
    const auto book_engine = core::CreateStepEngine(book_world, book_phases, 1);

    core::OrderRow assign;
    assign.kind = core::OrderKind::kAssignWork;
    assign.resident = core::ResidentId{7};
    core::OrderRow pause;
    pause.kind = core::OrderKind::kPauseUnit;
    pause.unit = core::UnitId{3};

    // Nothing is staged until the step runs: an order does not act, it waits.
    book_engine->StageOrders(std::span<const core::OrderRow>(&assign, 1), {});
    failures += Expect(book_engine->CompletedState().orders.rows.empty(),
                       "staging alone does not touch the completed world");

    // Two calls between steps accumulate in call order.
    book_engine->StageOrders(std::span<const core::OrderRow>(&pause, 1), {});
    book_engine->AdvanceStep();
    const core::WorldState& after_one = book_engine->CompletedState();
    failures += Expect(after_one.orders.rows.size() == 2 &&
                           after_one.orders.rows[0].kind == core::OrderKind::kAssignWork &&
                           after_one.orders.rows[1].kind == core::OrderKind::kPauseUnit,
                       "staged orders land in the book in call order");
    failures +=
        Expect(after_one.orders.rows[0].status == core::OrderStatus::kPending &&
                   after_one.orders.row_ids[0].value == 1 && after_one.orders.row_ids[1].value == 2,
               "a fresh order is pending, and the table issues the promised ids");
    failures += Expect(after_one.step_events.size() == 1, "the outbox holds this step's events");

    // A cancellation reaches a pending row; the outbox does not accumulate.
    const core::OrderId first_order = after_one.orders.row_ids[0];
    const core::OrderId absent{99};
    const core::OrderId cancelled_ids[] = {first_order, absent};
    book_engine->StageOrders({}, std::span<const core::OrderId>(cancelled_ids, 2));
    book_engine->AdvanceStep();
    const core::WorldState& after_two = book_engine->CompletedState();
    failures += Expect(after_two.orders.rows.size() == 2 &&
                           after_two.orders.rows[0].status == core::OrderStatus::kCancelled &&
                           after_two.orders.rows[1].status == core::OrderStatus::kPending,
                       "a cancellation reaches its row and only its row");
    failures += Expect(after_two.step_events.size() == 1,
                       "the outbox is emptied each step, not appended to");

    // Work that began is finished, never taken back (time design §11).
    core::WorldState active_world = after_two;
    active_world.orders.rows[1].status = core::OrderStatus::kActive;
    book_engine->ResetWorld(active_world);
    const core::OrderId second_order = active_world.orders.row_ids[1];
    book_engine->StageOrders({}, std::span<const core::OrderId>(&second_order, 1));
    book_engine->AdvanceStep();
    failures +=
        Expect(book_engine->CompletedState().orders.rows[1].status == core::OrderStatus::kActive,
               "an order already under way cannot be cancelled");

    // A reset drops what was staged: those rows named another world's ids.
    core::WorldState empty_world;
    book_engine->StageOrders(std::span<const core::OrderRow>(&assign, 1), {});
    book_engine->ResetWorld(empty_world);
    book_engine->AdvanceStep();
    failures += Expect(book_engine->CompletedState().orders.rows.empty(),
                       "a reset drops the orders staged for the world it replaced");
  }

  // The book and the outbox under the mandatory verification mode: the same
  // staging on 1 worker and on 4 must give the same book, row for row. The
  // determinism run above never touches these two blocks — it drives a phase
  // set that does not know they exist — so without this they would be the one
  // part of the state whose worker independence is assumed rather than shown.
  {
    const core::WorldState single_book = RunStagedBook(1);
    const core::WorldState parallel_book = RunStagedBook(4);
    failures += Expect(single_book.orders.rows.size() == 3 && single_book.orders.next_id_value == 4,
                       "the staged book is what the scenario built");
    bool book_matches = single_book.orders.rows.size() == parallel_book.orders.rows.size() &&
                        single_book.orders.row_ids == parallel_book.orders.row_ids &&
                        single_book.orders.next_id_value == parallel_book.orders.next_id_value &&
                        single_book.step_events.size() == parallel_book.step_events.size();
    for (std::size_t row = 0; book_matches && row < single_book.orders.rows.size(); ++row) {
      const core::OrderRow& one = single_book.orders.rows[row];
      const core::OrderRow& many = parallel_book.orders.rows[row];
      book_matches = one.kind == many.kind && one.status == many.status &&
                     one.refusal == many.refusal && one.work == many.work &&
                     one.issued_tick == many.issued_tick &&
                     one.resident.value == many.resident.value && one.unit.value == many.unit.value;
    }
    failures += Expect(book_matches, "1 worker and 4 workers build the same order book");
  }

  if (failures == 0) {
    std::cout << "unit_core_sim: all checks passed\n";
  }
  return failures;
}
