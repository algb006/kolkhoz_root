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
  // 0..1, the metric's own scale (social.md «Заполнение — метрика юнита,
  // 0–100 %»): past its area a cemetery's rows grow denser in the same
  // fill_100 frame (#1367), and the number stops at full.
  const float fill = static_cast<float>(graves) / static_cast<float>(capacity);
  return fill > 1.0F ? 1.0F : fill;
}

bool BurialHeld(const WorldState& world) {
  // NO STOP SINCE #1367 (social.md «Остановки игры нет», the human's word of
  // 1 November 2026, #1564): the capacity is unlimited, so no death waits for
  // room. The door stays for the layer's clock and answers no; a world with no
  // cemetery at all counts its waiting graves (burials_waiting) without
  // stopping.
  (void)world;
  return false;
}

}  // namespace core
