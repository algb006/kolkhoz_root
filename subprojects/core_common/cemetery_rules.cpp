/// @file
/// @brief The cemetery's capacity, fill and the burial stop (cemetery_rules.h).

#include "core_common/cemetery_rules.h"

#include <cmath>
#include <numbers>

namespace core {

std::uint16_t CemeteryCapacityGraves(float plot_radius_m, float m2_per_grave) {
  if (plot_radius_m <= 0.0F || m2_per_grave <= 0.0F) {
    return 0;
  }
  const double area_m2 =
      std::numbers::pi * static_cast<double>(plot_radius_m) * static_cast<double>(plot_radius_m);
  const double graves = std::floor(area_m2 / static_cast<double>(m2_per_grave));
  return graves >= 65535.0 ? std::uint16_t{65535} : static_cast<std::uint16_t>(graves);
}

float CemeteryFill(std::uint16_t graves, std::uint16_t capacity) {
  if (capacity == 0) {
    return 1.0F;
  }
  return static_cast<float>(graves) / static_cast<float>(capacity);
}

bool BurialHeld(const WorldState& world) {
  return world.burials_waiting > 0;
}

}  // namespace core
