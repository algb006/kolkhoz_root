// The care's re-pricing after the people's carts (care_reprice.h; #709).

#include "care_reprice.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core_common/labor_state.h"
#include "placement_rank.h"

namespace core {
namespace {

/// One hand on a care, priced by his own way.
struct CareHand {
  std::uint32_t candidate_index = 0;
  std::uint32_t turn = 0;  ///< RankCandidates' rotated tiebreaker of his row
  float norm = 0.0F;
  float score = 0.0F;
  bool drives = false;
};

}  // namespace

std::uint32_t RepriceCare(const CareRepricePlan& plan) {
  const std::vector<AssignmentJob>& jobs = *plan.jobs;
  const std::vector<AssignmentCandidate>& candidates = *plan.candidates;
  std::vector<std::uint32_t>& result = *plan.result;
  std::uint32_t moved = 0;
  for (std::uint32_t job_index = 0; job_index < jobs.size(); ++job_index) {
    const AssignmentJob& job = jobs[job_index];
    if (job.kind != WorkKind::kHerdCare) {
      continue;
    }
    std::vector<CareHand> crew;
    float priced = 0.0F;
    for (std::uint32_t index = 0; index < candidates.size(); ++index) {
      if (result[index] != job_index) {
        continue;
      }
      const bool drives = plan.rides_horse != nullptr && (*plan.rides_horse)[index] != 0;
      const bool seated =
          plan.rides_cart_with != nullptr && (*plan.rides_cart_with)[index] != kNoJobAssigned;
      // BY HIS OWN WAY: riding when a cart of today carries him, on foot if not.
      AssignmentJob judged = job;
      judged.cart_out = (*plan.cart_today)[job_index] != 0 && (drives || seated);
      RankedPick pick;
      bool by_road = false;
      const AssignmentParams& params = *plan.params;
      const std::uint32_t place = candidates[index].queue_place;  // #1347: not the row
      CareHand hand{.candidate_index = index,
                    .turn = params.roster == 0 ? place : (place + params.rotation) % params.roster,
                    .drives = drives};
      if (ConsiderCandidate(judged, job_index, candidates[index], index, params, pick, by_road)) {
        hand.norm = pick.daily_norm;
        hand.score = pick.score;
      }
      priced += hand.norm;
      crew.push_back(hand);
    }
    if (!(priced > job.work_days_remaining)) {
      (*plan.covered)[job_index] = priced;
      continue;
    }
    // THE LOWEST-RANKED FIRST: RankCandidates' order turned round - the
    // smallest score, and on a tie the latest by its rotated tiebreaker.
    std::ranges::stable_sort(crew, [](const CareHand& left, const CareHand& right) {
      if (left.score != right.score) {
        return left.score < right.score;
      }
      return left.turn > right.turn;
    });
    for (const CareHand& hand : crew) {
      if (hand.drives || !(priced - hand.norm >= job.work_days_remaining)) {
        continue;
      }
      result[hand.candidate_index] = kNoJobAssigned;
      if (plan.rides_cart_with != nullptr) {
        (*plan.rides_cart_with)[hand.candidate_index] = kNoJobAssigned;
      }
      (*plan.released)[hand.candidate_index] = 1U;
      priced -= hand.norm;
      ++moved;
    }
    (*plan.covered)[job_index] = priced;
  }
  return moved;
}

}  // namespace core
