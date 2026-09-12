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

/// Kind order INSIDE A TIER: the barn first, then the field work that loses
/// most by waiting. The barn leads because its demand is small, daily and
/// alive — an unfed cow is not a delayed job — and because the reaping crew
/// would otherwise swallow every hand in the village (found by the labor_year
/// run: day 31 left the cows unserved).
///
/// WHAT THE TIE IS, SINCE 2026-09-12: barn care carries kDays 0 and so does a
/// field whose window closes TODAY, and those are the two that meet here. A
/// field whose window has already RUN OUT is kOverdue — a tier below both,
/// and no longer a tie at all. This comment said "the tie only ever happens
/// when a field's window has ALSO run out" while that was true, and the three
/// tiers made it false the same evening.
constexpr std::uint8_t KindPriority(WorkKind kind) {
  switch (kind) {
    case WorkKind::kHerdCare:
      return 0;
    case WorkKind::kHarvest:
      return 1;
    case WorkKind::kSowing:
      return 2;
    case WorkKind::kPlowing:
      return 3;
    case WorkKind::kHarrowing:
      return 4;
    // Building comes after every field work on purpose: "full speed in
    // spring and autumn, but the people are needed in the fields"
    // (construction design §8). A site waits; a sowing window does not.
    case WorkKind::kConstruction:
      return 5;
    // Hauling comes after building and before nothing at all. It is the one
    // work whose urgency is carried entirely by its WINDOW rather than by
    // its kind: a load waiting on a field before the snow is the most urgent
    // thing in the village, and the same load in June can wait a week. The
    // window is what says which (task A4).
    case WorkKind::kHauling:
      return 6;
    // Not a kind of work, and neither is the terminator. Handled beside
    // kNone so this switch keeps no default and a genuinely new kind stays
    // a build error here — which is exactly where a new kind must declare
    // where it stands in the queue.
    case WorkKind::kNone:
    case WorkKind::kWorkKindCount:
      return 7;
  }
  return 6;
}

/// The stable identity of a job's target, for deterministic tie-breaks.
constexpr std::uint32_t TargetIdValue(const AssignmentJob& job) {
  if (job.kind == WorkKind::kHerdCare) {
    return job.herd.value;
  }
  if (job.kind == WorkKind::kConstruction) {
    return job.unit.value;
  }
  return job.field.value;
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
float TravelHours(const Vec2& home, const Vec2& place, float hours_per_km) {
  const float dx_km = (home.x - place.x) / 1000.0F;
  const float dy_km = (home.y - place.y) / 1000.0F;
  return std::sqrt((dx_km * dx_km) + (dy_km * dy_km)) * hours_per_km;
}

/// Deterministic job order: TIER first (window open, then overdue, then no
/// window), and only inside a tier the days left, the kind and the target id.
/// Input position is the last resort only for degenerate duplicate targets.
std::vector<std::uint32_t> OrderJobs(const std::vector<AssignmentJob>& jobs) {
  std::vector<std::uint32_t> order;
  order.reserve(jobs.size());
  for (std::uint32_t index = 0; index < jobs.size(); ++index) {
    if (jobs[index].kind != WorkKind::kNone && jobs[index].work_days_remaining > 0.0F) {
      order.push_back(index);
    }
  }
  // THREE TIERS, AND THEY ARE NOT ONE SCALE (boss, 2026-09-12). Work whose
  // window is still open goes first and is ranked by the days it has left;
  // then ALL the overdue work, whatever its age; then work that has no
  // window at all. Between two overdue jobs the age of the miss is not a
  // reason to prefer either, and sorting on it would be the defect of the
  // single integer turned round rather than repaired: the plough that missed
  // its window still has to turn the ground, and no arithmetic on how badly
  // it missed makes that more or less true.
  const auto tier = [](const AssignmentJob& job) {
    switch (job.window.kind) {
      case DeadlineKind::kDays:
        return 0;
      case DeadlineKind::kOverdue:
        return 1;
      default:
        return 2;
    }
  };
  std::ranges::sort(order, [&jobs, &tier](std::uint32_t left, std::uint32_t right) {
    const AssignmentJob& a = jobs[left];
    const AssignmentJob& b = jobs[right];
    if (tier(a) != tier(b)) {
      return tier(a) < tier(b);
    }
    // BOTH SIDES ASKED, not just the left one. Inside a tier the two kinds
    // are always the same, so one test would do — until the tier above is
    // edited, and then a comparator that reads a's kind and b's NUMBER gives
    // a different answer depending on which argument the sort hands it
    // first, which is not an ordering at all.
    if (a.window.kind == DeadlineKind::kDays && b.window.kind == DeadlineKind::kDays &&
        a.window.days != b.window.days) {
      return a.window.days < b.window.days;
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
  const bool horse_work = IsHorseWork(job.kind) || job.harnessed;
  if (candidate.horse_locked && !horse_work) {
    return false;  // The start-canon lock: only horse works may take him.
  }
  // One shoulder, two uses (decision 103): the same travel decides whether
  // he may be sent at all and how much of his day is left to work.
  const float travel =
      TravelHours(candidate.home,
                  job.position,
                  horse_work ? params.harness_hours_per_km : params.walk_hours_per_km);
  if (travel > params.travel_limit_hours) {
    return false;  // The road limit is a game rule, not accountant quality.
  }
  const float usable_hours = params.window_hours - (2.0F * travel);
  if (usable_hours < params.min_usable_hours) {
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
  // The rotated tiebreaker (assignment.h): row plus the day, modulo the
  // roster. Without it the queue is the birth order and never advances.
  const auto turn = [&params](std::uint32_t row) {
    return params.roster == 0 ? row : (row + params.rotation) % params.roster;
  };
  std::ranges::sort(picks, [&turn](const RankedPick& left, const RankedPick& right) {
    if (left.prefer != right.prefer) {
      return left.prefer;
    }
    if (left.score != right.score) {
      return left.score > right.score;
    }
    return turn(left.resident_row) < turn(right.resident_row);
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
    // A harnessed job takes a horse out of the day's pool exactly as
    // ploughing does — which is what assignment.h has promised since the
    // meadow cut was written, and what this line did not do until task A4
    // came to rely on it. Without it the village could field an unlimited
    // number of horse mowers, and hauling would have put a cart behind
    // every carrier at once.
    const bool horse_work = IsHorseWork(job.kind) || job.harnessed;
    if (horse_work && horses_left == 0) {
      continue;
    }

    // Fill until today's demand is covered: no more hands than the day can
    // consume — the surplus idles and earns nothing (digest 2026-08-29
    // §2.4: a trudoden is a work norm, not attendance).
    float expected_output = 0.0F;
    std::uint32_t placed = 0;
    for (const RankedPick& pick : RankCandidates(job, candidates, result, params)) {
      if (expected_output >= job.work_days_remaining) {
        break;
      }
      // The job's own ceiling, where it has one: a build class's brigade
      // caps a site regardless of how much work is left on it.
      if (job.max_crew != 0 && placed >= job.max_crew) {
        break;
      }
      // A HORSE IS TAKEN IF ONE IS FREE. Whether its absence STOPS the work
      // is a different question, and only ploughing and harrowing answer it
      // yes — those are IsHorseWork, and a man cannot pull a plough. Mowing
      // without a horse is a scythe, and carrying without one is a back:
      // slower, and still work.
      //
      // Measured, because the difference is not academic: while every
      // harnessed job demanded an animal, the carts took all sixteen horses
      // every day — hauling always outranked ploughing — and the farm opened
      // its ploughing and then sent nobody to it, year after year.
      if (horse_work) {
        if (horses_left > 0) {
          --horses_left;
        } else if (IsHorseWork(job.kind)) {
          break;  // no horse, no plough: this job cannot be done at all today
        }
      }
      result[pick.candidate_index] = job_index;
      expected_output += pick.daily_norm;
      ++placed;
    }
  }

  return result;
}

}  // namespace core
