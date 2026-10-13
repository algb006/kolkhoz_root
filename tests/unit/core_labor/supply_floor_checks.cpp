// The checks of the floor under a works' supply (supply_floor_checks.h).
//
// THE KNOWN ANSWERS, by hand from transport design «Пол подвоза»: four hands, four horses, a
// ploughing with a window that would take every horse, and a works' supply of three cart-days on
// the windowless tier. Off the floor the supply gets nobody - the trace of #734. On the floor
// with one carter in the budget: one carter with a horse on the supply, three ploughs. Two
// supplies on the floor and one carter: the first of the sites' queue takes him. Two carters and
// a first supply of half a cart-day: one each. The team taken whole by a load at level 0: the
// floor places nobody - no hand on foot. «Важнее полевых работ»: the supply is covered first,
// three carters, and the plough takes the horse that is left. And the budget's own arithmetic:
// a quarter of the team rounded down, never fewer than one while there is a horse.
#include "supply_floor_checks.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

#include "assignment.h"
#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/labor_state.h"
#include "core_common/site_supply_floor.h"
#include "core_common/unit_state.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: supply floor: " << label << '\n';
    return 1;
  }
  return 0;
}

constexpr core::Vec2 kOrigin{.x = 0.0F, .y = 0.0F};

std::vector<core::AssignmentCandidate> Hands(std::uint32_t count) {
  std::vector<core::AssignmentCandidate> hands;
  for (std::uint32_t row = 0; row < count; ++row) {
    core::AssignmentCandidate hand;
    hand.resident_row = row;
    hand.home = kOrigin;
    hand.efficiency = 1.0F;
    hand.rest = 70.0F;
    hand.skill = 20.0F;
    hands.push_back(hand);
  }
  return hands;
}

core::AssignmentParams Morning(std::uint32_t horses, std::uint32_t floor_carters) {
  core::AssignmentParams params;
  params.window_hours = 10.0F;
  params.walk_hours_per_km = 3.1F;
  params.travel_limit_hours = 2.0F;
  params.standard_day_hours = 10.0F;
  params.draught_horses = horses;
  params.supply_floor_carters = floor_carters;
  params.placement_level = 2;
  // A settlement with carts: a carrier on foot is a small share of a cart.
  params.walker_share_of_cart_day = 0.05F;
  return params;
}

/// A ploughing with an open window: forty norm-days, every horse's worth.
core::AssignmentJob Plough() {
  core::AssignmentJob job;
  job.kind = core::WorkKind::kPlowing;
  job.field = core::FieldId{1};
  job.position = kOrigin;
  job.work_days_remaining = 40.0F;
  job.window = core::DeadlineInDays(5);
  return job;
}

/// A works' supply of `cart_days`, its term `term_days` away.
core::AssignmentJob Supply(std::uint32_t unit, float cart_days, std::uint8_t term_days) {
  core::AssignmentJob job;
  job.kind = core::WorkKind::kHauling;
  job.unit = core::UnitId{unit};
  job.position = kOrigin;
  job.work_days_remaining = cart_days;
  job.harnessed = true;
  job.supplies_works = true;
  job.window = core::DeadlineInDays(term_days);
  return job;
}

struct Placed {
  std::uint32_t hands = 0;
  std::uint32_t with_horse = 0;
};

Placed On(std::uint32_t job,
          const std::vector<std::uint32_t>& plan,
          const std::vector<std::uint8_t>& rides_horse) {
  Placed placed;
  for (std::size_t index = 0; index < plan.size(); ++index) {
    if (plan[index] == job) {
      ++placed.hands;
      placed.with_horse += rides_horse[index] != 0 ? 1U : 0U;
    }
  }
  return placed;
}

int CheckTheBudget() {
  int failures = 0;
  const core::SupplyFloorRules rules;
  failures += Expect(core::SupplyFloorCarters(rules, 0) == 0, "no horse, no floor");
  failures +=
      Expect(core::SupplyFloorCarters(rules, 1) == 1 && core::SupplyFloorCarters(rules, 3) == 1 &&
                 core::SupplyFloorCarters(rules, 4) == 1,
             "below and at four horses the floor is one carter");
  failures +=
      Expect(core::SupplyFloorCarters(rules, 8) == 2 && core::SupplyFloorCarters(rules, 11) == 2 &&
                 core::SupplyFloorCarters(rules, 16) == 4,
             "a quarter of the team, rounded down");
  core::SupplyFloorRules off;
  off.after_days = 0;
  failures += Expect(core::SupplyFloorCarters(off, 16) == 0, "nought days switches the floor off");
  // The trigger: the rule's days, and something awaiting.
  core::ConstructionState works;
  works.awaiting = {0, 1000};
  works.supply_idle_days = 3;
  failures += Expect(!core::OnSupplyFloor(rules, works), "three idle days are not the floor yet");
  works.supply_idle_days = 4;
  failures += Expect(core::OnSupplyFloor(rules, works), "the fourth idle day is the floor");
  failures += Expect(!core::OnSupplyFloor(off, works), "switched off, no works is on it");
  works.awaiting = {0, 0};
  failures += Expect(!core::OnSupplyFloor(rules, works), "a works awaiting nothing is not on it");
  return failures;
}

}  // namespace

int CheckTheSupplyFloor() {
  int failures = CheckTheBudget();
  const std::vector<core::AssignmentCandidate> hands = Hands(4);
  std::vector<std::uint8_t> rides;
  const auto run = [&hands, &rides](const std::vector<core::AssignmentJob>& jobs,
                                    const core::AssignmentParams& params) {
    return core::PlanDayAssignments(jobs, hands, params, &rides);
  };

  // THE TRACE, AND ITS CURE. In either input order.
  for (const bool supply_first : {true, false}) {
    const std::uint32_t supply_at = supply_first ? 0U : 1U;
    const std::uint32_t plough_at = supply_first ? 1U : 0U;
    core::AssignmentJob supply = Supply(9, 3.0F, 6);
    const auto jobs = [&]() {
      return supply_first ? std::vector<core::AssignmentJob>{supply, Plough()}
                          : std::vector<core::AssignmentJob>{Plough(), supply};
    };
    auto plan = run(jobs(), Morning(4, 1));
    failures +=
        Expect(On(supply_at, plan, rides).hands == 0 && On(plough_at, plan, rides).hands == 4,
               "off the floor the plough takes every horse and the supply nobody");
    supply.supply_floor = true;
    plan = run(jobs(), Morning(4, 1));
    const Placed carting = On(supply_at, plan, rides);
    failures += Expect(
        carting.hands == 1 && carting.with_horse == 1 && On(plough_at, plan, rides).hands == 3,
        "on the floor one carter with a horse goes to the supply, three plough");
    plan = run(jobs(), Morning(4, 0));
    failures += Expect(On(supply_at, plan, rides).hands == 0,
                       "a placement with no floor budget (the day's top-up) deals none");
    // «ВАЖНЕЕ ПОЛЕВЫХ РАБОТ»: covered first, the plough takes what is left.
    supply.supply_floor = false;
    supply.over_fields = true;
    plan = run(jobs(), Morning(4, 0));
    const Placed whole = On(supply_at, plan, rides);
    failures +=
        Expect(whole.hands == 3 && whole.with_horse == 3 && On(plough_at, plan, rides).hands == 1,
               "«важнее полевых работ»: the supply's three cart-days are covered first, "
               "the plough takes the horse left");
  }

  // ONE BUDGET FOR ALL THE WORKS ON THE FLOOR, in the queue's order: the
  // first site of the sites' queue (`raised`), then the nearer term.
  {
    core::AssignmentJob first = Supply(9, 3.0F, 6);
    first.supply_floor = true;
    first.raised = true;
    core::AssignmentJob second = Supply(4, 3.0F, 2);
    second.supply_floor = true;
    const std::vector<core::AssignmentJob> jobs = {second, Plough(), first};
    auto plan = run(jobs, Morning(4, 1));
    failures += Expect(On(2, plan, rides).hands == 1 && On(0, plan, rides).hands == 0 &&
                           On(1, plan, rides).hands == 3,
                       "one carter in the budget goes to the first site of the queue");
    // What the first cannot use goes to the next.
    std::vector<core::AssignmentJob> small = jobs;
    small[2].work_days_remaining = 0.5F;
    std::vector<core::AssignmentCandidate> eight = Hands(8);
    std::vector<std::uint8_t> eight_rides;
    plan = core::PlanDayAssignments(small, eight, Morning(8, 2), &eight_rides);
    failures += Expect(On(2, plan, eight_rides).hands == 1 && On(0, plan, eight_rides).hands == 1 &&
                           On(1, plan, eight_rides).hands == 6,
                       "two carters, a first supply of half a cart-day: one each, six plough");
  }

  // BEHIND LEVEL 0, AND NOBODY ON FOOT: a load at level 0 that takes the whole
  // team leaves the floor no horse, and the floor places no back.
  {
    core::AssignmentJob supply = Supply(9, 3.0F, 6);
    supply.supply_floor = true;
    core::AssignmentJob urgent;
    urgent.kind = core::WorkKind::kHauling;
    urgent.field = core::FieldId{3};
    urgent.position = kOrigin;
    urgent.work_days_remaining = 40.0F;
    urgent.harnessed = true;
    urgent.logistics_urgent = true;
    urgent.window = core::DeadlineInDays(1);
    const std::vector<core::AssignmentJob> jobs = {supply, urgent};
    core::AssignmentParams params = Morning(4, 1);
    // The horse yard stands: no back is sent to a works (the last pass).
    params.yard_walk_hours = {0.0F};
    const auto plan = run(jobs, params);
    failures += Expect(On(1, plan, rides).hands == 4 && On(0, plan, rides).hands == 0,
                       "a load at level 0 takes the team first; the floor sends nobody on foot");
  }
  std::cout << "  supply floor: the budget, the trigger, the trace and its cure, the queue's "
               "order, level 0 first, «важнее полевых работ» - checked\n";
  return failures;
}
