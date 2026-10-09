// The people's carts and the lone rider (people_cart.h).

#include "people_cart.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core_common/geometry.h"
#include "core_common/labor_state.h"
#include "placement_rank.h"

namespace core {
namespace {

/// A hand going to a far object: placed by the queue already, or a free one
/// the ride lets reach it.
struct FarHand {
  std::uint32_t candidate_index = 0;
  float walk_hours = 0.0F;
};

/// Whether the hand who holds the horse — on foot to the horse yard, on from
/// it — has a working day left on this job.
bool RiderHasADay(const AssignmentJob& job,
                  std::uint32_t job_index,
                  const AssignmentCandidate& rider,
                  const AssignmentParams& params) {
  return RoadLeavesAWorkingDay(OneWayHours(job, job_index, rider, params, true, true),
                               params.window_hours,
                               params.travel_limit_hours,
                               params.min_usable_hours);
}

}  // namespace

void GivePeoplesCarts(const PeoplesCartPlan& plan) {
  const AssignmentParams& params = *plan.params;
  if (params.people_cart_seats == 0) {
    return;
  }
  const std::vector<AssignmentCandidate>& candidates = *plan.candidates;
  std::vector<std::uint32_t>& result = *plan.result;
  const std::uint32_t per_cart = params.people_cart_seats + 1U;  // and its driver
  for (const std::uint32_t job_index : *plan.order) {
    if (*plan.horses_left == 0) {
      return;
    }
    const AssignmentJob& job = (*plan.jobs)[job_index];
    if (!TakesThePeoplesCart(job.kind) || job.on_foot_only) {
      continue;
    }
    // ITS FAR WALKERS, the longest walk first: seats beyond the horses go to
    // them, and the rest walk as they were placed.
    std::vector<FarHand> crew;
    std::uint32_t on_job = 0;
    for (std::uint32_t index = 0; index < candidates.size(); ++index) {
      if (result[index] != job_index) {
        continue;
      }
      ++on_job;
      const float walk = OneWayHours(job, job_index, candidates[index], params, false, false);
      if (walk > params.people_cart_min_walk_hours) {
        crew.push_back(FarHand{.candidate_index = index, .walk_hours = walk});
      }
    }
    std::ranges::stable_sort(crew, [](const FarHand& left, const FarHand& right) {
      return left.walk_hours > right.walk_hours;
    });
    const std::uint32_t capacity = *plan.horses_left * per_cart;
    if (crew.size() > capacity) {
      crew.resize(capacity);
    }
    // THE FREE THE RIDE LETS REACH IT, while it is short and seats are left:
    // judged by the ride, as the brigade's hands are. A free hand near it was
    // the queue's to place on foot, and is not carried.
    AssignmentJob riding = job;
    riding.cart_out = true;
    std::vector<FarHand> joined;
    float joined_norm = 0.0F;
    std::uint32_t road_refused = 0;
    const std::vector<RankedPick> free_far =
        crew.size() < capacity
            ? RankCandidates(riding, job_index, candidates, result, params, road_refused)
            : std::vector<RankedPick>{};
    std::size_t next_free = 0;
    const auto seat_one_more = [&](bool beyond_the_cover) {
      for (; next_free < free_far.size(); ++next_free) {
        const RankedPick& pick = free_far[next_free];
        if (crew.size() + joined.size() >= capacity ||
            (!beyond_the_cover &&
             (*plan.covered)[job_index] + joined_norm >= job.work_days_remaining) ||
            (job.max_crew != 0 && on_job + joined.size() >= job.max_crew)) {
          return false;
        }
        const float walk =
            OneWayHours(job, job_index, candidates[pick.candidate_index], params, false, false);
        if (!(walk > params.people_cart_min_walk_hours)) {
          continue;
        }
        joined.push_back(FarHand{.candidate_index = pick.candidate_index, .walk_hours = walk});
        joined_norm += pick.daily_norm;
        ++next_free;
        return true;
      }
      return false;
    };
    while (seat_one_more(false)) {
    }
    // «ОДИН — ВЕРХОМ, ДВОЕ И БОЛЬШЕ — ПОДВОДА» (transport design §1; the
    // human's word of 24 September 2026: «Вдвоем на лошади убираем. Оставляем
    // верхом рысью и подвода»). UNTIL 0.37.208 ONLY THE SECOND HALF WAS BUILT:
    // a crew of one got nothing. The crew is filled only until the day's work
    // is covered, so a far job whose work ONE hand covers got a crew of one
    // and was manned on no morning of any season — the last 0.085 man-day of
    // a felling mark stood 312 days (village 1936, stand 37) with eighteen
    // horses in the pool; of the far stands' felling job-mornings with under
    // half a man-day left 1 386 were offered and 144 manned (nine villages,
    // fourteen years).
    //
    // THE ONE RIDES IF HIS OWN WAY LEAVES HIM A DAY, and otherwise a second
    // is seated and they go as a cart: the hour takes the holder of a horse
    // on foot to the horse yard first (horse_yard_road.h), and a rider sent
    // by the ride from his house stood on a December stand with the horse and
    // felled nothing — 697 stand-days in nine villages on this cure's first
    // pre-check, months 12 to 2. A cart's passenger rides from his house.
    // STUB, named: the unit's «конный выезд» mark of the design is not a
    // thing of the core yet — every far hand may ride.
    if (crew.size() + joined.size() == 1) {
      const FarHand& one = crew.empty() ? joined.front() : crew.front();
      if (!RiderHasADay(job, job_index, candidates[one.candidate_index], params) &&
          !seat_one_more(true)) {
        continue;
      }
    }
    crew.insert(crew.end(), joined.begin(), joined.end());
    if (crew.empty()) {
      continue;
    }
    const auto carts = static_cast<std::uint32_t>((crew.size() + per_cart - 1U) / per_cart);
    *plan.horses_left -= carts;
    (*plan.cart_today)[job_index] = 1U;
    (*plan.covered)[job_index] += joined_norm;
    for (std::size_t seat = 0; seat < crew.size(); ++seat) {
      const std::uint32_t index = crew[seat].candidate_index;
      result[index] = job_index;
      // The first of each cart's load drives it.
      const std::uint32_t driver = crew[(seat / per_cart) * per_cart].candidate_index;
      if (driver == index) {
        if (plan.rides_horse != nullptr) {
          (*plan.rides_horse)[index] = 1U;
        }
      } else if (plan.rides_cart_with != nullptr) {
        (*plan.rides_cart_with)[index] = driver;
      }
    }
  }
}

}  // namespace core
