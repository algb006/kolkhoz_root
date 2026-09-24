/// @file
/// @brief The band of ages a herd's adults hold — its youngest and its oldest
///        head — kept by every flow that adds, ages or takes adults.
/// @threading SINGLE_THREADED
/// Pure functions of the one HerdRow they are given; they touch no other row
/// and no shared state, so a caller that owns the row may call them from
/// whatever phase owns it.
///
/// WHY A BAND AND NOT AN IMAGINED SPREAD (boss, core-boss-epoch1-6 [3] line 1;
/// econ-boss-canon-0358 [8]; 0.35.16). The age death read the herd's MEAN age
/// and an ASSUMED spread of about ±3.3 years around it (AdultAgeHalfWidth,
/// retired). The start's team is drawn between one and four years old
/// (0.35.10), its mean 3.5, and the assumed spread reached past the horse's
/// old age of six — the hazard killed old horses that did not exist: in year
/// 2 one or two heads died of age on five seeds of nine. The band is the ages
/// the herd actually holds, so no head dies of age before the oldest has
/// reached the lifespan band.
///
/// The band is exact for a herd built of known ages and is kept as a UNIFORM
/// band after a cut: taking the oldest `k` of `n` cuts the top `k/n` of it.
/// With no adults it means nothing; the next head that comes in sets it anew.

#ifndef CORE_COMMON_HERD_AGE_BAND_H_
#define CORE_COMMON_HERD_AGE_BAND_H_

#include <cstdint>

namespace core {

struct HerdRow;

/// @brief Widens the band to hold new adults aged `youngest`..`oldest` (game
///        years). With no adults before (`adults_before` == 0) the band IS
///        those ages. Call it wherever adults are added; the count and the age
///        total are the caller's.
void WidenAdultAgeBand(HerdRow& herd, std::uint16_t adults_before, float youngest, float oldest);

/// @brief Folds `from`'s band into `into`'s, as when two herds of one kind are
///        gathered into one. `into_adults_before` is `into`'s adult count
///        before the gathering; a `from` with no adults changes nothing.
void MergeAdultAgeBand(HerdRow& into, std::uint16_t into_adults_before, const HerdRow& from);

/// @brief Ages the band by `years`, as the herd day ages every adult.
void AgeAdultAgeBand(HerdRow& herd, float years);

/// @brief Takes the oldest `gone` of `adults_before` out of the band, and
///        returns their mean age: the top `gone/adults_before` of a uniform
///        band. With none left the band is emptied.
/// @return The mean age of the heads taken, game years; 0 when none go.
float CutOldestFromAdultAgeBand(HerdRow& herd, std::uint16_t adults_before, std::uint16_t gone);

}  // namespace core

#endif  // CORE_COMMON_HERD_AGE_BAND_H_
