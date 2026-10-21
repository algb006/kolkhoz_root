// The plan writes the price (trip_pricing.h; #1056 C3 body 4/6).

#include "trip_pricing.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>

#include "core_common/logistics_rules.h"
#include "core_common/logistics_state.h"
#include "core_common/trip_terms.h"
#include "core_common/work_seam.h"

namespace core {
namespace {

/// The model's number (transport.csv `cart_loaded` `trip_model`; production's
/// CartTripModel::kPlanned): the plan writes the price.
constexpr std::uint8_t kPlannedTripModel = 3;

}  // namespace

bool PlanPricesCarts(const LogisticsConfig& config, const WorldState& world) {
  return config.cart_trip_model == kPlannedTripModel && config.standard_day_hours > 0.0F &&
         SettlementHasCarts(world, config.horse_kind);
}

void PriceTasksFromPlan(const LogisticsConfig& config, WorldState& world) {
  if (!PlanPricesCarts(config, world) || world.groom_plan.day != world.calendar.day) {
    return;
  }
  // THE WHOLE TRIPS DRAWN TODAY, by task: every loaded leg of a cart (not a
  // carrier on foot) names the task whose load it takes.
  std::unordered_map<std::uint32_t, std::uint32_t> drawn;
  for (const CartPlan& cart : world.groom_plan.carts) {
    if (cart.on_foot) {
      continue;
    }
    for (const CartLeg& leg : cart.legs) {
      if (leg.loaded != 0 && leg.task.value != kInvalidEntityIdValue) {
        ++drawn[leg.task.value];
      }
    }
  }
  YearLedger& book = world.ledger.current;
  for (std::uint32_t row = 0; row < world.logistics_tasks.rows.size(); ++row) {
    const LogisticsTaskRow& task = world.logistics_tasks.rows[row];
    const TaskTripTerms& terms = task.trip_terms;
    if (!(terms.trip_hours > 0.0F) || terms.cart_load <= 0 || terms.grams_left <= 0) {
      continue;  // not a cart's whole-trip price: production's evening price stands
    }
    const WorkAssignment work = HaulingWorkOf(task);
    float* const remaining = WorkSeamOf(world, work);
    float* const written = WorkSeamWrittenOf(world, work);
    if (remaining == nullptr || written == nullptr) {
      continue;
    }
    const auto found = drawn.find(world.logistics_tasks.row_ids[row].value);
    const std::uint32_t needed = WholeTripsFor(terms.grams_left, terms.cart_load);
    const std::uint32_t trips = found == drawn.end() ? 0U : std::min(found->second, needed);
    const Grams reached =
        std::min<Grams>(terms.grams_left, static_cast<Grams>(trips) * terms.cart_load);
    const float drawn_days =
        static_cast<float>(trips) * terms.trip_hours / config.standard_day_hours;
    const float fallback_days =
        FallbackCartDays(terms, terms.grams_left - reached, config.standard_day_hours);
    // THE ONE DOOR (ruling 1): both halves of the seam, so production's
    // conversion (drained = written - remaining) reads the plan's price.
    *remaining = drawn_days + fallback_days;
    *written = *remaining;
    book.cart_days_drawn += drawn_days;
    book.cart_days_fallback += fallback_days;
  }
}

}  // namespace core
