// district_trip — the chairman's trip to the district through the whole
// simulation (econ/manual/proposals/district-trip.md; boss seq 187, 206).
//
// WHY A RUN AND NOT A UNIT TEST. The gate that refuses the village's orders
// while he is away sits in core_world's decisions slot, before any consumer
// (core_common/chairman_away.h). A unit test of the function cannot see
// whether the slot calls it — and a gate nobody calls answers every question
// the unit test asks. So the orders go in the way the host sends them, and
// the answers are read off the events the engine emits.

#include <cstdint>
#include <iostream>
#include <span>
#include <vector>

#include "../common/run_harness.h"
#include "core_common/calendar.h"
#include "core_common/event_state.h"
#include "core_common/order_state.h"
#include "core_common/world_state.h"

namespace {

constexpr std::uint64_t kSeed = 1929;

/// May of the first year: no blizzard, no mud — the trip's plain day.
constexpr core::SimDay kMayDay = 4U * core::kDaysPerMonth;

/// Steps to `tick` exactly.
void StepTo(core::ISimulation& simulation, core::Tick tick) {
  while (simulation.CompletedState().calendar.tick < tick) {
    simulation.AdvanceStep();
  }
}

/// Issues one order of `kind` with no fields, steps once, and says what the
/// engine answered: the refusal, or kNone for done or accepted.
core::OrderRefusal Answer(core::ISimulation& simulation, core::OrderKind kind) {
  core::OrderRow order;
  order.kind = kind;
  simulation.StageOrders(std::span<const core::OrderRow>(&order, 1), {});
  simulation.AdvanceStep();
  for (const core::SimEvent& event : simulation.CompletedState().step_events) {
    if (event.kind == core::EventKind::kOrderRefused) {
      return static_cast<core::OrderRefusal>(event.amount);
    }
  }
  return core::OrderRefusal::kNone;
}

bool Saw(const core::ISimulation& simulation, core::EventKind kind) {
  for (const core::SimEvent& event : simulation.CompletedState().step_events) {
    if (event.kind == kind) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  run::Simulation world = run::Start(kSeed);
  if (!world) {
    return 1;
  }
  int failures = 0;
  const core::Tick day_start = static_cast<core::Tick>(kMayDay) * core::kTicksPerDay;
  StepTo(*world, day_start + 3);
  failures +=
      run::Expect(Answer(*world, core::OrderKind::kTripToDistrict) == core::OrderRefusal::kNone,
                  "district_trip: at 4:00 the trip is booked for this 8:00");
  bool departed = false;
  while (world.State().calendar.tick < day_start + 8) {
    world->AdvanceStep();
    departed = departed || Saw(*world, core::EventKind::kTripDeparted);
  }
  failures += run::Expect(departed, "district_trip: at 8:00 he leaves, and it is said");
  StepTo(*world, day_start + 10);
  failures += run::Expect(
      Answer(*world, core::OrderKind::kCancelDayOff) == core::OrderRefusal::kChairmanAway,
      "district_trip: an order to the village while he is away is refused "
      "kChairmanAway — the gate stands in the slot");
  failures += run::Expect(
      Answer(*world, core::OrderKind::kTripToDistrict) == core::OrderRefusal::kChairmanAway,
      "district_trip: and a second trip while he is on the first");
  bool returned = false;
  while (world.State().calendar.tick < day_start + 21) {
    world->AdvanceStep();
    returned = returned || Saw(*world, core::EventKind::kTripReturned);
  }
  failures += run::Expect(returned && world.State().chairman.away_until_tick == 0,
                          "district_trip: by the evening he is back");
  StepTo(*world, day_start + core::kTicksPerDay + 3);
  failures += run::Expect(
      Answer(*world, core::OrderKind::kTripToDistrict) == core::OrderRefusal::kTripThisMonth,
      "district_trip: the next morning, the same month — «раз в месяц»");
  std::cout << "district_trip: " << (failures == 0 ? "all checks passed" : "FAILED") << '\n';
  return failures == 0 ? 0 : 1;
}
