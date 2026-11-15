// The checks of #1764's (A) (plan_credit_checks.h).

#include "plan_credit_checks.h"

#include <iostream>

#include "core_common/event_state.h"
#include "core_common/quantities.h"
#include "core_common/world_state.h"
#include "district_plan.h"
#include "production_config.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

constexpr core::Grams kKilo = 1000;

/// A plan of 1.6 t of rye (resource 1) with `delivered` shipped and `stolen` lost before
/// acceptance.
core::WorldState MakePlan(core::Grams delivered, core::Grams stolen) {
  core::WorldState world;
  world.plan.announced = 1;
  world.plan.due = {0, 1600 * kKilo};
  world.plan.delivered = {0, delivered};
  world.ledger.current.stolen_before_acceptance = {0, stolen};
  return world;
}

}  // namespace

int CheckThePlanCredit() {
  int failures = 0;
  core::ProductionConfig on;
  on.farming.plan_credit_pre_act_theft = true;
  const core::ProductionConfig off;
  // THE CREDIT: 0.5 t short, 2 t stolen before acceptance - forgiven with the row on, not
  // with it off (today); 0.5 t short and 0.3 t stolen - the chairman's; nothing short - no
  // credit to give.
  failures += Expect(core::FailedOnlyByPreActTheft(on, MakePlan(1100 * kKilo, 2000 * kKilo)),
                     "plan credit: a shortfall the theft before acceptance covers is forgiven");
  failures += Expect(!core::FailedOnlyByPreActTheft(off, MakePlan(1100 * kKilo, 2000 * kKilo)),
                     "plan credit: off by default - the verdict as today");
  failures += Expect(!core::FailedOnlyByPreActTheft(on, MakePlan(1100 * kKilo, 300 * kKilo)),
                     "plan credit: a shortfall above the theft is the chairman's");
  failures += Expect(!core::FailedOnlyByPreActTheft(on, MakePlan(1600 * kKilo, 2000 * kKilo)),
                     "plan credit: a delivered plan has nothing to forgive");

  // THE VERDICT: the series does not grow, the reputation still falls, the book says it.
  {
    core::WorldState world = MakePlan(1100 * kKilo, 2000 * kKilo);
    const float reputation = world.chairman.raikom_reputation;
    core::JudgePlan(on, world);
    bool said = false;
    for (const core::SimEvent& event : world.step_events) {
      said = said || (event.kind == core::EventKind::kPlanFailureForgiven &&
                      event.amount == 500 * kKilo && event.resource.value == 1);
    }
    failures += Expect(world.plan.failed_years_in_a_row == 0 &&
                           world.plan.last_verdict == core::PlanVerdict::kFailed,
                       "plan credit: the forgiven year is still failed, and counts no year");
    failures += Expect(world.chairman.raikom_reputation <= reputation,
                       "plan credit: the reputation still pays");
    failures += Expect(said, "plan credit: the book line names the position and its shortfall");
  }
  return failures;
}
