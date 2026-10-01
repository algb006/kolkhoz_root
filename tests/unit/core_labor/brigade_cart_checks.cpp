// The checks of the brigade's cart (brigade_cart_checks.h).

#include "brigade_cart_checks.h"

#include <cstdint>
#include <iostream>
#include <vector>

#include "assignment.h"
#include "core_common/deadline.h"
#include "core_common/state_table_ops.h"
#include "core_common/work_seam.h"
#include "core_common/world_state.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

/// The October day of the hand print: 9.4 hours of light, a walker at 2.4
/// game hours a kilometre, a cart at 1.0, six hours of road at most.
core::AssignmentParams OctoberDay(std::uint32_t horses) {
  core::AssignmentParams params;
  params.window_hours = 9.4F;
  params.walk_hours_per_km = 2.4F;
  params.harness_hours_per_km = 1.0F;
  params.travel_limit_hours = 6.0F;
  params.min_usable_hours = 1.0F;
  params.standard_day_hours = 10.0F;
  params.placement_level = 0;
  params.draught_horses = horses;
  return params;
}

core::AssignmentJob Reaping(std::uint32_t field_id, float east_m, float work_days) {
  core::AssignmentJob job;
  job.kind = core::WorkKind::kHarvest;
  job.field = core::FieldId{field_id};
  job.position = core::Vec2{east_m, 0.0F};
  job.work_days_remaining = work_days;
  job.window = core::DeadlineInDays(2);
  job.brigade_cart = true;
  return job;
}

std::vector<core::AssignmentCandidate> Hands(std::uint32_t count) {
  std::vector<core::AssignmentCandidate> hands;
  for (std::uint32_t row = 0; row < count; ++row) {
    core::AssignmentCandidate hand;
    hand.resident_row = row;
    hand.home = core::Vec2{0.0F, 0.0F};
    hand.efficiency = 1.0F;
    hands.push_back(hand);
  }
  return hands;
}

std::uint32_t PlacedOn(const std::vector<std::uint32_t>& plan, std::uint32_t job) {
  std::uint32_t placed = 0;
  for (const std::uint32_t at : plan) {
    placed += at == job ? 1U : 0U;
  }
  return placed;
}

std::uint32_t Holders(const std::vector<std::uint8_t>& rides_horse) {
  std::uint32_t holders = 0;
  for (const std::uint8_t mark : rides_horse) {
    holders += mark != 0 ? 1U : 0U;
  }
  return holders;
}

/// TWO FIELDS 1.4 KM OUT, ONE HORSE, FIVE HANDS, by hand. On the cart a hand
/// does (9.4 - 2 x 1.4) / 10 = 0.66 of a norm, on foot (9.4 - 2 x 3.36) / 10
/// = 0.268. Field 1 has one norm-day left: two riders cover it (1.32), four
/// walkers would be needed (1.07). Field 2 has ten: the other hands walk.
int TestOneHorseOneBrigade() {
  int failures = 0;
  const std::vector<core::AssignmentJob> jobs = {Reaping(1, 1400.0F, 1.0F),
                                                 Reaping(2, 1400.0F, 10.0F)};
  const std::vector<core::AssignmentCandidate> hands = Hands(5);
  std::vector<std::uint8_t> rides;
  const std::vector<std::uint32_t> with_horse =
      core::PlanDayAssignments(jobs, hands, OctoberDay(1), &rides);
  failures += Expect(PlacedOn(with_horse, 0) == 2 && PlacedOn(with_horse, 1) == 3,
                     "brigade cart: with one horse the first field's brigade is judged by the "
                     "ride — two hands cover its norm-day, the other three walk to the second");
  failures += Expect(Holders(rides) == 1 && with_horse[0] == 0 && rides[0] == 1,
                     "brigade cart: one horse leaves the pool, written on the first hand placed "
                     "on the field that got it");
  // Three horses: a cart a field, and the third horse stays in the pool —
  // the riders beside the driver take none.
  const std::vector<std::uint32_t> three_horses =
      core::PlanDayAssignments(jobs, hands, OctoberDay(3), &rides);
  failures += Expect(
      PlacedOn(three_horses, 0) == 2 && PlacedOn(three_horses, 1) == 3 && Holders(rides) == 2,
      "brigade cart: with three horses each of the two fields takes one cart and "
      "no rider beside the driver takes a horse");
  const std::vector<std::uint32_t> no_horse =
      core::PlanDayAssignments(jobs, hands, OctoberDay(0), &rides);
  failures +=
      Expect(PlacedOn(no_horse, 0) == 4 && PlacedOn(no_horse, 1) == 1 && Holders(rides) == 0,
             "brigade cart: with the pool dry they walk — four hands to the same "
             "norm-day, and nobody holds a horse");
  return failures;
}

/// THE CART THAT IS OUT ALREADY takes no second horse: the top-up's hands
/// ride with the morning's driver.
int TestTheCartThatIsOut() {
  int failures = 0;
  std::vector<core::AssignmentJob> jobs = {Reaping(1, 1400.0F, 1.0F)};
  jobs[0].cart_out = true;
  std::vector<std::uint8_t> rides;
  const std::vector<std::uint32_t> plan =
      core::PlanDayAssignments(jobs, Hands(5), OctoberDay(3), &rides);
  failures += Expect(PlacedOn(plan, 0) == 2 && Holders(rides) == 0,
                     "brigade cart: hands sent to a field whose cart is out ride with its "
                     "driver and take no horse");
  return failures;
}

/// A FIELD THREE KILOMETRES OUT is 7.2 hours on foot, past the six-hour
/// limit, and three hours on the cart.
int TestTheCartReachesWhereTheWalkDoesNot() {
  int failures = 0;
  const std::vector<core::AssignmentJob> jobs = {Reaping(1, 3000.0F, 1.0F)};
  const std::vector<core::AssignmentCandidate> hands = Hands(5);
  failures += Expect(PlacedOn(core::PlanDayAssignments(jobs, hands, OctoberDay(0)), 0) == 0,
                     "brigade cart: three kilometres out nobody goes on foot");
  failures += Expect(PlacedOn(core::PlanDayAssignments(jobs, hands, OctoberDay(1)), 0) == 3,
                     "brigade cart: and on the cart three hands cover the norm-day at 0.34 each");
  return failures;
}

/// THE HOUR ASKS THE SAME QUESTION (work_seam.h): a hand of a reaping rides
/// while a driver stands on his field, and the harness counts one horse for
/// the brigade.
int TestTheHourAndTheHarness() {
  int failures = 0;
  core::WorldState world;
  core::FieldRow field;
  field.kind = core::LandKind::kArable;
  field.phase = core::FieldPhase::kHarvest;
  field.work_days_remaining = 5.0F;
  const core::FieldId id = core::AppendRow(world.fields, field);
  for (int index = 0; index < 3; ++index) {
    core::ResidentRow hand;
    hand.work.kind = core::WorkKind::kHarvest;
    hand.work.field = id;
    core::AppendRow(world.residents, hand);
  }
  const core::WorkAssignment passenger = world.residents.rows[1].work;
  failures +=
      Expect(!core::WorkRidesOut(world, passenger) && core::CountHarness(world).in_traces == 0,
             "brigade cart: with no driver on the field its hands walk and no horse is "
             "in the traces");
  world.residents.rows[0].work.rides_horse = 1;
  failures += Expect(core::WorkRidesOut(world, passenger) &&
                         core::WorkTravelMode(world, passenger) == core::TravelMode::kTeam,
                     "brigade cart: with a driver on the field every hand rides, a team's way — "
                     "not the roads-only way of a cart with produce");
  const core::HarnessCount harness = core::CountHarness(world);
  failures += Expect(harness.in_traces == 1 && harness.harnessed == 1 && harness.releasable == 1,
                     "brigade cart: three hands on one cart are one horse in the traces");
  world.fields.rows[0].kind = core::LandKind::kMeadow;
  failures += Expect(core::WorkRidesOut(world, passenger) &&
                         core::WorkTravelMode(world, passenger) == core::TravelMode::kTeam &&
                         core::CountHarness(world).releasable == 0,
                     "brigade cart: a meadow's mowers ride as before — a team, one horse a "
                     "meadow, not the release's");
  return failures;
}

}  // namespace

int CheckBrigadeCart() {
  int failures = 0;
  failures += TestOneHorseOneBrigade();
  failures += TestTheCartThatIsOut();
  failures += TestTheCartReachesWhereTheWalkDoesNot();
  failures += TestTheHourAndTheHarness();
  return failures;
}
