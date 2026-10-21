// The plan writes the price (trip_pricing.h; #1056 C3 bodies 4-5/6).

#include "trip_pricing.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "core_common/haul.h"
#include "core_common/logistics_rules.h"
#include "core_common/logistics_state.h"
#include "core_common/road_route.h"
#include "core_common/state_table_ops.h"
#include "core_common/trip_terms.h"
#include "core_common/work_seam.h"

namespace core {
namespace {

/// The model's number (transport.csv `cart_loaded` `trip_model`; production's
/// CartTripModel::kPlanned): the plan writes the price.
constexpr std::uint8_t kPlannedTripModel = 3;

/// What lies on a field heap task's field: its reaped resource, or none.
ResourceId HeapResourceOf(const WorldState& world, const LogisticsTaskRow& task) {
  const std::uint32_t row = FindRow(world.fields, task.field);
  return row == kNoRow ? ResourceId{} : world.fields.rows[row].reaped_resource;
}

/// The last trip's grams of a load priced at whole trips.
Grams LeftoverOf(const TaskTripTerms& terms) {
  const std::uint32_t trips = WholeTripsFor(terms.grams_left, terms.cart_load);
  return trips == 0 ? 0 : terms.grams_left - (static_cast<Grams>(trips - 1U) * terms.cart_load);
}

bool SamePlace(Vec2 a, Vec2 b) {
  return a.x == b.x && a.y == b.y;
}

/// One pooled trip of today's plan: the first load's task row and the second's.
struct Pooled {
  std::uint32_t first = kNoRow;
  std::uint32_t second = kNoRow;
  Vec2 receiver;
};

}  // namespace

bool PlanPricesCarts(const LogisticsConfig& config, const WorldState& world) {
  return config.cart_trip_model == kPlannedTripModel && config.standard_day_hours > 0.0F &&
         SettlementHasCarts(world, config.horse_kind);
}

bool PooledTripOf(const WorldState& world,
                  const TaskTripTerms& first,
                  Vec2 second_load,
                  Vec2 receiver,
                  PooledTrip& trip) {
  const float direct = RoadKm(world, TravelMode::kCart, first.load_point, receiver);
  if (!(direct > 0.0F)) {
    return false;
  }
  // THE FIRST LOAD's OWN PRICED PACES, per km of its way: the bed and the gear
  // the evening priced it at (ruling 2), the pauses taken out and put back by
  // the trip's own count of loadings.
  const float pause = kLoadingPauseHours;
  const float loaded_per_km = std::max(first.trip_loaded_hours - (2.0F * pause), 0.0F) / direct;
  const float empty_per_km = std::max(first.trip_hours - first.trip_loaded_hours, 0.0F) / direct;
  trip.to_second_hours =
      (RoadKm(world, TravelMode::kCart, first.load_point, second_load) * loaded_per_km) + pause;
  trip.to_receiver_hours =
      (RoadKm(world, TravelMode::kCart, second_load, receiver) * loaded_per_km) + (2.0F * pause);
  trip.back_hours = direct * empty_per_km;
  return true;
}

std::uint32_t PoolPartnerOf(const LogisticsConfig& config,
                            const WorldState& world,
                            std::uint32_t task_row,
                            Grams leftover,
                            Vec2 receiver,
                            const std::vector<float>& remaining) {
  if (!(config.pool_detour_share > 0.0F) || task_row >= world.logistics_tasks.rows.size()) {
    return kNoRow;
  }
  const LogisticsTaskRow& first = world.logistics_tasks.rows[task_row];
  const TaskTripTerms& terms = first.trip_terms;
  if (first.load_kind != LogisticsLoadKind::kFieldHeap || !(terms.trip_hours > 0.0F) ||
      leftover <= 0 || leftover >= terms.cart_load) {
    return kNoRow;
  }
  const float direct = RoadKm(world, TravelMode::kCart, terms.load_point, receiver);
  if (!(direct > 0.0F)) {
    return kNoRow;
  }
  const ResourceId resource = HeapResourceOf(world, first);
  std::uint32_t best = kNoRow;
  float best_detour = 0.0F;
  for (std::uint32_t row = 0; row < world.logistics_tasks.rows.size(); ++row) {
    const LogisticsTaskRow& other = world.logistics_tasks.rows[row];
    const TaskTripTerms& theirs = other.trip_terms;
    // ITS LAST PART-LOAD, of any heap (#1056 C3, (iii) lifted; core-boss-oats-
    // b-pair [110]): until then only a heap of one trip could be a partner.
    if (row == task_row || other.load_kind != LogisticsLoadKind::kFieldHeap || other.paused ||
        !(theirs.trip_hours > 0.0F) || theirs.cart_load != terms.cart_load ||
        theirs.grams_left <= 0 || leftover + LeftoverOf(theirs) > terms.cart_load ||
        row >= remaining.size() || !(remaining[row] > 0.0F) ||
        HeapResourceOf(world, other).value != resource.value) {
      continue;
    }
    const float via = RoadKm(world, TravelMode::kCart, terms.load_point, theirs.load_point) +
                      RoadKm(world, TravelMode::kCart, theirs.load_point, receiver);
    if (via > (1.0F + config.pool_detour_share) * direct) {
      continue;
    }
    const float detour = via - direct;
    if (best == kNoRow || detour < best_detour) {
      best = row;
      best_detour = detour;
    }
  }
  return best;
}

void PriceTasksFromPlan(const LogisticsConfig& config, WorldState& world, PriceMode mode) {
  if (!PlanPricesCarts(config, world) || world.groom_plan.day != world.calendar.day) {
    return;
  }
  const bool write = mode == PriceMode::kWrite;
  // The carried pairs are today's alone: each pricing before the carting writes them anew.
  if (write) {
    for (LogisticsTaskRow& task : world.logistics_tasks.rows) {
      task.carried_by = LogisticsTaskId{};
      task.carried_share = 0.0F;
    }
  }
  // THE WHOLE TRIPS DRAWN TODAY, by task: every loaded leg of a cart (not a
  // carrier on foot) names the task whose load it takes; a POOLED trip is two
  // loaded legs - to the second heap, then on to the receiver - and counts once,
  // on its second leg.
  std::unordered_map<std::uint32_t, std::uint32_t> drawn;
  std::vector<Pooled> pooled;
  for (const CartPlan& cart : world.groom_plan.carts) {
    if (cart.on_foot) {
      continue;
    }
    for (const CartLeg& leg : cart.legs) {
      if (leg.loaded == 0 || leg.task.value == kInvalidEntityIdValue) {
        continue;
      }
      if (leg.pooled_task.value != kInvalidEntityIdValue) {
        const std::uint32_t second = FindRow(world.logistics_tasks, leg.pooled_task);
        if (second == kNoRow ||
            SamePlace(leg.to, world.logistics_tasks.rows[second].trip_terms.load_point)) {
          continue;  // the leg to the second heap: the trip is counted on the next
        }
        pooled.push_back(Pooled{.first = FindRow(world.logistics_tasks, leg.task),
                                .second = second,
                                .receiver = leg.to});
      }
      ++drawn[leg.task.value];
    }
  }
  YearLedger& book = world.ledger.current;
  // THE POOLED TRIPS' HOURS, split by grams (ruling 3: each seam pays by grams).
  std::unordered_map<std::uint32_t, float> first_pooled_hours;    // by the first's row
  std::unordered_map<std::uint32_t, float> second_pooled_hours;   // by the second's row
  std::unordered_map<std::uint32_t, std::uint32_t> pooled_trips;  // by the first's row
  for (const Pooled& trip : pooled) {
    if (trip.first == kNoRow) {
      continue;
    }
    const TaskTripTerms& first = world.logistics_tasks.rows[trip.first].trip_terms;
    const TaskTripTerms& second = world.logistics_tasks.rows[trip.second].trip_terms;
    PooledTrip hours;
    if (!PooledTripOf(world, first, second.load_point, trip.receiver, hours)) {
      continue;
    }
    const float whole = hours.to_second_hours + hours.to_receiver_hours + hours.back_hours;
    const auto grams_first = static_cast<float>(LeftoverOf(first));
    const auto grams_second = static_cast<float>(LeftoverOf(second));  // its last part-load
    const float share =
        grams_first + grams_second > 0.0F ? grams_first / (grams_first + grams_second) : 1.0F;
    first_pooled_hours[trip.first] += whole * share;
    second_pooled_hours[trip.second] += whole * (1.0F - share);
    ++pooled_trips[trip.first];
    if (!write) {
      book.cart_trips_pooled += 1.0F;
    }
  }
  std::unordered_map<std::uint32_t, float> seam_of;  // by row: the seam written
  for (std::uint32_t row = 0; row < world.logistics_tasks.rows.size(); ++row) {
    const LogisticsTaskRow& task = world.logistics_tasks.rows[row];
    const TaskTripTerms& terms = task.trip_terms;
    if (!(terms.trip_hours > 0.0F) || terms.cart_load <= 0 || terms.grams_left <= 0) {
      continue;  // not a cart's whole-trip price
    }
    const WorkAssignment work = HaulingWorkOf(task);
    float* const remaining = WorkSeamOf(world, work);
    float* const written = WorkSeamWrittenOf(world, work);
    if (remaining == nullptr || written == nullptr) {
      continue;
    }
    // A POOLED SECOND's OWN TRIPS ((iii) lifted): its last part-load rides in
    // the first's pooled trip (below); the rest of it, its whole loads, is
    // priced here as any heap's. A heap of one trip has none.
    const Grams own_grams =
        second_pooled_hours.contains(row) ? terms.grams_left - LeftoverOf(terms) : terms.grams_left;
    const auto found = drawn.find(world.logistics_tasks.row_ids[row].value);
    const std::uint32_t needed = WholeTripsFor(own_grams, terms.cart_load);
    const std::uint32_t trips = found == drawn.end() ? 0U : std::min(found->second, needed);
    const Grams reached = std::min<Grams>(own_grams, static_cast<Grams>(trips) * terms.cart_load);
    const auto pool = pooled_trips.find(row);
    const std::uint32_t in_pool = pool == pooled_trips.end() ? 0U : std::min(pool->second, trips);
    const float drawn_hours = (static_cast<float>(trips - in_pool) * terms.trip_hours) +
                              (in_pool > 0U ? first_pooled_hours[row] : 0.0F);
    const float drawn_days = drawn_hours / config.standard_day_hours;
    const float fallback_days =
        FallbackCartDays(terms, own_grams - reached, config.standard_day_hours);
    // THE ONE DOOR (ruling 1): both halves of the seam, so production's
    // conversion (drained = written - remaining) reads the plan's price.
    seam_of[row] = drawn_days + fallback_days;
    if (write) {
      *remaining = seam_of[row];
      *written = *remaining;
    } else {
      book.cart_days_drawn += drawn_days;
      book.cart_days_fallback += fallback_days;
    }
  }
  // THE CARRIED LOADS (ruling 3): a pooled second field pays its share of the
  // trip by grams, and drains with its carrier's seam (work_seam.h,
  // CoDrainCarriedTasks) at the ratio of the two seams, so both run out
  // together.
  for (const Pooled& trip : pooled) {
    if (trip.first == kNoRow || !second_pooled_hours.contains(trip.second)) {
      continue;
    }
    LogisticsTaskRow& second = world.logistics_tasks.rows[trip.second];
    const WorkAssignment work = HaulingWorkOf(second);
    float* const remaining = WorkSeamOf(world, work);
    float* const written = WorkSeamWrittenOf(world, work);
    const auto carrier = seam_of.find(trip.first);
    if (remaining == nullptr || written == nullptr || carrier == seam_of.end() ||
        !(carrier->second > 0.0F)) {
      continue;
    }
    const float days = second_pooled_hours[trip.second] / config.standard_day_hours;
    const auto own = seam_of.find(trip.second);
    const float own_days = own == seam_of.end() ? 0.0F : own->second;
    if (write) {
      *remaining = own_days + days;
      *written = *remaining;
      second.carried_by = world.logistics_tasks.row_ids[trip.first];
      second.carried_share = days / carrier->second;
    } else {
      book.cart_days_drawn += days;
    }
    second_pooled_hours.erase(trip.second);
  }
}

}  // namespace core
