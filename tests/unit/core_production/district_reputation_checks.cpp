// The checks of the rating earned beyond the plan (district_reputation_checks.h).
//
// THE KNOWN ANSWERS, by hand: the limit's tiers 5 t and 20 t (limit_catalog.h's defaults); oat a
// plan position whose gram is a gram of grain, milk a gram of a fifth of one and NO sowing-plan
// position. Oat due 10 t, delivered 13 t, 40 t: 3 t and 30 t over - tiers 1 and 3. Milk 100 t over
// its due adds nothing (grain and potato only). At +1 a tier capped at 3, 30 t over raises 50 to
// 53; at +2 a tier the cap holds 53. A surplus of 3 t that is all a district loan's grain reaches
// no tier. A clean visit with no recovery owed steps nothing; after a failed year it steps +1 and
// the debt clears, so the next clean visit steps nothing. The plan's weight ×1.6 multiplies a
// position's 4 200 kg to 6 720 kg.
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

constexpr core::Grams kTonne = core::kGramsPerTonne;

core::ProductionConfig RatingConfig() {
  core::ProductionConfig config;
  config.limit.overfulfil_tier1_t = 5.0F;
  config.limit.overfulfil_tier2_t = 20.0F;
  config.limit.overfulfil_grain_kcal_per_gram = 3.0F;
  config.food_kcal_per_gram = {3.0F, 0.6F};  // oat, milk
  core::CropDef oat;
  oat.resource = core::ResourceId{0};
  oat.yield_kg_per_ha = 1000.0F;
  config.crops = {oat};
  config.plan_positions = {{.crop = core::CropId{0}, .area_share = 0.15F}};
  config.plan_overfulfil_reputation_per_tier = 1.0F;
  config.plan_overfulfil_reputation_max = 3.0F;
  config.district_visit_clean_reputation = 1.0F;
  return config;
}

/// A year whose oat was due 10 t and delivered `oat_t`, and whose milk was 100 t over.
core::WorldState RatingWorld(core::Grams oat_t) {
  core::WorldState world;
  world.chairman.raikom_reputation = 50.0F;
  world.plan.due = {10 * kTonne, 10 * kTonne};
  world.plan.delivered = {oat_t * kTonne, 110 * kTonne};
  world.ledger.current.goods_loan_taken.assign(2, 0);
  return world;
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

  // GRAIN AND POTATO ONLY: the milk's 100 t over adds nothing.
  core::WorldState exact = RatingWorld(10);
  failures += Expect(core::OwnOverfulfilGrainTonnes(config, exact) == 0.0F,
                     "grain exactly at due and milk 100 t over: no own over-tonnes");
  core::StepReputationBeyondPlan(config, exact);
  failures += Expect(exact.chairman.raikom_reputation == 50.0F &&
                         exact.ledger.current.reputation_tiers_reached == 0,
                     "and no tier, the rating unmoved");

  core::WorldState thirty = RatingWorld(40);
  core::StepReputationBeyondPlan(config, thirty);
  failures += Expect(thirty.chairman.raikom_reputation == 53.0F &&
                         thirty.ledger.current.reputation_tiers_reached == 3 &&
                         thirty.ledger.current.reputation_own_overfulfil_t == 30.0F &&
                         thirty.ledger.current.reputation_tier_step == 3.0F,
                     "30 t of oat over: three tiers at +1 raise 50 to 53, booked by writer");
  core::ProductionConfig doubled = config;
  doubled.plan_overfulfil_reputation_per_tier = 2.0F;
  core::WorldState capped = RatingWorld(40);
  core::StepReputationBeyondPlan(doubled, capped);
  failures +=
      Expect(capped.chairman.raikom_reputation == 53.0F, "at +2 a tier the cap of 3 a year holds");
  core::ProductionConfig off = config;
  off.plan_overfulfil_reputation_per_tier = 0.0F;
  core::WorldState quiet = RatingWorld(40);
  core::StepReputationBeyondPlan(off, quiet);
  failures += Expect(quiet.chairman.raikom_reputation == 50.0F &&
                         quiet.ledger.current.reputation_tiers_reached == 3,
                     "off (the table's default) the rating does not move, the tiers are booked");

  // THE PUMP SHUT: a surplus that is all the district's lent grain.
  core::WorldState lent = RatingWorld(13);
  lent.ledger.current.goods_loan_taken[0] = 3 * kTonne;
  failures += Expect(core::OwnOverfulfilGrainTonnes(config, lent) == 0.0F,
                     "a surplus of 3 t that is all a loan's grain is none of the farm's own");
  core::StepReputationBeyondPlan(config, lent);
  failures +=
      Expect(lent.chairman.raikom_reputation == 50.0F, "a loan-only surplus buys no rating");

  // A CLEAN VISIT RECOVERS, ONLY WHILE OWED.
  core::WorldState visits = RatingWorld(10);
  core::StepReputationForCleanVisit(config, visits);
  failures += Expect(visits.chairman.raikom_reputation == 50.0F &&
                         visits.ledger.current.reputation_clean_visits == 1,
                     "a clean visit with nothing owed steps nothing, and is counted");
  core::OweReputationRecovery(visits);
  core::StepReputationForCleanVisit(config, visits);
  failures += Expect(visits.chairman.raikom_reputation == 51.0F &&
                         visits.chairman.reputation_recovery_owed == 0 &&
                         visits.ledger.current.reputation_visit_step == 1.0F,
                     "after a failed year the first clean visit recovers +1 and clears the debt");
  core::StepReputationForCleanVisit(config, visits);
  failures += Expect(visits.chairman.raikom_reputation == 51.0F &&
                         visits.ledger.current.reputation_clean_visits == 3,
                     "and the next clean visit steps nothing");
  visits.chairman.raikom_reputation = 99.5F;
  core::OweReputationRecovery(visits);
  core::StepReputationForCleanVisit(config, visits);
  failures += Expect(visits.chairman.raikom_reputation == 100.0F,
                     "a recovery at +1 raises 99.5 to 100 and no further");

  // A LOSS IS BOOKED by what it took.
  visits.chairman.raikom_reputation = 40.0F;
  core::BookReputationLoss(visits, 48.0F);
  failures += Expect(visits.ledger.current.reputation_losses == 8.0F,
                     "a loss of 8 is booked as 8 taken off");

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
