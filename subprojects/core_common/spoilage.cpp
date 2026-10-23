// The spoilage rule of spoilage.h.

#include "core_common/spoilage.h"

#include <cmath>
#include <cstddef>
#include <limits>

#include "core_common/calendar.h"

namespace core {

Grams RotMarginGrams(Grams held, float spoil_days, std::uint32_t days) {
  if (held <= 0 || !(spoil_days > 1.0F)) {
    return 0;
  }
  // The arithmetic the seed fund's margin had in family_exchange.cpp, moved
  // here unchanged: double for the power, float for the grams.
  const double kept_share =
      std::pow(1.0 - (1.0 / static_cast<double>(spoil_days)), static_cast<double>(days));
  return GramsFromFloat(
      static_cast<float>(static_cast<double>(held) / kept_share - static_cast<double>(held)));
}

float FieldHeapShelfDays(float spoil_days, float keeping_factor) {
  return spoil_days > 0.0F ? spoil_days * keeping_factor : std::numeric_limits<float>::infinity();
}

std::uint32_t HayWeatheringSeason(std::uint32_t month) {
  if (month == 11U || month < 2U) {
    return 2U;  // winter
  }
  if (month < 5U) {
    return 3U;  // spring
  }
  return month < 8U ? 0U : 1U;  // summer, autumn
}

float HayHeapShelfDays(float season_share) {
  const float share_a_day = season_share / static_cast<float>(kDaysPerMonth);
  return share_a_day > 0.0F ? 1.0F / share_a_day : std::numeric_limits<float>::infinity();
}

Grams SpoiledToday(Grams held, float spoil_days, float keeping_factor) {
  if (held <= 0) {
    return 0;
  }
  const float days = spoil_days * keeping_factor;
  // Positive test, so a nan shelf life keeps rather than rots: a broken cell
  // must not empty the village's stores.
  if (!(days > 0.0F)) {
    return 0;
  }
  if (days <= 1.0F) {
    return held;  // a shelf life of a day or less: nothing is left by morning
  }
  const Grams lost = GramsFromFloat(static_cast<float>(held) / days);
  return lost > held ? held : lost;
}

void SpoilAmounts(ResourceAmounts& amounts,
                  const std::vector<float>& spoil_days,
                  float keeping_factor,
                  ResourceAmounts& lost) {
  for (std::size_t index = 0; index < amounts.size(); ++index) {
    if (index >= spoil_days.size() || amounts[index] <= 0) {
      continue;
    }
    const Grams gone = SpoiledToday(amounts[index], spoil_days[index], keeping_factor);
    if (gone <= 0) {
      continue;
    }
    amounts[index] -= gone;
    if (lost.size() <= index) {
      lost.resize(index + 1U, 0);
    }
    lost[index] += gone;
  }
}

}  // namespace core
