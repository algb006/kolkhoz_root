// Unit test of core_labor. Stage 5, task F2: the accountant's placement
// algorithm — determinism against input order, the surplus-idles rule, the
// road limit, the horse pool and the horse lock, window urgency, and what
// each placement level sees. The subsystem boundary keeps its stage-1
// checks until the full system lands (task O1).

#include <cstdint>
#include <iostream>
#include <string_view>
#include <type_traits>
#include <vector>

#include "assignment.h"
#include "core_common/world_state.h"
#include "core_labor/labor_system.h"
#include "core_tables/tables.h"

static_assert(std::is_abstract_v<core::ILaborSystem>, "ILaborSystem is a contract");
static_assert(std::has_virtual_destructor_v<core::ILaborSystem>,
              "implementations are destroyed through the interface");

namespace {

int Expect(bool condition, const char* label) {
  if (condition) {
    return 0;
  }
  std::cout << "FAIL: " << label << '\n';
  return 1;
}

class EmptyTableSet final : public core::ITableSet {
 public:
  const core::ITable* FindTable(std::string_view /*name*/) const override { return nullptr; }

  std::uint32_t TableCount() const override { return 0; }

  std::string_view TableName(std::uint32_t /*index*/) const override { return {}; }
};

core::AssignmentJob FieldJob(core::WorkKind kind,
                             std::uint32_t field_id,
                             core::Vec2 position,
                             float work_days,
                             std::uint8_t window_days) {
  core::AssignmentJob job;
  job.kind = kind;
  job.field = core::FieldId{field_id};
  job.position = position;
  job.work_days_remaining = work_days;
  job.window_days_left = window_days;
  return job;
}

core::AssignmentCandidate Worker(std::uint32_t row, core::Vec2 home) {
  core::AssignmentCandidate candidate;
  candidate.resident_row = row;
  candidate.home = home;
  candidate.efficiency = 1.0F;
  candidate.rest = 70.0F;
  candidate.skill = 20.0F;
  return candidate;
}

/// Params with a wide-open day so geometry tests control the outcome.
core::AssignmentParams DayParams() {
  core::AssignmentParams params;
  params.window_hours = 12.0F;
  params.walk_hours_per_km = 3.1F;
  params.travel_limit_hours = 2.0F;
  params.standard_day_hours = 10.0F;
  params.draught_horses = 0;
  params.placement_level = 2;
  return params;
}

int TestSurplusIdles() {
  int failures = 0;
  // One tiny job, five hands: the crew stops when expected output covers
  // the demand — the rest idle (a trudoden is a work norm, not attendance).
  const std::vector<core::AssignmentJob> jobs = {
      FieldJob(core::WorkKind::kSowing, 1, {100.0F, 0.0F}, 0.5F, 3),
  };
  std::vector<core::AssignmentCandidate> candidates;
  for (std::uint32_t row = 0; row < 5; ++row) {
    candidates.push_back(Worker(row, {0.0F, 0.0F}));
  }
  const auto plan = core::PlanDayAssignments(jobs, candidates, DayParams());
  std::uint32_t assigned = 0;
  for (const std::uint32_t job : plan) {
    assigned += job != core::kNoJobAssigned ? 1 : 0;
  }
  failures += Expect(assigned == 1, "a half-day job takes one worker, four idle");
  return failures;
}

int TestRoadLimit() {
  int failures = 0;
  // 1 km straight-line at 3.1 h/km is over the 2-hour limit: unreachable
  // even when he is the only hand. 500 m is within it.
  const std::vector<core::AssignmentJob> jobs = {
      FieldJob(core::WorkKind::kSowing, 1, {1000.0F, 0.0F}, 5.0F, 3),
  };
  const std::vector<core::AssignmentCandidate> far = {Worker(0, {0.0F, 0.0F})};
  const auto none = core::PlanDayAssignments(jobs, far, DayParams());
  failures += Expect(none[0] == core::kNoJobAssigned, "a job beyond the road limit takes nobody");

  const std::vector<core::AssignmentCandidate> near = {Worker(0, {500.0F, 0.0F})};
  const auto one = core::PlanDayAssignments(jobs, near, DayParams());
  failures += Expect(one[0] == 0, "the same job within the limit takes the worker");
  return failures;
}

int TestHorsePoolAndLock() {
  int failures = 0;
  // Plowing wants ~4 workers, but two horses cap the crew at two; the
  // locked resident may take only horse work, so on a sowing day he idles.
  const core::Vec2 origin{0.0F, 0.0F};
  const std::vector<core::AssignmentJob> jobs = {
      FieldJob(core::WorkKind::kPlowing, 1, origin, 4.0F, 10),
      FieldJob(core::WorkKind::kSowing, 2, origin, 4.0F, 10),
  };
  std::vector<core::AssignmentCandidate> candidates;
  for (std::uint32_t row = 0; row < 6; ++row) {
    candidates.push_back(Worker(row, origin));
  }
  candidates[0].horse_locked = true;
  auto params = DayParams();
  params.draught_horses = 2;
  const auto plan = core::PlanDayAssignments(jobs, candidates, params);

  std::uint32_t plowing = 0;
  for (const std::uint32_t job : plan) {
    plowing += job == 0 ? 1 : 0;
  }
  failures += Expect(plowing == 2, "two horses cap the plowing crew at two");
  failures += Expect(plan[0] == 0, "the horse-locked resident is spent on horse work first");

  // Without horses the plowing job takes nobody, and the locked resident
  // cannot be placed on the sowing that remains.
  params.draught_horses = 0;
  const auto horseless = core::PlanDayAssignments(jobs, candidates, params);
  failures += Expect(horseless[0] == core::kNoJobAssigned,
                     "no horses: the locked resident idles even with sowing open");
  std::uint32_t on_plowing = 0;
  for (const std::uint32_t job : horseless) {
    on_plowing += job == 0 ? 1 : 0;
  }
  failures += Expect(on_plowing == 0, "no horses: nobody plows");
  return failures;
}

int TestWindowUrgency() {
  int failures = 0;
  // One worker, two jobs: the harvest window closes in 2 days, the plowing
  // in 30 — the urgent one wins regardless of input order.
  const core::Vec2 origin{0.0F, 0.0F};
  std::vector<core::AssignmentJob> jobs = {
      FieldJob(core::WorkKind::kPlowing, 1, origin, 3.0F, 30),
      FieldJob(core::WorkKind::kHarvest, 2, origin, 3.0F, 2),
  };
  const std::vector<core::AssignmentCandidate> candidates = {Worker(0, origin)};
  auto params = DayParams();
  params.draught_horses = 1;
  const auto plan = core::PlanDayAssignments(jobs, candidates, params);
  failures += Expect(plan[0] == 1, "the closing harvest window outranks relaxed plowing");

  // The same jobs in the opposite input order give the same choice by
  // target, not by position: determinism against input order.
  std::swap(jobs[0], jobs[1]);
  const auto swapped = core::PlanDayAssignments(jobs, candidates, params);
  failures += Expect(swapped[0] == 0, "input order does not change the chosen target");
  return failures;
}

int TestPlacementLevels() {
  int failures = 0;
  // A strong worker lives 600 m away (long walk), a weak one next to the
  // field. The naive level takes the first row; the experienced level
  // weighs the road in and takes the neighbor.
  const core::Vec2 field_at{0.0F, 0.0F};
  // Demand 0.9 norm-days: either worker covers it alone, so the crew is
  // one and the choice itself is what the levels disagree on.
  const std::vector<core::AssignmentJob> jobs = {
      FieldJob(core::WorkKind::kSowing, 1, field_at, 0.9F, 3),
  };
  std::vector<core::AssignmentCandidate> candidates = {
      Worker(0, {600.0F, 0.0F}),
      Worker(1, {50.0F, 0.0F}),
  };
  candidates[0].efficiency = 1.1F;
  candidates[1].efficiency = 0.9F;

  auto params = DayParams();
  params.placement_level = 0;
  const auto naive = core::PlanDayAssignments(jobs, candidates, params);
  failures += Expect(naive[0] == 0 && naive[1] == core::kNoJobAssigned,
                     "the naive level places by stable order, blind to the road");

  params.placement_level = 2;
  const auto experienced = core::PlanDayAssignments(jobs, candidates, params);
  failures += Expect(experienced[1] == 0 && experienced[0] == core::kNoJobAssigned,
                     "the experienced level prices the road in and takes the neighbor");
  return failures;
}

int TestBoundarySystemStub() {
  int failures = 0;
  const EmptyTableSet tables;
  const auto system = core::CreateLaborSystem(tables);
  failures += Expect(system != nullptr, "factory yields a system");
  if (system != nullptr) {
    const core::WorldState previous;
    core::WorldState current = previous;
    system->RunAssignmentDecisions(previous, current);
    failures += Expect(current.residents.rows.empty() && current.fields.rows.empty(),
                       "an empty world stays empty through the labor sub-step");
  }
  return failures;
}

}  // namespace

int main() {
  int failures = 0;
  failures += TestSurplusIdles();
  failures += TestRoadLimit();
  failures += TestHorsePoolAndLock();
  failures += TestWindowUrgency();
  failures += TestPlacementLevels();
  failures += TestBoundarySystemStub();
  if (failures == 0) {
    std::cout << "unit_core_labor: all checks passed\n";
  }
  return failures;
}
