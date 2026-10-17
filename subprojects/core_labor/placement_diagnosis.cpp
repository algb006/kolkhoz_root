// Why each idle hand stands idle (placement_diagnosis.h).

#include "placement_diagnosis.h"

#include <cstdint>
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
        } else if (*shortfall[job_index] == JobShortfall::kRoad) {
          // THE QUEUE SAID THE WAY (#994; boss [2]): its fill turned every hand
          // away by the road, and a replay that judges him fit judged him by a
          // transport the fill did not (probe/994: 400 of 400 kUnexplained
          // pairs of seed 1933's year 8 were felling stands the queue had
          // marked kRoad, replayed with cart_today 1). The queue's verdict is
          // the one filed.
          here = IdleReason::kRoad;
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
      if (!reason || rank(here) < rank(*reason)) {
        reason = here;
      }
    }
    diagnosis.idle[index] = reason ? *reason : IdleReason::kWorkCovered;
  }
}

}  // namespace core
