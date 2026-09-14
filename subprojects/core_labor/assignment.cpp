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
    // Felling comes last of the real work (2026-09-13). It has no window —
    // "круглый год" (timber design §8a) — so it only ever meets the other
    // windowless kinds here, and among them the carting of what is already
    // down goes first: a felled log lying in the grove builds nothing, and
    // felling more while it lies there only lengthens the heap.
    case WorkKind::kFelling:
      return 7;
    // Digging clay, stone and sand stands beside felling and for its reason:
    // no window, all the year round (construction design §3; boss, parcel
    // 270), and what is already dug and lying goes first.
    case WorkKind::kExtraction:
      return 8;
    // A producing unit's work is done by its post holders before the
    // accountant plans (labor_system.cpp, AssignPostHolders), so it meets the
    // queue only through a standing order; it ranks after felling, whose logs
    // it saws: a sawyer at an empty log pile turns out nothing.
    case WorkKind::kUnitWork:
      return 9;
    // Not a kind of work, and neither is the terminator. Handled beside
    // kNone so this switch keeps no default and a genuinely new kind stays
    // a build error here — which is exactly where a new kind must declare
    // where it stands in the queue.
    case WorkKind::kNone:
    case WorkKind::kWorkKindCount:
      return 10;
  }
  return 10;
}

/// The stable identity of a job's target, for deterministic tie-breaks.
constexpr std::uint32_t TargetIdValue(const AssignmentJob& job) {
  if (job.kind == WorkKind::kHerdCare) {
    return job.herd.value;
  }
  if (job.kind == WorkKind::kConstruction || job.kind == WorkKind::kUnitWork) {
    return job.unit.value;
  }
  // A stand's id and a field's may be the same number; a tie there falls to
  // the job's place in the list, which is deterministic (the comparator).
  if (job.stand.value != kInvalidEntityIdValue) {
    return job.stand.value;
  }
  if (job.extraction_site.value != kInvalidEntityIdValue) {
    return job.extraction_site.value;
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

/// Deterministic job order: TIER first (window open, then overdue, then the
/// meadow cut in its window, then the winter preparation, then the rest), and
/// only inside a tier the days left, the kind and the target id.
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
  //
  // AND A FOURTH BETWEEN THE LAST TWO (boss, parcel 233): the preparation of
  // a fallow for this autumn's winter crop has a window, and still yields to
  // every job that has one of its own — bread in the field before a sowing to
  // come — while going ahead of work with no window at all.
  //
  // AND THE MEADOW CUT IN ITS WINDOW ABOVE THAT ONE, and below the overdue
  // (boss, parcels 258-262; farming design, the row before "Окно в один
  // месяц"): open, overdue, the cut in June-July, the fallow for rye, then
  // the rest — the grass left uncut after July among it. Each place was
  // measured on nine seeds with no orders. The cut with no window ranked
  // below the fallow, and the first year's hay went to the rye (seed 9 of the
  // host's party: 140 t -> 110 t, 17 cows of 33 starved in the second winter).
  // As open work it took the spring sowing that always runs into June: 11 ha
  // sown of 66. Left overdue after July it outranked the fallow again and the
  // rye was lost for two years.
  const auto tier = [](const AssignmentJob& job) {
    if (job.prepares_winter_crop) {
      return 3;
    }
    // The meadow cut is the one harvest that rides out (labor_system.cpp,
    // CollectJobs sets `harnessed` for a meadow and nothing else).
    const bool meadow_cut = job.kind == WorkKind::kHarvest && job.harnessed;
    if (meadow_cut) {
      return job.window.kind == DeadlineKind::kDays ? 2 : 4;
    }
    switch (job.window.kind) {
      case DeadlineKind::kDays:
        return 0;
      case DeadlineKind::kOverdue:
        return 1;
      default:
        return 4;
    }
  };
  std::ranges::sort(order, [&jobs, &tier](std::uint32_t left, std::uint32_t right) {
    const AssignmentJob& a = jobs[left];
    const AssignmentJob& b = jobs[right];
    if (tier(a) != tier(b)) {
      return tier(a) < tier(b);
    }
    // BOTH SIDES ASKED, not just the left one: a comparator that reads a's
    // kind and b's NUMBER gives a different answer depending on which
    // argument the sort hands it first, which is not an ordering at all.
    //
    // AND THE TIER ABOVE WAS EDITED, as this comment once warned it would be:
    // since 2026-09-14 the winter-preparation tier (3 since the meadow cut's tier) can hold an open
    // window and an overdue one together. Compared by days for one pair and by work kind for
    // another, three such jobs made a cycle — no ordering, and undefined behaviour in the sort
    // (UB-001 of the 0.23.0 cycle). So inside a tier the window's kind goes first, open before
    // overdue before none, and only then its days.
    const auto window_rank = [](const AssignmentJob& job) {
      switch (job.window.kind) {
        case DeadlineKind::kDays:
          return 0;
        case DeadlineKind::kOverdue:
          return 1;
        default:
          return 2;
      }
    };
    if (window_rank(a) != window_rank(b)) {
      return window_rank(a) < window_rank(b);
    }
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
