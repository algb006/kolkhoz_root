// The checks of the people's cart (people_cart_checks.h).

#include "people_cart_checks.h"

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

/// The October day of the brigade's cart checks: 9.4 hours of light, a
/// walker at 2.4 game hours a kilometre, a cart at 1.0, six hours of road at
/// most — and the people's cart with `seats` beside its driver for a walk
/// one way longer than two hours (0.83 km).
core::AssignmentParams OctoberDay(std::uint32_t horses, std::uint32_t seats) {
  core::AssignmentParams params;
  params.window_hours = 9.4F;
  params.walk_hours_per_km = 2.4F;
  params.harness_hours_per_km = 1.0F;
  params.travel_limit_hours = 6.0F;
  params.min_usable_hours = 1.0F;
  params.standard_day_hours = 10.0F;
  params.placement_level = 0;
  params.draught_horses = horses;
  params.people_cart_seats = seats;
  params.people_cart_min_walk_hours = 2.0F;
  return params;
}

/// A job `east_m` out. A kilometre is 2.4 hours on foot — a day's norm of
/// (9.4 - 4.8) / 10 = 0.46 — and an hour on the cart, 0.74.
core::AssignmentJob Site(core::WorkKind kind, float east_m, float work_days) {
  core::AssignmentJob job;
  job.kind = kind;
  job.unit = core::UnitId{7};
  job.position = core::Vec2{east_m, 0.0F};
  job.work_days_remaining = work_days;
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

/// What one plan did: hands on job 0 and 1, horse holders, riders, and
/// whether every rider's driver holds a horse on the rider's job.
struct Plan {
  std::uint32_t on_first = 0;
  std::uint32_t on_second = 0;
  std::uint32_t holders = 0;
  std::uint32_t riders = 0;
  bool riders_follow_a_driver = true;
};

Plan Run(const std::vector<core::AssignmentJob>& jobs,
         std::uint32_t hand_count,
         const core::AssignmentParams& params) {
  std::vector<std::uint8_t> rides;
  std::vector<std::uint32_t> with;
  const std::vector<std::uint32_t> placed =
      core::PlanDayAssignments(jobs, Hands(hand_count), params, &rides, nullptr, nullptr, &with);
  Plan plan;
  for (std::uint32_t index = 0; index < placed.size(); ++index) {
    plan.on_first += placed[index] == 0 ? 1U : 0U;
    plan.on_second += placed[index] == 1 ? 1U : 0U;
    plan.holders += rides[index] != 0 ? 1U : 0U;
    if (with[index] == core::kNoJobAssigned) {
      continue;
    }
    ++plan.riders;
    plan.riders_follow_a_driver = plan.riders_follow_a_driver && with[index] < placed.size() &&
                                  rides[with[index]] != 0 && placed[with[index]] == placed[index];
  }
  return plan;
}

/// THREE HANDS ON A SITE A KILOMETRE OUT (2.4 hours on foot) ride one cart:
/// the first drives, two ride beside him. With no horse, no seats in the
/// table, or the site half a kilometre out (1.2 hours) they walk.
int TestAFarSiteRidesOneCart() {
  int failures = 0;
  const std::vector<core::AssignmentJob> far = {Site(core::WorkKind::kConstruction, 1000.0F, 1.0F)};
  const Plan one_horse = Run(far, 5, OctoberDay(1, 6));
  std::cout << "  people's cart, a site 1 km out, one horse: on it " << one_horse.on_first
            << ", holders " << one_horse.holders << ", riders " << one_horse.riders << '\n';
  failures += Expect(one_horse.on_first == 3 && one_horse.holders == 1 && one_horse.riders == 2 &&
                         one_horse.riders_follow_a_driver,
                     "people's cart: three hands a kilometre out ride one cart — one drives, two "
                     "ride beside him on his job");
  const Plan no_horse = Run(far, 5, OctoberDay(0, 6));
  failures += Expect(no_horse.on_first == 3 && no_horse.holders == 0 && no_horse.riders == 0,
                     "people's cart: with the pool dry they walk");
  const Plan no_seats = Run(far, 5, OctoberDay(1, 0));
  failures += Expect(no_seats.holders == 0 && no_seats.riders == 0,
                     "people's cart: a table that seats nobody gives no cart");
  const std::vector<core::AssignmentJob> near = {Site(core::WorkKind::kConstruction, 500.0F, 1.0F)};
  const Plan near_site = Run(near, 5, OctoberDay(1, 6));
  failures += Expect(near_site.on_first == 2 && near_site.holders == 0 && near_site.riders == 0,
                     "people's cart: half a kilometre out — 1.2 hours on foot — they walk");
  const std::vector<core::AssignmentJob> one_hand = {
      Site(core::WorkKind::kConstruction, 1000.0F, 0.4F)};
  const Plan alone = Run(one_hand, 5, OctoberDay(1, 6));
  failures += Expect(alone.on_first == 1 && alone.holders == 0,
                     "people's cart: one far hand rides no cart — «двое и больше — подвода»");
  return failures;
}

/// THE CART IS FROM THE REMAINDER: a ploughing with a window takes its two
/// horses first; with two in the pool the site walks, with three it rides.
int TestTheRemainderOfThePool() {
  int failures = 0;
  core::AssignmentJob plough = Site(core::WorkKind::kPlowing, 100.0F, 1.5F);
  plough.window = core::DeadlineInDays(2);
  const std::vector<core::AssignmentJob> jobs = {
      plough, Site(core::WorkKind::kConstruction, 1000.0F, 1.0F)};
  const Plan two = Run(jobs, 6, OctoberDay(2, 6));
  failures += Expect(two.on_first == 2 && two.holders == 2 && two.on_second == 3 && two.riders == 0,
                     "people's cart: the plough takes both horses, and the far site walks");
  const Plan three = Run(jobs, 6, OctoberDay(3, 6));
  failures +=
      Expect(three.on_first == 2 && three.holders == 3 && three.riders == 2,
             "people's cart: the third horse, left by the plough, carries the site's three");
  return failures;
}

/// TWO KILOMETRES OUT is 4.8 hours on foot — no day left after the walk —
/// and two on the cart: nobody goes on foot, and two free hands go on the
/// cart, (9.4 - 4) / 10 = 0.54 each, to cover the one norm-day.
int TestTheCartReachesWhereTheWalkDoesNot() {
  int failures = 0;
  const std::vector<core::AssignmentJob> jobs = {
      Site(core::WorkKind::kConstruction, 2000.0F, 1.0F)};
  failures += Expect(Run(jobs, 5, OctoberDay(0, 6)).on_first == 0,
                     "people's cart: two kilometres out nobody goes on foot");
  const Plan cart = Run(jobs, 5, OctoberDay(1, 6));
  failures += Expect(cart.on_first == 2 && cart.holders == 1 && cart.riders == 1,
                     "people's cart: and on the cart two free hands go, judged by the ride");
  return failures;
}

/// SEATS BEYOND THE HORSES: five far hands, two seats beside the driver. One
/// horse carries three and two walk; two horses carry all five on two carts.
int TestTheSeats() {
  int failures = 0;
  const std::vector<core::AssignmentJob> jobs = {
      Site(core::WorkKind::kConstruction, 1000.0F, 2.0F)};
  const Plan one = Run(jobs, 6, OctoberDay(1, 2));
  failures += Expect(one.on_first == 5 && one.holders == 1 && one.riders == 2,
                     "people's cart: one cart of three seats with its driver, and two walk");
  const Plan two = Run(jobs, 6, OctoberDay(2, 2));
  failures +=
      Expect(two.on_first == 5 && two.holders == 2 && two.riders == 3 && two.riders_follow_a_driver,
             "people's cart: two carts carry all five, each rider beside a driver");
  return failures;
}

/// A4. THE FELLERS WALK UNLESS A CART CARRIES THEM: a kilometre out on foot
/// 0.46 a hand, three to the norm-day — riding free until 0.37.168 it was
/// 0.74 and two. THE MOWERS WITH NO HORSE WALK: two kilometres out nobody
/// mows on foot — until 0.37.168 two went by the ride with no horse at all.
int TestTheTwoRideFreeBroughtToOne() {
  int failures = 0;
  const std::vector<core::AssignmentJob> stand = {Site(core::WorkKind::kFelling, 1000.0F, 1.0F)};
  const Plan walking = Run(stand, 5, OctoberDay(0, 6));
  failures += Expect(walking.on_first == 3,
                     "A4: fellers a kilometre out walk — three to the norm-day, not two");
  const Plan riding = Run(stand, 5, OctoberDay(1, 6));
  failures += Expect(riding.on_first == 3 && riding.holders == 1 && riding.riders == 2,
                     "A4: and ride the people's cart when a horse is left");
  core::AssignmentJob meadow = Site(core::WorkKind::kHarvest, 2000.0F, 1.0F);
  meadow.unit = core::UnitId{};
  meadow.field = core::FieldId{3};
  meadow.harnessed = true;
  meadow.window = core::DeadlineInDays(2);
  const std::vector<core::AssignmentJob> cut = {meadow};
  const Plan scythes = Run(cut, 5, OctoberDay(0, 6));
  failures += Expect(scythes.on_first == 0 && scythes.holders == 0,
                     "A4: a meadow two kilometres out with no horse — nobody mows it on foot");
  const Plan mower = Run(cut, 5, OctoberDay(1, 6));
  failures += Expect(mower.on_first == 2 && mower.holders == 1 && mower.riders == 0,
                     "A4: with a horse the mower is written on the first mower, and the others "
                     "ride with him on no seat");
  return failures;
}

/// THE HOUR ASKS THE SAME QUESTION (work_seam.h): a rider rides while his
/// driver holds a horse on the same work and target; a seat on a goods cart,
/// a driver on another site or a driver with no horse is not the people's
/// cart.
int TestTheHourAndTheHarness() {
  int failures = 0;
  core::WorldState world;
  const auto put =
      [&world](
          core::WorkKind kind, std::uint32_t unit, std::uint8_t rides, core::ResidentId driver) {
        core::ResidentRow hand;
        hand.work.kind = kind;
        hand.work.unit = core::UnitId{unit};
        hand.work.rides_horse = rides;
        hand.work.rides_cart_of = driver;
        return core::AppendRow(world.residents, hand);
      };
  const core::ResidentId driver = put(core::WorkKind::kConstruction, 7, 1, core::ResidentId{});
  put(core::WorkKind::kConstruction, 7, 0, driver);
  const core::ResidentId carter = put(core::WorkKind::kHauling, 0, 1, core::ResidentId{});
  put(core::WorkKind::kConstruction, 7, 0, carter);
  put(core::WorkKind::kConstruction, 8, 0, driver);
  put(core::WorkKind::kFelling, 0, 0, core::ResidentId{});
  const auto rides = [&world](std::uint32_t row) {
    return core::WorkRidesOut(world, world.residents.rows[row].work);
  };
  failures += Expect(
      rides(0) && rides(1) &&
          core::WorkTravelMode(world, world.residents.rows[1].work) == core::TravelMode::kTeam,
      "people's cart, the hour: the driver and his rider ride, a team's way");
  failures += Expect(!rides(3) && !rides(4),
                     "people's cart, the hour: a goods cart's seat and a driver on another site "
                     "are not the people's cart");
  failures += Expect(!rides(5), "A4, the hour: a feller with no cart walks");
  const core::HarnessCount harness = core::CountHarness(world);
  failures += Expect(harness.in_traces == 2 && harness.releasable == 2,
                     "people's cart, the harness: the cart's horse is in the traces and the "
                     "release's, beside the carter's");
  world.residents.rows[0].work.rides_horse = 0;
  failures += Expect(!rides(1), "people's cart, the hour: with the driver's horse gone they walk");
  return failures;
}

}  // namespace

int CheckThePeoplesCart() {
  int failures = 0;
  failures += TestAFarSiteRidesOneCart();
  failures += TestTheRemainderOfThePool();
  failures += TestTheCartReachesWhereTheWalkDoesNot();
  failures += TestTheSeats();
  failures += TestTheTwoRideFreeBroughtToOne();
  failures += TestTheHourAndTheHarness();
  return failures;
}
