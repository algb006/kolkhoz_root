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

#include "core_common/geometry.h"

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
    // Planting a forest zone last of all (save 82): no window, and a planted
    // hectare pays back in five years — anything that feeds or houses the
    // village this year goes before it.
    case WorkKind::kPlanting:
      return 10;
    // Road work (delivery 7a declared it; its jobs come with 7e): last, for
    // now — windowless, and a road waits as a site does. Where it truly
    // stands against building and hauling is 7e's to measure and say.
    case WorkKind::kRoadWork:
      return 11;
    // Not a kind of work, and neither is the terminator. Handled beside
    // kNone so this switch keeps no default and a genuinely new kind stays
    // a build error here — which is exactly where a new kind must declare
    // where it stands in the queue.
    case WorkKind::kNone:
    case WorkKind::kWorkKindCount:
      return 12;
  }
  return 12;
}

/// The stable identity of a job's target, for deterministic tie-breaks.
constexpr std::uint32_t TargetIdValue(const AssignmentJob& job) {
  if (job.kind == WorkKind::kHerdCare) {
    return job.herd.value;
  }
  // The perevalka carries out of a unit (kEmptyStore): its target is the unit.
  if (job.kind == WorkKind::kConstruction || job.kind == WorkKind::kUnitWork ||
      job.unit.value != kInvalidEntityIdValue) {
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
  if (job.limit_delivery.value != kInvalidEntityIdValue) {
    return job.limit_delivery.value;
  }
  return job.field.value;
}

/// True for a job nobody can do without a horse: the plough and the harrow
/// (IsHorseWork), and the fetch of a timber lot from the district centre —
/// 25 km beyond the border is no carry for a back. Until 0.36.18 the lot's
/// carters fell to the on-foot rule once the horses were gone, and on the
/// canon forty walked for the logs every free day (seed 1931, year 2).
constexpr bool StopsWithoutHorse(const AssignmentJob& job) {
  return IsHorseWork(job.kind) || job.limit_delivery.value != kInvalidEntityIdValue;
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
    // CollectJobs sets `harnessed` on a harvest for a meadow and no other).
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
    // THE PLOUGH GOES TO THE PLAN'S FIELDS FIRST (boss, boss-core-epoch1-5 seq
    // 50; transport design §1): inside one tier and one kind of window, horse
    // work on a field whose crop carries a plan position goes before the
    // rest. On seed 1939 the potato — last by its window — stood unploughed
    // to August on a team of seven, three years running, to the trial.
    //
    // A KEY FOR EVERY JOB, not a rule between two ploughs: asked only when both
    // sides are horse work, it would rank a plan plough of day 10 before a
    // plain one of day 2, that one before a sowing of day 5, and the sowing
    // before the first — a cycle, UB-001's shape again. And the key LIFTS the
    // plan's ploughs rather than holding the others back: the first draft held
    // every plain plough behind every other job of its tier, and the carts took
    // the horses before it (three assertions red at once). Lifted, the plan's
    // ploughs go ahead of everything in their tier and window, and everything
    // else keeps the order it had.
    const auto plan_plough = [](const AssignmentJob& job) {
      return IsHorseWork(job.kind) && job.plan_position ? 0 : 1;
    };
    if (plan_plough(a) != plan_plough(b)) {
      return plan_plough(a) < plan_plough(b);
    }
    if (a.window.kind == DeadlineKind::kDays && b.window.kind == DeadlineKind::kDays &&
        a.window.days != b.window.days) {
      return a.window.days < b.window.days;
    }
    if (KindPriority(a.kind) != KindPriority(b.kind)) {
      return KindPriority(a.kind) < KindPriority(b.kind);
    }
    // THE LAST DAYS BEFORE THE SNOW (boss seq 95 and 103): between reapings
    // with one edge, the snow — first those the village can still finish,
    // then the heavier. KEYS AND NOT A PAIRWISE RULE, after the kind: asked
    // only when both sides carry grams, a zero-gram job of the same kind and
    // days compared by id with each made a cycle possible (UB-001's shape).
    // A job with no grams never carries beyond_the_snow, so it ranks as a
    // finishable one of weight nought — after every weighed reaping.
    if (a.beyond_the_snow != b.beyond_the_snow) {
      return !a.beyond_the_snow;
    }
    if (a.grams_at_risk != b.grams_at_risk) {
      return a.grams_at_risk > b.grams_at_risk;
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
                       std::uint32_t job_index,
                       const AssignmentCandidate& candidate,
                       std::uint32_t candidate_index,
                       const AssignmentParams& params,
                       RankedPick& pick,
                       bool& refused_by_road) {
  refused_by_road = false;
  const bool horse_work = IsHorseWork(job.kind) || job.harnessed;
  if (candidate.horse_locked && !horse_work) {
    return false;  // The start-canon lock: only horse works may take him.
  }
  // One shoulder, two uses (decision 103): the same travel decides whether
  // he may be sent at all and how much of his day is left to work. Felling
  // rides without being horse work (RidesOut; parcel 308).
  const bool rides = horse_work || RidesOut(job.kind);
  const float hours_per_km = rides ? params.harness_hours_per_km : params.walk_hours_per_km;
  // BY THE ROADS WHEN THE CALLER MEASURED THEM (AssignmentParams::road_km;
  // 0.36.2) — the way the labour hour will measure his day by; the straight
  // line otherwise.
  float travel = 0.0F;
  if (!params.road_km.empty() && candidate.home_slot < params.home_slots) {
    const std::size_t at =
        ((((static_cast<std::size_t>(job_index) * params.home_slots) + candidate.home_slot) * 2U) +
         (rides ? 1U : 0U));
    travel = at < params.road_km.size() ? params.road_km[at] * hours_per_km
                                        : TravelHours(candidate.home, job.position, hours_per_km);
  } else {
    travel = TravelHours(candidate.home, job.position, hours_per_km);
  }
  // The road limit is a game rule, not accountant quality.
  if (!RoadLeavesAWorkingDay(
          travel, params.window_hours, params.travel_limit_hours, params.min_usable_hours)) {
    refused_by_road = true;
    return false;
  }
  const float usable_hours = params.window_hours - (2.0F * travel);
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
/// `road_refused`: how many free workers the road rule alone turned away.
std::vector<RankedPick> RankCandidates(const AssignmentJob& job,
                                       std::uint32_t job_index,
                                       const std::vector<AssignmentCandidate>& candidates,
                                       const std::vector<std::uint32_t>& result,
                                       const AssignmentParams& params,
                                       std::uint32_t& road_refused) {
  std::vector<RankedPick> picks;
  road_refused = 0;
  for (std::uint32_t index = 0; index < candidates.size(); ++index) {
    if (result[index] != kNoJobAssigned) {
      continue;
    }
    RankedPick pick;
    bool by_road = false;
    if (ConsiderCandidate(job, job_index, candidates[index], index, params, pick, by_road)) {
      picks.push_back(pick);
    }
    road_refused += by_road ? 1U : 0U;
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
                                              const AssignmentParams& params,
                                              std::vector<std::uint8_t>* rides_horse,
                                              std::vector<std::uint8_t>* road_blocked) {
  std::vector<std::uint32_t> result(candidates.size(), kNoJobAssigned);
  if (rides_horse != nullptr) {
    rides_horse->assign(candidates.size(), 0U);
  }
  if (road_blocked != nullptr) {
    road_blocked->assign(jobs.size(), 0U);
  }
  std::uint32_t horses_left = params.draught_horses;

  for (const std::uint32_t job_index : OrderJobs(jobs)) {
    const AssignmentJob& job = jobs[job_index];
    // A harnessed job takes a horse out of the day's pool exactly as
    // ploughing does — which is what assignment.h has promised since the
    // meadow cut was written, and what this line did not do until task A4
    // came to rely on it. Without it the village could field an unlimited
    // number of horse mowers, and hauling would have put a cart behind
    // every carrier at once.
    //
    // EXCEPT THE MEADOW CUT, which takes ONE horse for the brigade (time design
    // §7: "косилка одна на бригаду, а не всадник на косца"; boss, parcel 312).
    // It took one a mower until 2026-09-14, and with no horse left it was not
    // cut at all — while the rule below says a scythe is still work.
    const bool meadow_cut = job.kind == WorkKind::kHarvest && job.harnessed;
    const bool horse_work = IsHorseWork(job.kind) || (job.harnessed && !meadow_cut);
    // ONLY THE PLOUGH, THE HARROW AND THE LOT'S FETCH FROM THE DISTRICT
    // (0.36.18) STOP FOR WANT OF A HORSE, as the rule below says (every
    // other cart about the village is a back without one). Until 2026-09-15
    // this skipped every harnessed job once the
    // pool was empty, so the autumn ploughing took the last horse and the
    // carts behind it were not offered at all: on seed 1936 the vegetables
    // lay a day with 45 hands idle, and a December potato load went to the
    // snow. A cart with no horse is a back.
    if (StopsWithoutHorse(job) && horses_left == 0) {
      continue;
    }
    if (meadow_cut && horses_left > 0) {
      --horses_left;  // the mower, once for the whole brigade
    }

    // Fill until today's demand is covered: no more hands than the day can
    // consume — the surplus idles and earns nothing (digest 2026-08-29
    // §2.4: a trudoden is a work norm, not attendance).
    float expected_output = 0.0F;
    std::uint32_t placed = 0;
    std::uint32_t road_refused = 0;
    const std::vector<RankedPick> picks =
        RankCandidates(job, job_index, candidates, result, params, road_refused);
    // THE JOB THE ROAD STOPPED (econ, boss-econ-roads-balance [4] item 5б):
    // work left, nobody fit to take it, and free hands turned away by the
    // road rule alone. A job nobody free could take for other reasons —
    // every hand already placed, a horse lock — is not the road's.
    if (road_blocked != nullptr && picks.empty() && road_refused > 0 &&
        job.work_days_remaining > 0.0F) {
      (*road_blocked)[job_index] = 1U;
    }
    for (const RankedPick& pick : picks) {
      if (expected_output >= job.work_days_remaining) {
        break;
      }
      // The job's own ceiling, where it has one: a build class's brigade
      // caps a site regardless of how much work is left on it.
      if (job.max_crew != 0 && placed >= job.max_crew) {
        break;
      }
      // A HORSE IS TAKEN IF ONE IS FREE. Whether its absence STOPS the work
      // is a different question, and ploughing and harrowing answer it yes —
      // those are IsHorseWork, and a man cannot pull a plough — and so, since
      // 0.36.18, does the fetch of a timber lot from the district
      // (StopsWithoutHorse). Mowing without a horse is a scythe, and carrying
      // about the village without one is a back: slower, and still work.
      //
      // Measured, because the difference is not academic: while every
      // harnessed job demanded an animal, the carts took all sixteen horses
      // every day — hauling always outranked ploughing — and the farm opened
      // its ploughing and then sent nobody to it, year after year.
      bool took_horse = false;
      float daily_norm = pick.daily_norm;
      if (horse_work) {
        if (horses_left > 0) {
          --horses_left;
          took_horse = true;
        } else if (StopsWithoutHorse(job)) {
          break;  // no horse, no plough (nor a lot fetched): not done at all today
        } else {
          // A CARTER WITH NO HORSE IS JUDGED ON FOOT (boss, boss-core-topup-
          // horses seq 2): he was ranked by the ride, and until 0.34.51 he
          // was sent by it too — 2.5 km out at the trot with every horse in
          // the plough. The walk decides whether he may go and what his day
          // is worth; too far to walk, and the next in the queue is asked.
          AssignmentJob on_foot = job;
          on_foot.harnessed = false;
          RankedPick walking;
          bool walk_refused_by_road = false;  // he was ranked: the job is not the road's
          if (!ConsiderCandidate(on_foot,
                                 job_index,
                                 candidates[pick.candidate_index],
                                 pick.candidate_index,
                                 params,
                                 walking,
                                 walk_refused_by_road)) {
            continue;
          }
          daily_norm = walking.daily_norm;
        }
      }
      result[pick.candidate_index] = job_index;
      if (rides_horse != nullptr) {
        (*rides_horse)[pick.candidate_index] = took_horse ? 1U : 0U;
      }
      expected_output += daily_norm;
      ++placed;
    }
  }

  return result;
}

}  // namespace core
