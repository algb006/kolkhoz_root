// Why each idle hand stands idle (placement_diagnosis.h).

#include "placement_diagnosis.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <vector>

#include "assignment.h"
#include "placement_rank.h"

namespace core {

void DiagnosePlacement(const PlacedPlan& plan, PlacementDiagnosis& diagnosis) {
  const std::vector<AssignmentJob>& jobs = *plan.jobs;
  const std::vector<AssignmentCandidate>& candidates = *plan.candidates;
  const AssignmentParams& params = *plan.params;
  const std::vector<std::optional<JobShortfall>>& shortfall = *plan.shortfall;
  diagnosis.shortfall = shortfall;
  diagnosis.idle.assign(candidates.size(), std::nullopt);
  diagnosis.moved_off_care = *plan.released;
  // Each idle person asked of every job left short, the first reason in
  // IdleReason's order kept: a horse missing outranks a full brigade,
  // which outranks his road, and so on — EXCEPT kUnexplained, which
  // outranks them all. A hand fit for a job short of hands and left idle
  // is the plan contradicting its own rules; ranked last, as it was in the
  // first reading, any other short job he could not take answered for him,
  // and its nought on seed 1936 said nothing. Ranked first, it still read
  // nought over thirty years: a nought measured, not masked.
  const auto rank = [](IdleReason reason) {
    return reason == IdleReason::kUnexplained
               ? std::uint8_t{0}
               : static_cast<std::uint8_t>(static_cast<std::uint8_t>(reason) + 1U);
  };
  for (std::uint32_t index = 0; index < candidates.size(); ++index) {
    if ((*plan.result)[index] != kNoJobAssigned) {
      continue;
    }
    std::optional<IdleReason> reason;
    for (std::uint32_t job_index = 0; job_index < jobs.size(); ++job_index) {
      if (!shortfall[job_index]) {
        continue;
      }
      // As the queue judged it: by the ride where the brigade's cart, the
      // people's cart or the meadow's mower went, on foot where the mower
      // did not.
      AssignmentJob job = jobs[job_index];
      job.cart_out = (*plan.cart_today)[job_index] != 0;
      job.harnessed = job.harnessed && (*plan.mowers_walk)[job_index] == 0;
      RankedPick pick;
      bool by_road = false;
      IdleReason here = IdleReason::kUnexplained;
      if (ConsiderCandidate(job, job_index, candidates[index], index, params, pick, by_road)) {
        if (*shortfall[job_index] == JobShortfall::kNoHorse) {
          here = IdleReason::kNoHorse;
        } else if (*shortfall[job_index] == JobShortfall::kCrewCap) {
          here = IdleReason::kCrewCap;
        } else if (job.harnessed && !IsMeadowCut(job)) {
          // A carter left over with no horse is judged on foot (the queue's
          // own rule); the mower is not. LIMIT: whether a horse was still
          // free at his turn is not replayed, so a carter skipped while
          // horses remained and unable to walk reads kRoad or kNoDayLeft,
          // not kUnexplained.
          AssignmentJob on_foot = job;
          on_foot.harnessed = false;
          RankedPick walking;
          bool walk_by_road = false;
          if (!ConsiderCandidate(
                  on_foot, job_index, candidates[index], index, params, walking, walk_by_road)) {
            here = walk_by_road ? IdleReason::kRoad : IdleReason::kNoDayLeft;
          }
        }
      } else if (by_road) {
        here = IdleReason::kRoad;
      } else if (candidates[index].horse_locked && !(IsHorseWork(job.kind) || job.harnessed)) {
        here = IdleReason::kHorseLock;
      } else {
        here = IdleReason::kNoDayLeft;
      }
      // PROBE #994 (probe/994 only, never main): the first kUnexplained days
      // from KOLKHOZ_PROBE_994_FROM_DAY, with the job and the two skips of the
      // walkers' pass the diagnosis does not replay.
      if (here == IdleReason::kUnexplained) {
        static int printed = 0;
        static const char* const from_env = std::getenv("KOLKHOZ_PROBE_994_FROM_DAY");
        const long from = from_env == nullptr ? -1 : std::strtol(from_env, nullptr, 10);
        if (from >= 0 && static_cast<long>(params.rotation) >= from && printed < 400) {
          ++printed;
          AssignmentJob on_foot = job;
          on_foot.harnessed = false;
          const float one_way =
              OneWayHours(on_foot, job_index, candidates[index], params, false, false);
          const float trip_day = std::min(params.standard_day_hours, params.window_hours);
          std::uint32_t placed = 0;
          for (const std::uint32_t assigned : *plan.result) {
            placed += assigned == job_index ? 1U : 0U;
          }
          std::printf(
              "PROBE994 day=%u resident_row=%u job=%u kind=%u shortfall=%u placed=%u max_crew=%u "
              "left_days=%.2f harnessed=%d walker_log=%d log_pair_open=%d supplies_works=%d "
              "rick_waits=%d cart_today=%u one_way_h=%.2f trips=%.2f min_trips=%.2f\n",
              params.rotation,
              candidates[index].resident_row,
              job_index,
              static_cast<unsigned>(job.kind),
              static_cast<unsigned>(*shortfall[job_index]),
              placed,
              static_cast<unsigned>(job.max_crew),
              static_cast<double>(job.work_days_remaining),
              job.harnessed ? 1 : 0,
              job.walker_log ? 1 : 0,
              job.log_pair_open ? 1 : 0,
              job.supplies_works ? 1 : 0,
              job.rick_waits_for_a_cart ? 1 : 0,
              static_cast<unsigned>((*plan.cart_today)[job_index]),
              static_cast<double>(one_way),
              static_cast<double>(one_way > 0.0F ? trip_day / (2.0F * one_way) : -1.0F),
              static_cast<double>(params.walker_min_trips_per_day));
        }
      }
      if (!reason || rank(here) < rank(*reason)) {
        reason = here;
      }
    }
    diagnosis.idle[index] = reason ? *reason : IdleReason::kWorkCovered;
  }
}

}  // namespace core
