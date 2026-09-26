#include "core_common/herd_age_band.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

#include "core_common/herd_state.h"

namespace core {
namespace {

/// Up to four bands while two rows' bands are folded into one row's.
struct BandSet {
  std::array<AdultAgeBand, 4> band{};
  std::uint32_t size = 0;
};

void Push(BandSet& set, const AdultAgeBand& band) {
  if (band.count > 0 && set.size < set.band.size()) {
    set.band[set.size] = band;
    ++set.size;
  }
}

/// The distance between two bands, nought when they touch or overlap.
float Gap(const AdultAgeBand& a, const AdultAgeBand& b) {
  const float apart = std::max(a.low - b.high, b.low - a.high);
  return apart > 0.0F ? apart : 0.0F;
}

AdultAgeBand Joined(const AdultAgeBand& a, const AdultAgeBand& b) {
  return AdultAgeBand{.count = static_cast<std::uint16_t>(a.count + b.count),
                      .low = std::min(a.low, b.low),
                      .high = std::max(a.high, b.high)};
}

void SortByAge(BandSet& set) {
  std::stable_sort(set.band.begin(),
                   set.band.begin() + set.size,
                   [](const AdultAgeBand& a, const AdultAgeBand& b) {
                     return a.low < b.low || (a.low == b.low && a.high < b.high);
                   });
}

/// Folds the nearest two bands together until at most two remain and those
/// stand kAdultAgeBandSplitYears or more apart. Deterministic: the bands are
/// sorted by age, and of equally near pairs the first in that order folds.
void Fold(BandSet& set) {
  SortByAge(set);
  while (set.size > 1) {
    std::uint32_t first = 0;
    std::uint32_t second = 1;
    float nearest = Gap(set.band[0], set.band[1]);
    for (std::uint32_t i = 0; i < set.size; ++i) {
      for (std::uint32_t j = i + 1; j < set.size; ++j) {
        const float gap = Gap(set.band[i], set.band[j]);
        if (gap < nearest) {
          nearest = gap;
          first = i;
          second = j;
        }
      }
    }
    if (set.size <= 2 && nearest >= kAdultAgeBandSplitYears) {
      return;
    }
    set.band[first] = Joined(set.band[first], set.band[second]);
    for (std::uint32_t k = second + 1; k < set.size; ++k) {
      set.band[k - 1] = set.band[k];
    }
    --set.size;
    SortByAge(set);
  }
}

BandSet Read(const HerdRow& herd, std::uint16_t adults) {
  BandSet set;
  if (adults == 0) {
    return set;
  }
  if (herd.adult_older_count == 0 || herd.adult_older_count >= adults) {
    Push(set,
         {.count = adults,
          .low = herd.adult_age_min_game_years,
          .high = herd.adult_age_max_game_years});
    return set;
  }
  Push(set,
       {.count = static_cast<std::uint16_t>(adults - herd.adult_older_count),
        .low = herd.adult_age_min_game_years,
        .high = herd.adult_younger_to_game_years});
  Push(set,
       {.count = herd.adult_older_count,
        .low = herd.adult_older_from_game_years,
        .high = herd.adult_age_max_game_years});
  return set;
}

/// Writes at most two bands, the younger first; a third is the caller's to
/// have folded.
void Write(HerdRow& herd, const BandSet& set) {
  herd.adult_older_count = 0;
  herd.adult_older_from_game_years = 0.0F;
  herd.adult_younger_to_game_years = 0.0F;
  if (set.size == 0) {
    herd.adult_age_min_game_years = 0.0F;
    herd.adult_age_max_game_years = 0.0F;
    return;
  }
  herd.adult_age_min_game_years = set.band[0].low;
  if (set.size == 1) {
    herd.adult_age_max_game_years = set.band[0].high;
    return;
  }
  herd.adult_younger_to_game_years = set.band[0].high;
  herd.adult_older_from_game_years = set.band[1].low;
  herd.adult_age_max_game_years = set.band[1].high;
  herd.adult_older_count = set.band[1].count;
}

}  // namespace

AdultAgeBands AdultAgeBandsOf(const HerdRow& herd, std::uint16_t adults) {
  const BandSet set = Read(herd, adults);
  AdultAgeBands bands;
  for (std::uint32_t i = 0; i < set.size && i < bands.band.size(); ++i) {
    bands.band[i] = set.band[i];
    bands.size = i + 1;
  }
  return bands;
}

namespace {

/// The two ways a group joins a row: `apart_if_far` keeps a group a split or
/// more from every band as a band of its own; without it the group widens
/// the nearest band at any distance.
void AddGroup(HerdRow& herd,
              std::uint16_t adults_before,
              std::uint16_t added,
              float youngest,
              float oldest,
              bool apart_if_far) {
  if (added == 0) {
    return;
  }
  if (oldest < youngest) {
    std::swap(youngest, oldest);
  }
  BandSet set = Read(herd, adults_before);
  const AdultAgeBand group{.count = added, .low = youngest, .high = oldest};
  std::uint32_t nearest = set.size;
  float nearest_gap = 0.0F;
  for (std::uint32_t i = 0; i < set.size; ++i) {
    const float gap = Gap(set.band[i], group);
    if (nearest == set.size || gap < nearest_gap) {
      nearest_gap = gap;
      nearest = i;
    }
  }
  if (nearest < set.size && (!apart_if_far || nearest_gap < kAdultAgeBandSplitYears)) {
    set.band[nearest] = Joined(set.band[nearest], group);
  } else {
    Push(set, group);
  }
  Fold(set);
  Write(herd, set);
}

}  // namespace

void WidenAdultAgeBand(
    HerdRow& herd, std::uint16_t adults_before, std::uint16_t added, float youngest, float oldest) {
  AddGroup(herd, adults_before, added, youngest, oldest, false);
}

void AddAdultAgeGroup(
    HerdRow& herd, std::uint16_t adults_before, std::uint16_t added, float youngest, float oldest) {
  AddGroup(herd, adults_before, added, youngest, oldest, true);
}

void MergeAdultAgeBand(HerdRow& into, std::uint16_t into_adults_before, const HerdRow& from) {
  if (from.adult_count == 0) {
    return;
  }
  BandSet set = Read(into, into_adults_before);
  const BandSet theirs = Read(from, from.adult_count);
  // Two rows of one band each gather into one band, as they did before the
  // band came in two: only a split a purchase made is kept apart.
  if (set.size <= 1 && theirs.size <= 1) {
    AdultAgeBand whole = theirs.band[0];
    if (set.size == 1) {
      whole = Joined(set.band[0], whole);
    }
    BandSet one;
    Push(one, whole);
    Write(into, one);
    return;
  }
  for (std::uint32_t i = 0; i < theirs.size; ++i) {
    Push(set, theirs.band[i]);
  }
  Fold(set);
  Write(into, set);
}

void AgeAdultAgeBand(HerdRow& herd, float years) {
  herd.adult_age_min_game_years += years;
  herd.adult_age_max_game_years += years;
  if (herd.adult_older_count > 0) {
    herd.adult_older_from_game_years += years;
    herd.adult_younger_to_game_years += years;
  }
}

float CutOldestFromAdultAgeBand(HerdRow& herd, std::uint16_t adults_before, std::uint16_t gone) {
  if (gone == 0 || adults_before == 0) {
    return 0.0F;
  }
  BandSet set = Read(herd, adults_before);
  std::uint16_t left = gone < adults_before ? gone : adults_before;
  double taken_years = 0.0;
  std::uint32_t taken = 0;
  // The older band first; within a band the top `left/count` of it, as a
  // uniform band.
  for (std::uint32_t i = set.size; i-- > 0 && left > 0;) {
    AdultAgeBand& band = set.band[i];
    const float width = band.high > band.low ? band.high - band.low : 0.0F;
    if (left >= band.count) {
      taken_years +=
          static_cast<double>(band.count) * static_cast<double>(band.low + band.high) * 0.5;
      taken += band.count;
      left = static_cast<std::uint16_t>(left - band.count);
      band.count = 0;
      continue;
    }
    const float share = static_cast<float>(left) / static_cast<float>(band.count);
    taken_years +=
        static_cast<double>(left) * static_cast<double>(band.high - (share * width * 0.5F));
    band.high -= share * width;
    band.count = static_cast<std::uint16_t>(band.count - left);
    taken += left;
    left = 0;
  }
  BandSet kept;
  for (std::uint32_t i = 0; i < set.size; ++i) {
    Push(kept, set.band[i]);
  }
  Fold(kept);
  Write(herd, kept);
  return taken > 0 ? static_cast<float>(taken_years / static_cast<double>(taken)) : 0.0F;
}

}  // namespace core
