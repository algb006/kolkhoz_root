// The whole-trip arithmetic of trip_terms.h (#1056 C3).

#include "core_common/trip_terms.h"

namespace core {

std::uint32_t WholeTripsFor(Grams grams, Grams cart_load) {
  if (grams <= 0 || cart_load <= 0) {
    return 0;
  }
  // A cart leaves with what lies, up to its load, and comes back: the ceiling.
  return static_cast<std::uint32_t>((grams + cart_load - 1) / cart_load);
}

float FallbackCartDays(const TaskTripTerms& terms, Grams grams, float standard_day_hours) {
  if (!(terms.trip_hours > 0.0F) || !(standard_day_hours > 0.0F)) {
    return 0.0F;
  }
  const std::uint32_t trips = WholeTripsFor(grams, terms.cart_load);
  return static_cast<float>(trips) * terms.trip_hours / standard_day_hours;
}

}  // namespace core
