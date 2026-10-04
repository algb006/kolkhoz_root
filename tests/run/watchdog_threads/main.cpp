// THE WATCHDOG, ONE THREAD AND MANY, ON A WORLD THAT WAITS (architecture
// §7ж³, «Приёмка: порча с зависшим агентом гоняется в одном потоке и во
// многих, счёт срабатываний обязан совпасть»; boss, the logistics thread
// [41]-[43]).
//
// unit_core_world compares the count across worker counts on a bare genesis
// village that makes no wait at all — a test that cannot fail. This one runs
// the world the canon's chairman builds (building_chairman.h: the horse yard
// stands within the year, the goods carts set out from it with passengers on
// their benches), and A FAULT: on every working day from day 10, at hour 2 —
// the passengers seated at hour 1, none at his point yet — the first driver
// whose passenger will wait for him loses his horse. The dog must find each
// such wait the hour it begins, send the passenger on foot and write a line.
//
// WHAT IS COMPARED, one worker against three: the waits made and the firings
// by kind (the year's book), the kWatchdogFired lines counted every step, and
// the whole world byte for byte (the save codec, as determinism does).
// PRINTED BESIDE: how many waits and how many faults — a count of nought on
// both sides would agree for the wrong reason, and the run reddens on it.

#include <cstdint>
#include <iostream>
#include <optional>
#include <vector>

#include "../common/building_chairman.h"
#include "../common/run_harness.h"
#include "core_common/calendar.h"
#include "core_common/event_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/wait_state.h"
#include "core_save/save.h"

namespace {

constexpr std::uint64_t kSeed = 1931;
constexpr std::uint32_t kFirstFaultDay = 10;
constexpr std::uint32_t kFaultHour = 2;

struct Count {
  std::uint32_t waits = 0;
  std::uint32_t fired = 0;
  std::uint32_t lines = 0;
  std::uint32_t faults = 0;
  std::vector<std::byte> world;
};

/// The fault: the first resident whose passenger wait has not begun yet
/// (since after now) — his driver loses the horse. Applied through
/// ResetWorld on a copy, identically for every worker count.
bool TakeADriversHorse(core::ISimulation& simulation) {
  core::WorldState copy = simulation.CompletedState();
  for (const core::ResidentRow& person : copy.residents.rows) {
    if (!person.wait.has_value() || person.wait->kind != core::WaitKind::kPassengerAwaitsCart ||
        person.wait->since <= copy.calendar.tick) {
      continue;
    }
    const std::uint32_t driver = core::FindRow(copy.residents, person.wait->target.resident);
    if (driver == core::kNoRow || copy.residents.rows[driver].work.rides_horse == 0) {
      continue;
    }
    copy.residents.rows[driver].work.rides_horse = 0;
    simulation.ResetWorld(copy);
    return true;
  }
  return false;
}

std::optional<Count> RunAYear(std::uint32_t workers) {
  const run::Simulation world = run::Start(kSeed, workers);
  if (!world) {
    return std::nullopt;
  }
  core::ISimulation& simulation = *world.simulation;
  run::BuildingChairman builder(*world.tables);
  Count count;
  for (std::uint32_t tick = 0; tick < core::kTicksPerYear; ++tick) {
    simulation.AdvanceStep();
    const core::WorldState& now = simulation.CompletedState();
    for (const core::SimEvent& event : now.step_events) {
      count.lines += event.kind == core::EventKind::kWatchdogFired ? 1U : 0U;
    }
    const std::uint32_t hour = core::HourFromTick(now.calendar.tick);
    if (hour == 0) {
      builder.RunDay(simulation);
    }
    if (hour == kFaultHour && now.calendar.day >= kFirstFaultDay) {
      count.faults += TakeADriversHorse(simulation) ? 1U : 0U;
    }
  }
  const core::WorldState& end = simulation.CompletedState();
  for (const core::YearLedger* book : {&end.ledger.current, &end.ledger.closed}) {
    for (std::size_t kind = 0; kind < core::kWaitKindCount; ++kind) {
      count.waits += book->waits_made[kind];
      count.fired += book->watchdog_fired[kind];
    }
  }
  count.world = core::EncodeWorld(end, *world.tables);
  return count;
}

}  // namespace

int main() {
  int failures = 0;
  const std::optional<Count> one = RunAYear(1);
  const std::optional<Count> three = RunAYear(3);
  if (!one.has_value() || !three.has_value()) {
    std::cout << "FAIL: the runs did not start\n";
    return 1;
  }
  std::cout << "watchdog_threads: seed " << kSeed << ", one year - waits made " << one->waits
            << " / " << three->waits << ", faults " << one->faults << " / " << three->faults
            << ", fired " << one->fired << " / " << three->fired << ", journal lines " << one->lines
            << " / " << three->lines << " (one worker / three)\n";
  failures += run::Expect(one->waits > 0 && one->faults > 0,
                          "the world waits and the fault reaches it: waits made and faults "
                          "above nought — else the agreement below is vacuous");
  failures += run::Expect(one->fired >= one->faults && one->lines == one->fired,
                          "every fault is found by the dog, and every firing has its line");
  failures += run::Expect(one->waits == three->waits && one->faults == three->faults &&
                              one->fired == three->fired && one->lines == three->lines,
                          "one worker and three: the same waits, faults, firings and lines");
  failures += run::Expect(one->world == three->world, "and the same world, byte for byte");
  return failures;
}
