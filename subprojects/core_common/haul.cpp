// The haul arithmetic of haul.h.

#include "core_common/haul.h"

#include <cmath>

namespace core {
namespace {

/// The shortest trip the model will price. A load still has to be picked up
/// and put down, and two places at the same coordinates would otherwise
/// divide a day by zero and move the whole world in an instant — which is
/// the very stub task A4 exists to remove.
constexpr float kMinRoundTripHours = 0.25F;

}  // namespace

HaulRate RateBetween(Vec2 from, Vec2 to, float hours_per_km, Grams load) {
  const float dx = to.x - from.x;
  const float dy = to.y - from.y;
  const float metres = std::sqrt((dx * dx) + (dy * dy));
  const float one_way = metres / 1000.0F * hours_per_km;
  HaulRate rate;
  rate.load = load;
  rate.round_trip_hours = 2.0F * one_way;
  if (!(rate.round_trip_hours >= kMinRoundTripHours)) {
    rate.round_trip_hours = kMinRoundTripHours;  // positive test: nan lands here too
  }
  return rate;
}

Grams HaulGrams(float hours, const HaulRate& rate) {
  if (!(hours > 0.0F) || rate.load <= 0 || !(rate.round_trip_hours > 0.0F)) {
    return 0;
  }
  const float trips = hours / rate.round_trip_hours;
  return GramsFromFloat(static_cast<float>(rate.load) * trips);
}

float HaulDaysFor(Grams waiting, const HaulRate& rate, float standard_day_hours) {
  if (waiting <= 0 || rate.load <= 0 || !(rate.round_trip_hours > 0.0F) ||
      !(standard_day_hours > 0.0F)) {
    return 0.0F;
  }
  const float per_day = static_cast<float>(rate.load) * standard_day_hours / rate.round_trip_hours;
  if (!(per_day > 0.0F)) {
    return 0.0F;
  }
  return static_cast<float>(waiting) / per_day;
}

}  // namespace core
