// The trip's facts, written by each pricer (trip_terms_writer.h; #1056 C3).

#include "trip_terms_writer.h"

#include <cstdint>

#include "cart_trips.h"
#include "core_common/logistics_rules.h"

namespace core {

TaskTripTerms TripTermsOf(const ProductionConfig& config,
                          Vec2 load_point,
                          const HaulRate& rate,
                          Grams grams_left) {
  TaskTripTerms terms;
  if (config.cart_trip_model != static_cast<std::uint8_t>(CartTripModel::kPlanned) ||
      !rate.produce_cart || rate.load <= 0 || grams_left <= 0 || !(rate.round_trip_hours > 0.0F)) {
    return terms;
  }
  terms.load_point = load_point;
  terms.trip_hours = rate.round_trip_hours;
  terms.trip_loaded_hours = rate.loaded_hours;
  terms.cart_load = rate.load;
  terms.grams_left = grams_left;
  return terms;
}

void WriteTripTermsFor(const ProductionConfig& config,
                       WorldState& world,
                       LogisticsLoadKind kind,
                       const WorkAssignment& hauled,
                       Vec2 load_point,
                       const HaulRate& rate,
                       Grams grams_left) {
  for (LogisticsTaskRow& task : world.logistics_tasks.rows) {
    if (task.load_kind == kind && WorkServesTask(hauled, task)) {
      task.trip_terms = TripTermsOf(config, load_point, rate, grams_left);
      return;
    }
  }
}

}  // namespace core
