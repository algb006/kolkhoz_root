// The checks of the care's re-pricing (care_reprice_checks.h; #709).
//
// THE KNOWN ANSWER, by hand. An October day: 9.4 hours of light, a walker at 2.4 game hours a
// kilometre, the people's cart at 1.0, its seats six for a walk over two hours. A herd's care 1 km
// out wants 1.5 man-days: on foot a hand's day there is (9.4 - 4.8) / 10 = 0.46, so the main pass
// places FOUR (1.84). The people's cart then takes the four - each now rides an hour each way, (9.4
// - 2.0) / 10 = 0.74 a day, 2.96 for the four, and the care needs 1.5: the lowest-ranked passenger
// comes off (2.22 left; a second off would leave 1.48, short), never the driver. A harvest 300 m
// out wanting 5.0 had the other four at (9.4 - 1.44) / 10 = 0.796 each, 3.18, short: the released
// hand goes there in the morning's second pass - care 3, harvest 5. With no horse there is no cart,
// the walkers' price stands, and nothing moves.

#include "care_reprice_checks.h"

#include <cstdint>
#include <iostream>
#include <vector>

#include "assignment.h"
#include "core_common/deadline.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: care re-pricing: " << label << '\n';
    return 1;
  }
  return 0;
}

core::AssignmentParams OctoberDay(std::uint32_t horses) {
  core::AssignmentParams params;
  params.window_hours = 9.4F;
  params.walk_hours_per_km = 2.4F;
  params.harness_hours_per_km = 1.0F;
  params.people_cart_hours_per_km = 1.0F;
  params.travel_limit_hours = 6.0F;
  params.min_usable_hours = 1.0F;
  params.standard_day_hours = 10.0F;
  params.placement_level = 0;
  params.draught_horses = horses;
  params.people_cart_seats = 6;
  params.people_cart_min_walk_hours = 2.0F;
  return params;
}

std::vector<core::AssignmentJob> CareAndHarvest() {
  core::AssignmentJob care;
  care.kind = core::WorkKind::kHerdCare;
  care.herd = core::HerdId{3};
  care.position = core::Vec2{1000.0F, 0.0F};
  care.work_days_remaining = 1.5F;
  care.window = core::DeadlineInDays(0);
  core::AssignmentJob harvest;
  harvest.kind = core::WorkKind::kHarvest;
  harvest.field = core::FieldId{5};
  harvest.position = core::Vec2{300.0F, 0.0F};
  harvest.work_days_remaining = 5.0F;
  harvest.window = core::DeadlineInDays(5);
  return {care, harvest};
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

struct Day {
  std::uint32_t on_care = 0;
  std::uint32_t on_harvest = 0;
  std::uint32_t moved = 0;
  std::uint32_t moved_and_driving = 0;
};

Day Run(std::uint32_t horses) {
  std::vector<std::uint8_t> rides;
  std::vector<std::uint32_t> with;
  core::PlacementDiagnosis diagnosis;
  const std::vector<std::uint32_t> placed = core::PlanDayAssignments(
      CareAndHarvest(), Hands(8), OctoberDay(horses), &rides, nullptr, &diagnosis, &with);
  Day day;
  for (std::uint32_t index = 0; index < placed.size(); ++index) {
    day.on_care += placed[index] == 0 ? 1U : 0U;
    day.on_harvest += placed[index] == 1 ? 1U : 0U;
    const bool moved =
        index < diagnosis.moved_off_care.size() && diagnosis.moved_off_care[index] != 0;
    day.moved += moved ? 1U : 0U;
    day.moved_and_driving += moved && rides[index] != 0 ? 1U : 0U;
  }
  return day;
}

}  // namespace

int CheckTheCareReprice() {
  int failures = 0;
  const Day with_cart = Run(1);
  failures += Expect(with_cart.on_care == 3 && with_cart.moved == 1,
                     "the cart carries the care's four; priced riding, one is beyond the 1.5 and "
                     "comes off - three stay");
  failures += Expect(with_cart.on_harvest == 5,
                     "the released hand lands on the harvest short of hands, in the morning's "
                     "second pass");
  failures += Expect(with_cart.moved_and_driving == 0, "the cart's driver is never taken off");
  const Day walking = Run(0);
  failures += Expect(walking.on_care == 4 && walking.moved == 0 && walking.on_harvest == 4,
                     "no horse, no cart: the walkers' price stands and nobody moves");
  return failures;
}
