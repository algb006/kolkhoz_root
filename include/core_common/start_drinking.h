/// @file
/// @brief The drinking village's past: the band a man of 16 and over starts in
///        by his class (econ's alcohol-start-and-sport §2.1; STUB, boss seq 121).
/// @threading SINGLE_THREADED
/// Pure arithmetic over its arguments; called by genesis for the founders and
/// by core_residents for the arrivals (#1262), so neither module owns it.
#ifndef CORE_COMMON_START_DRINKING_H_
#define CORE_COMMON_START_DRINKING_H_

#include <cstdint>

#include "core_common/quantities.h"

namespace core {

/// @brief The class bands, world_params alcohol_start_*: «незанятый» (old or
/// sick) first, then married, then single. ONE HOME for the founders and, since
/// #1262, the arrivals (crime §6 «Приезжие приходят со своим прошлым»).
struct StartDrinking {
  float married_min = 15.0F;
  float married_max = 35.0F;
  float single_min = 25.0F;
  float single_max = 45.0F;
  float idle_min = 35.0F;
  float idle_max = 55.0F;
  float idle_age = 55.0F;     ///< «старик»: this many biological years and over
  float sick_health = 50.0F;  ///< «больной»: health below this
};

/// @brief The counter hash's salt of a starting draw: the founders' (day 0)
///        and the arrivals' (their day) alike.
inline constexpr std::uint64_t kStartDrinkingSalt = 0x414C4300ULL;

/// @brief A man's starting alcoholism: `draw` in [0, 1) placed uniformly in
///        his class's band.
/// @param age_years Biological years. @param married He has a spouse.
constexpr float StartingAlcoholism(
    const StartDrinking& start, float age_years, Metric health, bool married, float draw) {
  float low = start.single_min;
  float high = start.single_max;
  if (age_years >= start.idle_age || health < start.sick_health) {
    low = start.idle_min;
    high = start.idle_max;
  } else if (married) {
    low = start.married_min;
    high = start.married_max;
  }
  return low + (draw * (high - low));
}

}  // namespace core

#endif  // CORE_COMMON_START_DRINKING_H_
