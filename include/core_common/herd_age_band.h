/// @file
/// @brief The bands of ages a herd's adults hold — one, or two when a group
///        stands apart — kept by every flow that adds, ages or takes adults.
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
/// WHY TWO (save 103; boss-core-epoch1-resume [80]-[81]). A band is kept
/// UNIFORM after a cut: taking the oldest `k` of `n` cuts the top `k/n` of it.
/// That is exact for a herd drawn of even ages and wrong for a lopsided one:
/// ten young horses bought into a row whose own horse was old made the band
/// [1, 7] of eleven, and when the old one died its top fell by only 6/11 — the
/// band still read "old", kHerdAging stayed lit, and the age death went on
/// killing old horses that did not exist. A group a game year or more away
/// from the band (kAdultAgeBandSplitYears) is kept as a second band, and a
/// cut takes the older band first. Two is the most a row holds: a third group
/// folds the two nearest together.
///
/// With no adults the bands mean nothing; the next head that comes in sets
/// them anew. The flows that take adults other than by age — hunger, the
/// maturation cull — do not narrow them (named; a row they empty is reset
/// by the next arrival, and a row they leave is read by its bands as before).

#ifndef CORE_COMMON_HERD_AGE_BAND_H_
#define CORE_COMMON_HERD_AGE_BAND_H_

#include <array>
#include <cstdint>

namespace core {

struct HerdRow;

/// @brief How far apart, in game years, two groups of adults must stand to
///        be kept as two bands rather than one. A representation's
///        threshold, not a design number: a year is half the horse's
///        lifespan band (6 to 8 years), so a band wider than that by a split
///        would misread the death hazard by a quarter of it.
inline constexpr float kAdultAgeBandSplitYears = 1.0F;

/// @brief One band of adult ages: `count` heads, taken as even between `low`
///        and `high`, game years.
struct AdultAgeBand {
  std::uint16_t count = 0;
  float low = 0.0F;
  float high = 0.0F;
};

/// @brief A row's bands, the younger first: none with no adults, one or two.
struct AdultAgeBands {
  std::array<AdultAgeBand, 2> band{};
  std::uint32_t size = 0;
};

/// @brief The bands of `herd` holding `adults` heads (its adult count, or
///        the count before a flow the caller is in the middle of).
AdultAgeBands AdultAgeBandsOf(const HerdRow& herd, std::uint16_t adults);

/// @brief Adds `added` adults aged `youngest`..`oldest` (game years) grown
///        up in the row or drawn with it — the maturation, the start's draw,
///        a family's gift: they widen the nearest band, whatever the distance.
///        A herd's own yearly cohorts are what a uniform band describes; cut
///        into two at every calving they moved every herd's age deaths (the
///        first cut of 0.36.36: the canon moved on all nine seeds). The count
///        and the age total are the caller's.
void WidenAdultAgeBand(
    HerdRow& herd, std::uint16_t adults_before, std::uint16_t added, float youngest, float oldest);

/// @brief Adds `added` adults aged `youngest`..`oldest` that come FROM
///        OUTSIDE — the district's arrival: within kAdultAgeBandSplitYears of
///        a band they widen it; farther, they stand as a band of their own,
///        folding the two nearest when that would make three (boss-core-
///        epoch1-resume [81]: «две полосы на строку — до и после покупки»).
void AddAdultAgeGroup(
    HerdRow& herd, std::uint16_t adults_before, std::uint16_t added, float youngest, float oldest);

/// @brief Folds `from`'s bands into `into`'s, as when two herds of one kind
///        are gathered into one. `into_adults_before` is `into`'s adult count
///        before the gathering; a `from` with no adults changes nothing. Two
///        rows of one band each gather into one band, as before 0.36.36; a
///        row that holds two keeps them apart where they stand a year or more
///        apart.
void MergeAdultAgeBand(HerdRow& into, std::uint16_t into_adults_before, const HerdRow& from);

/// @brief Ages every band by `years`, as the herd day ages every adult.
void AgeAdultAgeBand(HerdRow& herd, float years);

/// @brief Takes the oldest `gone` of `adults_before` out of the bands — the
///        older band first, the top of each as a uniform band — and returns
///        their mean age. With none left the bands are emptied.
/// @return The mean age of the heads taken, game years; 0 when none go.
float CutOldestFromAdultAgeBand(HerdRow& herd, std::uint16_t adults_before, std::uint16_t gone);

}  // namespace core

#endif  // CORE_COMMON_HERD_AGE_BAND_H_
