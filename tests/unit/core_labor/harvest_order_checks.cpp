// The checks of the harvest rule 2 (harvest_order_checks.h).

#include "harvest_order_checks.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

#include "assignment.h"
#include "core_common/deadline.h"
#include "core_common/quantities.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

/// The October day of the print of 0.37.81: 9.4 hours of light, a walker at
/// 2.4 game hours a kilometre, the standard day of ten.
core::AssignmentParams OctoberDay() {
  core::AssignmentParams params;
  params.window_hours = 9.4F;
  params.walk_hours_per_km = 2.4F;
  params.travel_limit_hours = 6.0F;
  params.min_usable_hours = 1.0F;
  params.standard_day_hours = 10.0F;
  params.placement_level = 0;
  return params;
}

core::AssignmentJob Reaping(std::uint32_t field_id,
                            float east_m,
                            float work_days,
                            float kcal_at_risk,
                            bool beyond_the_snow) {
  core::AssignmentJob job;
  job.kind = core::WorkKind::kHarvest;
  job.field = core::FieldId{field_id};
  job.position = core::Vec2{east_m, 0.0F};
  job.work_days_remaining = work_days;
  job.window = core::DeadlineInDays(2);
  job.kcal_at_risk = kcal_at_risk;
  job.grams_at_risk = static_cast<core::Grams>(kcal_at_risk);
  job.beyond_the_snow = beyond_the_snow;
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

bool Near(float value, float want, float tolerance) {
  return std::fabs(value - want) <= tolerance;
}

/// SEED 1936, YEAR 1, DAY 39, by hand: the wheat 1.4 km out, 12 norm-days
/// left, 10 t (33.0 million kcal), finishable; the potato 0.7 km out, 42
/// norm-days left, 100 t (77.0 million kcal), beyond the snow. A hand does
/// (9.4 - 2 x 3.36) / 10 = 0.268 of a norm on the wheat and (9.4 - 2 x 1.68)
/// / 10 = 0.604 on the potato; the wheat saves 33.0e6 x 0.268 / 12 = 0.737
/// million kcal a hand-day, the potato 77.0e6 x 0.604 / 42 = 1.107 million.
int TestThePotatoBesideTheVillageGoesBeforeTheFarWheat() {
  int failures = 0;
  const std::vector<core::AssignmentJob> jobs = {
      Reaping(1, 1400.0F, 12.0F, 33.0e6F, false),
      Reaping(2, 700.0F, 42.0F, 77.0e6F, true),
  };
  const std::vector<core::AssignmentCandidate> hands = Hands(23);
  const core::AssignmentParams day = OctoberDay();
  failures += Expect(Near(core::ExpectedNormPerHand(jobs[0], 0, hands, day), 0.268F, 0.001F) &&
                         Near(core::ExpectedNormPerHand(jobs[1], 1, hands, day), 0.604F, 0.001F),
                     "harvest order: a hand is expected to do 0.268 of a norm 1.4 km out and "
                     "0.604 at 0.7 km on a 9.4-hour day");
  failures += Expect(Near(core::SavedPerHandDay(jobs[0], 0, hands, day), 0.737e6F, 0.002e6F) &&
                         Near(core::SavedPerHandDay(jobs[1], 1, hands, day), 1.107e6F, 0.002e6F),
                     "harvest order: the wheat saves 0.737 million kcal a hand-day, the potato "
                     "1.107 million");
  const std::vector<std::uint32_t> plan = core::PlanDayAssignments(jobs, hands, day);
  failures += Expect(PlacedOn(plan, 1) == 23 && PlacedOn(plan, 0) == 0,
                     "harvest order: 23 hands go to the potato that saves more a hand-day, though "
                     "the wheat is the one the village can still finish");
  return failures;
}

/// INSIDE ONE BAND «can still be finished» decides, and across bands it does
/// not. The grid's band 100 runs from 1.1^100 = 13 780.6 to 15 158.7; one
/// norm-day of work at the village (0.94 of a norm a hand) makes the saved
/// measure kcal x 0.94.
int TestCanFinishBreaksATieInsideABandOnly() {
  int failures = 0;
  const std::vector<core::AssignmentCandidate> one = Hands(1);
  const core::AssignmentParams day = OctoberDay();
  const float band_floor = 13780.6F / 0.94F;
  // Field 1 saves 6 % above the band's floor and is beyond the snow; field 2
  // saves 1 % above it and can be finished.
  const std::vector<core::AssignmentJob> tie = {
      Reaping(1, 0.0F, 1.0F, band_floor * 1.06F, true),
      Reaping(2, 0.0F, 1.0F, band_floor * 1.01F, false),
  };
  failures += Expect(core::PlanDayAssignments(tie, one, day)[0] == 1,
                     "harvest order: inside one band the reaping that can still be finished goes "
                     "first, though the other saves five per cent more");
  // Field 1 saves 20 % above the floor — the next band — and is still beyond.
  const std::vector<core::AssignmentJob> apart = {
      Reaping(1, 0.0F, 1.0F, band_floor * 1.20F, true),
      Reaping(2, 0.0F, 1.0F, band_floor * 1.01F, false),
  };
  failures += Expect(core::PlanDayAssignments(apart, one, day)[0] == 0,
                     "harvest order: a band higher goes first, finishable or not");
  return failures;
}

/// A reaping with no food at risk keeps its place after every one with food,
/// and a field nobody can reach saves nothing.
int TestNothingAtRiskAndNobodyToGo() {
  int failures = 0;
  const std::vector<core::AssignmentCandidate> one = Hands(1);
  const core::AssignmentParams day = OctoberDay();
  std::vector<core::AssignmentJob> jobs = {
      Reaping(1, 0.0F, 1.0F, 0.0F, false),
      Reaping(2, 0.0F, 1.0F, 1000.0F, false),
  };
  // Outside the last days no reaping carries grams: the food is the only
  // thing that tells the two apart.
  jobs[1].grams_at_risk = 0;
  failures += Expect(core::PlanDayAssignments(jobs, one, day)[0] == 1,
                     "harvest order: the reaping with food at risk goes before the one with none, "
                     "whatever the field's id");
  // 7 km out: 16.8 hours one way, past the six-hour limit.
  const core::AssignmentJob far = Reaping(3, 7000.0F, 1.0F, 1.0e9F, false);
  failures += Expect(core::ExpectedNormPerHand(far, 0, one, day) == 0.0F &&
                         core::SavedPerHandDay(far, 0, one, day) == 0.0F,
                     "harvest order: a field nobody can reach is expected nothing of and saves "
                     "nothing a hand-day");
  return failures;
}

}  // namespace

int CheckHarvestOrder() {
  int failures = 0;
  failures += TestThePotatoBesideTheVillageGoesBeforeTheFarWheat();
  failures += TestCanFinishBreaksATieInsideABandOnly();
  failures += TestNothingAtRiskAndNobodyToGo();
  return failures;
}
