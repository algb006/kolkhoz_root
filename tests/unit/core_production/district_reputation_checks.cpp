// The checks of the rating earned beyond the plan (district_reputation_checks.h).
//
// THE KNOWN ANSWERS, by hand: the limit's tiers 5 t and 20 t (limit_catalog.h's defaults) - 0 t
// over reaches no tier, 3 t the first, 10 t the second, 30 t the third. At +1 a tier capped at 3,
// 30 t over raises 50 to 53; at +2 a tier the cap holds it at 53. A surplus of 3 t that is all a
// district loan's grain (3 t of a crop weighing one gram of grain a gram) reaches no tier. A clean
// visit at +1 raises 99.5 to 100, never past it. The plan's weight ×1.6 multiplies a position's
// 4 200 kg to 6 720 kg.
#include "district_reputation_checks.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "core_catalog/limit_catalog.h"
#include "core_common/calendar.h"
#include "core_common/quantities.h"
#include "core_common/world_state.h"
#include "district_plan.h"
#include "district_reputation.h"
#include "plan_due.h"
#include "production_config.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: rating beyond the plan: " << label << '\n';
    return 1;
  }
  return 0;
}

core::ProductionConfig RatingConfig() {
  core::ProductionConfig config;
  config.limit.overfulfil_tier1_t = 5.0F;
  config.limit.overfulfil_tier2_t = 20.0F;
  config.limit.overfulfil_grain_kcal_per_gram = 3.0F;
  config.food_kcal_per_gram = {3.0F};
  config.plan_overfulfil_reputation_per_tier = 1.0F;
  config.plan_overfulfil_reputation_max = 3.0F;
  config.district_visit_clean_reputation = 1.0F;
  return config;
}

}  // namespace

int CheckTheRatingBeyondThePlan() {
  int failures = 0;
  const core::ProductionConfig config = RatingConfig();
  failures += Expect(core::OverfulfilTiersReached(config.limit, 0.0F) == 0 &&
                         core::OverfulfilTiersReached(config.limit, 3.0F) == 1 &&
                         core::OverfulfilTiersReached(config.limit, 10.0F) == 2 &&
                         core::OverfulfilTiersReached(config.limit, 30.0F) == 3,
                     "the tiers REACHED: 0 t none, 3 t the first, 10 t the second, 30 t the third");

  core::WorldState world;
  world.chairman.raikom_reputation = 50.0F;
  world.ledger.current.goods_loan_taken.assign(1, 0);
  core::StepReputationBeyondPlan(config, world, 30.0F);
  failures +=
      Expect(world.chairman.raikom_reputation == 53.0F, "three tiers at +1 a tier raise 50 to 53");
  core::ProductionConfig doubled = config;
  doubled.plan_overfulfil_reputation_per_tier = 2.0F;
  world.chairman.raikom_reputation = 50.0F;
  core::StepReputationBeyondPlan(doubled, world, 30.0F);
  failures +=
      Expect(world.chairman.raikom_reputation == 53.0F, "at +2 a tier the cap of 3 a year holds");
  core::ProductionConfig off = config;
  off.plan_overfulfil_reputation_per_tier = 0.0F;
  world.chairman.raikom_reputation = 50.0F;
  core::StepReputationBeyondPlan(off, world, 30.0F);
  failures += Expect(world.chairman.raikom_reputation == 50.0F,
                     "off (the table's default) the rating does not move");

  // THE PUMP SHUT: a surplus that is all the district's lent grain.
  world.chairman.raikom_reputation = 50.0F;
  world.ledger.current.goods_loan_taken.assign(1, 3 * core::kGramsPerTonne);
  failures += Expect(core::OwnOverfulfilGrainTonnes(config, world, 3.0F) == 0.0F,
                     "a surplus of 3 t that is all a loan's grain is none of the farm's own");
  core::StepReputationBeyondPlan(config, world, 3.0F);
  failures +=
      Expect(world.chairman.raikom_reputation == 50.0F, "a loan-only surplus buys no rating");
  failures += Expect(core::OwnOverfulfilGrainTonnes(config, world, 10.0F) == 7.0F,
                     "a loan of 3 t comes off a surplus of 10 t, leaving 7 t");

  // A CLEAN VISIT, and the scale's ceiling.
  world.chairman.raikom_reputation = 99.5F;
  core::StepReputationForCleanVisit(config, world);
  failures += Expect(world.chairman.raikom_reputation == 100.0F,
                     "a clean visit at +1 raises 99.5 to 100 and no further");

  // THE PLAN'S WEIGHT: one product with the band, on the position's figure.
  core::ProductionConfig weighed;
  core::CropDef oat;
  oat.resource = core::ResourceId{0};
  oat.yield_kg_per_ha = 1000.0F;
  weighed.crops = {oat};
  weighed.plan_grain_share = 0.28F;
  weighed.plan_positions = {{.crop = core::CropId{0}, .area_share = 0.15F}};
  core::WorldState plan;
  plan.calendar.day = core::kDaysPerYear;
  plan.chairman.raikom_reputation = 50.0F;
  const core::Grams plain = core::PlanDueGrams(weighed, plan, 0, 100.0F, 0, plan.calendar.day);
  weighed.plan_total_factor = 1.6F;
  const core::Grams heavy = core::PlanDueGrams(weighed, plan, 0, 100.0F, 0, plan.calendar.day);
  failures += Expect(std::llabs(plain - 4200 * core::kGramsPerKilogram) <= 2 &&
                         std::llabs(heavy - 6720 * core::kGramsPerKilogram) <= 7,
                     "the plan's weight ×1.6 takes the oat's 4 200 kg to 6 720 kg");
  return failures;
}
