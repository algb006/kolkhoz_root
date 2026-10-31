// The checks of #1205 (c2)'s held ride horse (ride_hold_checks.h).

#include "ride_hold_checks.h"

#include <cstdint>
#include <iostream>
#include <vector>

#include "assignment.h"
#include "core_common/deadline.h"
#include "core_common/labor_state.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

core::AssignmentParams OneHorseDay(bool hold) {
  core::AssignmentParams params;
  params.window_hours = 9.4F;
  params.walk_hours_per_km = 2.4F;
  params.harness_hours_per_km = 1.0F;
  params.travel_limit_hours = 6.0F;
  params.min_usable_hours = 1.0F;
  params.standard_day_hours = 10.0F;
  params.placement_level = 0;
  params.draught_horses = 1;
  params.hold_ride_horse = hold;
  return params;
}

/// ONE HORSE, A WINDOWLESS CART ABOUT THE VILLAGE, FOUR HANDS. Known
/// direction: with the hold the cart's hand goes on foot and the horse stays
/// for the people's carts (horses_for_rides 1); without it the cart takes the
/// horse (0). The pair: a cart with a window takes the horse either way.
int TestTheLastHorseStaysForTheRide() {
  int failures = 0;
  core::AssignmentJob cart;
  cart.kind = core::WorkKind::kHauling;
  cart.unit = core::UnitId{4};
  cart.position = core::Vec2{500.0F, 0.0F};
  cart.work_days_remaining = 5.0F;
  cart.harnessed = true;
  cart.window = core::DeadlineNotApplicable();
  std::vector<core::AssignmentCandidate> hands;
  for (std::uint32_t row = 0; row < 4; ++row) {
    core::AssignmentCandidate hand;
    hand.resident_row = row;
    hand.home = core::Vec2{0.0F, 0.0F};
    hand.efficiency = 1.0F;
    hands.push_back(hand);
  }
  const auto left_for_rides = [&hands](const core::AssignmentJob& job, bool hold) {
    std::vector<std::uint8_t> rides;
    core::PlacementDiagnosis diagnosis;
    core::PlanDayAssignments({job}, hands, OneHorseDay(hold), &rides, nullptr, &diagnosis);
    std::uint32_t riders = 0;
    for (const std::uint8_t mark : rides) {
      riders += mark != 0 ? 1U : 0U;
    }
    std::cout << "  ride hold " << hold << ": riders " << riders << ", horses for rides "
              << diagnosis.horses_for_rides << '\n';
    return diagnosis.horses_for_rides;
  };
  failures += Expect(left_for_rides(cart, true) == 1 && left_for_rides(cart, false) == 0,
                     "ride hold (c2): a windowless cart leaves the last horse to the people's "
                     "carts with the hold, and takes it without");
  core::AssignmentJob windowed = cart;
  windowed.window = core::DeadlineInDays(2);
  failures += Expect(left_for_rides(windowed, true) == 0,
                     "ride hold (c2): a cart with a window takes the last horse, hold or not");
  return failures;
}

}  // namespace

int CheckRideHold() {
  return TestTheLastHorseStaysForTheRide();
}
