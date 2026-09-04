// The spoilage rule of spoilage.h.

#include "core_common/spoilage.h"

#include <cstddef>

namespace core {

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
