/// @file
/// @brief The yard's hay year, mowing to mowing (#859, #1216): how much of a
/// head's year of draw from the yard's stack falls on a run of days, and the
/// share of that year the young of a kind eat before the next mowing.
/// @threading PARALLEL_READONLY
/// Pure functions of their arguments - no state, static or global - so they
/// are safe from any phase and any thread.
///
/// TWO CALLERS, ONE ANSWER TO «HOW MUCH OF THE YEAR IS LEFT»: the mowing
/// (core_residents MowHay) mows for the young the yard's females will bring
/// before the next mowing, and the gift (core_production CarryHayShare) sends
/// a head away with only its own need to that mowing. A copy in each module
/// would let the yard mow for one year and give away by another.
///
/// The draw of one day: 1 in a stall month, 1 - the kind's
/// pasture_coverage_summer in a pasture month (the herd day's own split,
/// farming.csv pasture_from_month..pasture_to_month). The mowing is the last
/// day of the mowing month (food.csv hay_harvest_month), and that day is
/// still eaten from the old stack: the mowing lands at its evening.

#ifndef CORE_COMMON_YARD_HAY_YEAR_H_
#define CORE_COMMON_YARD_HAY_YEAR_H_

#include <cstdint>

#include "core_common/calendar.h"

namespace core {

/// @brief The months that shape the yard's hay year, 0-based.
struct YardHayYear {
  std::uint8_t mow_month = 7;           ///< August: the mowing lands on its last day.
  std::uint8_t pasture_from_month = 4;  ///< May.
  std::uint8_t pasture_to_month = 8;    ///< Inclusive: September.
};

/// @brief The day of the year (0..kDaysPerYear-1) the mowing lands on.
inline std::uint32_t MowingDayOfYear(const YardHayYear& year) {
  return (static_cast<std::uint32_t>(year.mow_month) * kDaysPerMonth) + kDaysPerMonth - 1U;
}

/// @brief One adult head's draw from the stack on a day of the year, in
///        stall-days: 1 in a stall month, 1 - coverage on the pasture.
/// @param coverage The kind's pasture_coverage_summer, 0..1.
inline float StackDayDraw(const YardHayYear& year, float coverage, std::uint32_t day_of_year) {
  const std::uint32_t month = (day_of_year % kDaysPerYear) / kDaysPerMonth;
  const bool pasture = month >= year.pasture_from_month && month <= year.pasture_to_month;
  return pasture ? 1.0F - coverage : 1.0F;
}

/// @brief An adult head's whole year of draw from the stack, in stall-days.
inline float StackDaysOfYear(const YardHayYear& year, float coverage) {
  float days = 0.0F;
  for (std::uint32_t day = 0; day < kDaysPerYear; ++day) {
    days += StackDayDraw(year, coverage, day);
  }
  return days;
}

/// @brief How many days on from `day_of_year` the next mowing lands: 0 on
///        the mowing day itself.
inline std::uint32_t DaysToMowing(const YardHayYear& year, std::uint32_t day_of_year) {
  const std::uint32_t today = day_of_year % kDaysPerYear;
  const std::uint32_t mowing = MowingDayOfYear(year);
  return mowing >= today ? mowing - today : mowing + kDaysPerYear - today;
}

/// @brief An adult head's draw from `day_of_year` to the next mowing, both
///        days counted, in stall-days.
inline float StackDaysToMowing(const YardHayYear& year, float coverage, std::uint32_t day_of_year) {
  float days = 0.0F;
  const std::uint32_t left = DaysToMowing(year, day_of_year);
  for (std::uint32_t step = 0; step <= left; ++step) {
    days += StackDayDraw(year, coverage, day_of_year + step);
  }
  return days;
}

/// @brief What one young of a kind eats from the stack between its birth and
///        the next mowing, as a share of an adult head's year: births
///        spread evenly over the days of the birth band, nothing from the
///        stack while a newborn at the dam, the juvenile's feed factor until
///        it is grown, a whole head's after. The herd day's own numbers
///        (herd_life.cpp BreedHerd, herd_system.cpp's feeding), so the
///        share follows the tables when someone edits them.
/// @param birth_from_month,birth_to_month The calving season, 0-based, inclusive (farming.csv).
/// @param newborn_months,adult_from_months The kind's livestock.csv game months, from birth.
/// @param juvenile_feed_factor farming.csv: a juvenile's share of an adult's feed.
/// @return 0..1; 0 when the band is empty or an adult eats nothing from the stack.
inline float YoungStackShare(const YardHayYear& year,
                             float coverage,
                             std::uint8_t birth_from_month,
                             std::uint8_t birth_to_month,
                             float newborn_months,
                             float adult_from_months,
                             float juvenile_feed_factor) {
  const float adult_year = StackDaysOfYear(year, coverage);
  if (birth_to_month < birth_from_month || !(adult_year > 0.0F)) {
    return 0.0F;
  }
  const std::uint32_t first = static_cast<std::uint32_t>(birth_from_month) * kDaysPerMonth;
  const std::uint32_t last =
      ((static_cast<std::uint32_t>(birth_to_month) + 1U) * kDaysPerMonth) - 1U;
  const auto weaned = static_cast<float>(kDaysPerMonth) * newborn_months;
  const auto grown = static_cast<float>(kDaysPerMonth) * adult_from_months;
  float eaten = 0.0F;
  for (std::uint32_t born = first; born <= last; ++born) {
    const std::uint32_t left = DaysToMowing(year, born);
    for (std::uint32_t age = 0; age <= left; ++age) {
      const auto days_old = static_cast<float>(age);
      if (days_old < weaned) {
        continue;
      }
      const float weight = days_old < grown ? juvenile_feed_factor : 1.0F;
      eaten += weight * StackDayDraw(year, coverage, born + age);
    }
  }
  return eaten / static_cast<float>(last - first + 1U) / adult_year;
}

}  // namespace core

#endif  // CORE_COMMON_YARD_HAY_YEAR_H_
