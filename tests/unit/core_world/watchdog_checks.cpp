// The checks of the watchdog (watchdog_checks.h).

#include "watchdog_checks.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>

#include "core_common/event_state.h"
#include "core_common/herd_state.h"
#include "core_common/labor_state.h"
#include "core_common/resident_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/wait_state.h"
#include "core_common/world_state.h"
#include "core_world/watchdog.h"
#include "wait_rules.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

/// The kWatchdogFired lines of this step, and the last one's packed amount.
std::uint32_t Lines(const core::WorldState& world, std::int64_t& last_amount) {
  std::uint32_t lines = 0;
  for (const core::SimEvent& event : world.step_events) {
    if (event.kind == core::EventKind::kWatchdogFired) {
      ++lines;
      last_amount = event.amount;
    }
  }
  return lines;
}

/// A driver on a horse and his passenger on a reaping, waiting for the cart
/// at his point from tick 10, the term one hour.
struct Passenger {
  core::WorldState world;
  core::ResidentId driver;
  core::ResidentId rider;
};

Passenger APassenger() {
  Passenger out;
  core::ResidentRow driver;
  driver.work.kind = core::WorkKind::kHauling;
  driver.work.rides_horse = 1;
  out.driver = core::AppendRow(out.world.residents, driver);
  core::ResidentRow rider;
  rider.work.kind = core::WorkKind::kHarvest;
  rider.work.rides_cart_of = out.driver;
  rider.work.travel_hours = 2.5F;
  rider.wait = core::WaitRecord(
      core::WaitKind::kPassengerAwaitsCart,
      10,
      1,
      core::WaitTarget{.resident = out.driver, .unit = core::UnitId{}, .field = core::FieldId{}});
  out.rider = core::AppendRow(out.world.residents, rider);
  return out;
}

void WalkAt(core::IWatchdog& dog,
            core::WorldState& world,
            core::Tick tick,
            core::WatchdogTally& tally) {
  world.calendar.tick = tick;
  world.step_events.clear();
  tally = dog.WalkHour(world);
}

}  // namespace

int CheckTheWatchdog() {
  int failures = 0;
  const auto kind = [](core::WaitKind wait) { return static_cast<std::size_t>(wait); };
  const std::size_t passenger = kind(core::WaitKind::kPassengerAwaitsCart);
  const std::size_t horse = kind(core::WaitKind::kHorseAtWorkersYard);

  // NO DOG WITHOUT EVERY KIND'S RULES (watchdog.h, CreateWatchdog).
  {
    auto rules = core::CreateWaitRules();
    rules[horse].reset();
    failures += Expect(core::CreateWatchdog(std::move(rules)) == nullptr,
                       "watchdog: a kind without rules builds no dog");
    auto swapped = core::CreateWaitRules();
    std::swap(swapped[0], swapped[1]);
    failures += Expect(core::CreateWatchdog(std::move(swapped)) == nullptr,
                       "watchdog: rules in another kind's place build no dog");
  }
  const auto dog = core::CreateWatchdog(core::CreateWaitRules());
  if (Expect(dog != nullptr, "watchdog: every kind's rules build the dog") != 0) {
    return failures + 1;
  }
  core::WatchdogTally tally;
  std::int64_t amount = 0;

  // THE HONEST WAIT, AND ONE NOT BEGUN: before tick 10 he has not reached his
  // point — not looked at; at 10 the cart is coming — looked at, no firing.
  {
    Passenger case_ = APassenger();
    WalkAt(*dog, case_.world, 8, tally);
    failures += Expect(tally.polled[passenger] == 0 && tally.fired[passenger] == 0,
                       "watchdog: a wait not begun is not looked at");
    WalkAt(*dog, case_.world, 10, tally);
    failures += Expect(tally.polled[passenger] == 1 && tally.fired[passenger] == 0 &&
                           case_.world.residents.rows[1].wait.has_value() &&
                           Lines(case_.world, amount) == 0,
                       "watchdog: an honest wait, within its term and its cart there, is "
                       "looked at and left");
  }

  // FAULT 1 — THE CART IS GONE: the driver leaves the world. Within the hour
  // the dog finds it, the passenger walks (the cart struck off, his road to be
  // measured again), the wait is cleared, and the journal has its line.
  {
    Passenger case_ = APassenger();
    core::RemoveRow(case_.world.residents, case_.driver);
    WalkAt(*dog, case_.world, 10, tally);
    const std::uint32_t row = core::FindRow(case_.world.residents, case_.rider);
    const core::ResidentRow& rider = case_.world.residents.rows[row];
    const std::uint32_t lines = Lines(case_.world, amount);
    const auto verdict = static_cast<core::WaitVerdict>((amount >> 32) & 0xFF);
    std::cout << "  watchdog, the driver gone: fired " << tally.fired[passenger]
              << ", journal lines " << lines << ", verdict " << static_cast<int>(verdict)
              << ", rides a cart "
              << (rider.work.rides_cart_of.value != core::kInvalidEntityIdValue) << '\n';
    failures += Expect(tally.fired[passenger] == 1 && !rider.wait.has_value() &&
                           rider.work.rides_cart_of.value == core::kInvalidEntityIdValue &&
                           rider.work.travel_hours < 0.0F,
                       "watchdog: the driver gone — the passenger walks, his wait cleared");
    failures += Expect(lines == 1 && verdict == core::WaitVerdict::kTargetGone &&
                           ((amount >> 40) & 0xFF) == passenger,
                       "watchdog: and the journal has one line, the cart gone, his kind");
  }

  // FAULT 2 — THE TERM RUNS OUT: the cart is there, but at tick 12 he has
  // stood two hours, past his one.
  {
    Passenger case_ = APassenger();
    WalkAt(*dog, case_.world, 12, tally);
    const std::uint32_t lines = Lines(case_.world, amount);
    failures += Expect(tally.fired[passenger] == 1 && lines == 1 &&
                           static_cast<core::WaitVerdict>((amount >> 32) & 0xFF) ==
                               core::WaitVerdict::kTermPassed &&
                           (amount & 0xFFFFFFFF) == 2,
                       "watchdog: past its term the wait hangs — fired, two hours stood, "
                       "written");
  }

  // FAULT 3 — THE HORSE'S NIGHT PAST ITS TERM (no writer in the world yet:
  // the test makes the record). Its worker is there; at tick 30 the night of
  // twenty hours from tick 5 has run out: the horse is in the stable again.
  {
    core::WorldState world;
    core::ResidentRow worker;
    const core::ResidentId worker_id = core::AppendRow(world.residents, worker);
    core::HerdRow team;
    team.adult_count = 2;
    team.wait = core::WaitRecord(
        core::WaitKind::kHorseAtWorkersYard,
        5,
        20,
        core::WaitTarget{.resident = worker_id, .unit = core::UnitId{}, .field = core::FieldId{}});
    const core::HerdId herd = core::AppendRow(world.herds, team);
    WalkAt(*dog, world, 20, tally);
    failures += Expect(tally.fired[horse] == 0 && world.herds.rows[0].wait.has_value(),
                       "watchdog: a horse within its night is left");
    WalkAt(*dog, world, 30, tally);
    bool named = false;
    for (const core::SimEvent& event : world.step_events) {
      named = named ||
              (event.kind == core::EventKind::kWatchdogFired && event.herd.value == herd.value);
    }
    failures += Expect(tally.fired[horse] == 1 && !world.herds.rows[0].wait.has_value() && named,
                       "watchdog: past its night the horse stands in the stable again, and the "
                       "line names the herd");
  }
  return failures;
}
