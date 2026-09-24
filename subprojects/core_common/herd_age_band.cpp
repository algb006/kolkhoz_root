#include "core_common/herd_age_band.h"

#include <algorithm>

#include "core_common/herd_state.h"

namespace core {

void WidenAdultAgeBand(HerdRow& herd, std::uint16_t adults_before, float youngest, float oldest) {
  if (oldest < youngest) {
    std::swap(youngest, oldest);
  }
  if (adults_before == 0) {
    herd.adult_age_min_game_years = youngest;
    herd.adult_age_max_game_years = oldest;
    return;
  }
  herd.adult_age_min_game_years = std::min(herd.adult_age_min_game_years, youngest);
  herd.adult_age_max_game_years = std::max(herd.adult_age_max_game_years, oldest);
}

void MergeAdultAgeBand(HerdRow& into, std::uint16_t into_adults_before, const HerdRow& from) {
  if (from.adult_count == 0) {
    return;
  }
  WidenAdultAgeBand(
      into, into_adults_before, from.adult_age_min_game_years, from.adult_age_max_game_years);
}

void AgeAdultAgeBand(HerdRow& herd, float years) {
  herd.adult_age_min_game_years += years;
  herd.adult_age_max_game_years += years;
}

float CutOldestFromAdultAgeBand(HerdRow& herd, std::uint16_t adults_before, std::uint16_t gone) {
  if (gone == 0 || adults_before == 0) {
    return 0.0F;
  }
  const float low = herd.adult_age_min_game_years;
  const float high = herd.adult_age_max_game_years;
  const float width = high > low ? high - low : 0.0F;
  const float share =
      gone >= adults_before ? 1.0F : static_cast<float>(gone) / static_cast<float>(adults_before);
  const float taken_mean = high - (share * width * 0.5F);
  if (gone >= adults_before) {
    herd.adult_age_min_game_years = 0.0F;
    herd.adult_age_max_game_years = 0.0F;
  } else {
    herd.adult_age_max_game_years = high - (share * width);
  }
  return taken_mean;
}

}  // namespace core
