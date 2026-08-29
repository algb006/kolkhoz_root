// The accountant's placement algorithm (assignment.h). Pure and
// deterministic: total ordering over the inputs, no RNG even at the naive
// level — "whoever is there" means stable resident order, not chance.
//
// The shape of the scoring — how much skill weighs against raw output, how
// hard fatigue discounts a pick — is the accountant's heuristic itself, not
// world balance, so it lives here as named constants; the polish backlog
// owns tuning it against playtests (society design §1: the levels and what
// each one starts to see).

#include "assignment.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace core {
namespace {

/// Kind order at equal window urgency: permanent-loss work first, daily
/// care last (a missed harvest day is gone; a hungry cow catches up).
constexpr std::uint8_t KindPriority(WorkKind kind) {
  switch (kind) {
    case WorkKind::kHarvest:
      return 0;
    case WorkKind::kSowing:
      return 1;
    case WorkKind::kPlowing:
      return 2;
    case WorkKind::kHarrowing:
      return 3;
    case WorkKind::kHerdCare:
      return 4;
    case WorkKind::kNone:
      return 5;
  }
  return 5;
}

/// The stable identity of a job's target, for deterministic tie-breaks.
constexpr std::uint32_t TargetIdValue(const AssignmentJob& job) {
  return job.kind == WorkKind::kHerdCare ? job.herd.value : job.field.value;
}

/// How much the skill blend lifts a pick: 0.7 at skill 0, 1.0 at 100.
constexpr float kSkillWeightBase = 0.7F;
constexpr float kSkillWeightSpan = 0.3F;

constexpr float SkillWeight(Metric skill) {
  return kSkillWeightBase + (kSkillWeightSpan * (skill / 100.0F));
}

/// The experienced accountant's fatigue caution: full trust at rest >= 50,
/// down to half for someone about to walk off.
constexpr float kFatigueTrustRest = 50.0F;

constexpr float FatigueDiscount(Metric rest) {
  if (rest >= kFatigueTrustRest) {
    return 1.0F;
  }
  return 0.5F + (0.5F * (rest / kFatigueTrustRest));
}

/// One-way travel between a home and a work place, in game hours.
float TravelHours(const Vec2& home, const Vec2& place, const AssignmentParams& params) {
  const float dx_km = (home.x - place.x) / 1000.0F;
  const float dy_km = (home.y - place.y) / 1000.0F;
  return std::sqrt((dx_km * dx_km) + (dy_km * dy_km)) * params.walk_hours_per_km;
}

/// Deterministic job order: urgency, then kind, then target id. Input
/// position is the last resort only for degenerate duplicate targets.
std::vector<std::uint32_t> OrderJobs(const std::vector<AssignmentJob>& jobs) {
  std::vector<std::uint32_t> order;
  order.reserve(jobs.size());
  for (std::uint32_t index = 0; index < jobs.size(); ++index) {
    if (jobs[index].kind != WorkKind::kNone && jobs[index].work_days_remaining > 0.0F) {
      order.push_back(index);
    }
  }
  std::ranges::sort(order, [&jobs](std::uint32_t left, std::uint32_t right) {
    const AssignmentJob& a = jobs[left];
    const AssignmentJob& b = jobs[right];
    if (a.window_days_left != b.window_days_left) {
      return a.window_days_left < b.window_days_left;
    }
    if (KindPriority(a.kind) != KindPriority(b.kind)) {
      return KindPriority(a.kind) < KindPriority(b.kind);
    }
    if (TargetIdValue(a) != TargetIdValue(b)) {
      return TargetIdValue(a) < TargetIdValue(b);
    }
    return left < right;
  });
  return order;
}

/// A worker considered for one job, with everything the pick order needs.
struct RankedPick {
  std::uint32_t candidate_index = 0;

  std::uint32_t resident_row = 0;

  /// Norm man-days this worker would deliver on this job today.
  float daily_norm = 0.0F;

  float score = 0.0F;

  /// Sort-first flag on horse works from level 1 up: a horse-locked
  /// resident is useless anywhere else, so spending him here preserves
  /// everyone else's flexibility.
  bool prefer = false;
};

/// The accountant's eye on one worker for one job: nothing (still busy,
/// barred or out of reach) or a ranked pick.
bool ConsiderCandidate(const AssignmentJob& job,
                       const AssignmentCandidate& candidate,
                       std::uint32_t candidate_index,
                       const AssignmentParams& params,
                       RankedPick& pick) {
  const bool horse_work = IsHorseWork(job.kind);
  if (candidate.horse_locked && !horse_work) {
    return false;  // The start-canon lock: only horse works may take him.
  }
  const float travel = TravelHours(candidate.home, job.position, params);
  if (travel > params.travel_limit_hours) {
    return false;  // The road limit is a game rule, not accountant quality.
  }
  const float usable_hours = params.window_hours - (2.0F * travel);
  if (usable_hours <= 0.0F) {
    return false;
  }
  const float daily_norm = usable_hours * candidate.efficiency / params.standard_day_hours;
  if (daily_norm <= 0.0F) {
    return false;
  }

  float score = 0.0F;  // Level 0 sees nothing: stable order decides.
  if (params.placement_level == 1) {
    // Skill and strength, blind to the road and to fatigue.
    score = candidate.efficiency * SkillWeight(candidate.skill);
  } else if (params.placement_level >= 2) {
    // Everything: daily_norm already prices the road in; fatigue discounts
    // a probable walk-off. Level 3 pair synergy is a STUB.
    score = daily_norm * SkillWeight(candidate.skill) * FatigueDiscount(candidate.rest);
  }
  pick = RankedPick{
      .candidate_index = candidate_index,
      .resident_row = candidate.resident_row,
      .daily_norm = daily_norm,
      .score = score,
      .prefer = horse_work && candidate.horse_locked && params.placement_level >= 1,
  };
  return true;
}

/// Everyone still free who may and can reach this job today, best first.
std::vector<RankedPick> RankCandidates(const AssignmentJob& job,
                                       const std::vector<AssignmentCandidate>& candidates,
                                       const std::vector<std::uint32_t>& result,
                                       const AssignmentParams& params) {
  std::vector<RankedPick> picks;
  for (std::uint32_t index = 0; index < candidates.size(); ++index) {
    if (result[index] != kNoJobAssigned) {
      continue;
    }
    RankedPick pick;
    if (ConsiderCandidate(job, candidates[index], index, params, pick)) {
      picks.push_back(pick);
    }
  }
  std::ranges::sort(picks, [](const RankedPick& left, const RankedPick& right) {
    if (left.prefer != right.prefer) {
      return left.prefer;
    }
    if (left.score != right.score) {
      return left.score > right.score;
    }
    return left.resident_row < right.resident_row;
  });
  return picks;
}

}  // namespace

std::vector<std::uint32_t> PlanDayAssignments(const std::vector<AssignmentJob>& jobs,
                                              const std::vector<AssignmentCandidate>& candidates,
                                              const AssignmentParams& params) {
  std::vector<std::uint32_t> result(candidates.size(), kNoJobAssigned);
  std::uint32_t horses_left = params.draught_horses;

  for (const std::uint32_t job_index : OrderJobs(jobs)) {
    const AssignmentJob& job = jobs[job_index];
    const bool horse_work = IsHorseWork(job.kind);
    if (horse_work && horses_left == 0) {
      continue;
    }

    // Fill until today's demand is covered: no more hands than the day can
    // consume — the surplus idles and earns nothing (digest 2026-08-29
    // §2.4: a trudoden is a work norm, not attendance).
    float expected_output = 0.0F;
    for (const RankedPick& pick : RankCandidates(job, candidates, result, params)) {
      if (expected_output >= job.work_days_remaining) {
        break;
      }
      if (horse_work) {
        if (horses_left == 0) {
          break;
        }
        --horses_left;
      }
      result[pick.candidate_index] = job_index;
      expected_output += pick.daily_norm;
    }
  }

  return result;
}

}  // namespace core
