/// @file
/// @brief The early edge of the autumn's first snow: the last day a count of
/// the reaping may still promise, read off the climate's own share of
/// campaigns in which the snow has already lain by each day.
/// @threading PARALLEL_READONLY
/// Pure functions over a constant table.
///
/// WHY AN EDGE OF ITS OWN (farming design §6, the harvest rule 5; boss,
/// core-boss-potato-crew-trace-2026-10-01 [35]-[36]). The climate's mean edge
/// (ITimeSystem::GrowingSeasonLastDay) is a day the snow has beaten in half
/// the years, and for a warning that is the wrong side to err on: a missed
/// «not in time» costs the crop, a false one costs a day off. Until 0.37.147
/// the gathering alarm counted to world_params `gather_alarm_snow_day` — the
/// P10 of host's 2000-year probe, re-read by hand whenever the weather table
/// moved — and labor's last days before the snow counted to the mean: two
/// readers of one question with two dates. The edge is now counted off the
/// generator itself, as the rain days are, and both read it.

#ifndef CORE_COMMON_EARLY_SNOW_H_
#define CORE_COMMON_EARLY_SNOW_H_

#include <array>
#include <cstdint>

#include "core_common/calendar.h"

namespace core {

/// @brief For each day of the year, the share of campaigns, 0..1, in which
/// the autumn's first snow day has come ON OR BEFORE it — counted from
/// midsummer, so the days of spring and early summer read nought
/// (core_time/time_system.h, ITimeSystem::ClimateSnowLainShares). All zeros
/// means «no snow in this climate», which is what a build without a weather
/// table gets.
using SnowLainShares = std::array<float, kDaysPerYear>;

/// @brief The last day of the year a reaping may still be promised: the day
/// before the first one by which the snow has lain in at least `share` of
/// the campaigns, and never past `mean_last_day`.
///
/// THE SNOW'S OWN DAY COUNTS FOR NOTHING: a standing field is taken on its
/// morning (production_system.cpp, RunFields), so the edge is the day before
/// (econ, econ-boss-snow-edge-reading).
/// @param share The share of years the count may promise a day that the
///        snow then takes, 0..1 (farming.csv `early_snow_share`).
/// @param mean_last_day The climate's mean edge, the fallback when the snow
///        never reaches `share`.
constexpr std::uint32_t EarlySnowLastDay(const SnowLainShares& lain,
                                         float share,
                                         std::uint32_t mean_last_day) {
  for (std::uint32_t day = 0; day < kDaysPerYear; ++day) {
    if (lain[day] > 0.0F && lain[day] >= share) {
      const std::uint32_t edge = day > 0 ? day - 1U : 0U;
      return edge < mean_last_day ? edge : mean_last_day;
    }
  }
  return mean_last_day;
}

}  // namespace core

#endif  // CORE_COMMON_EARLY_SNOW_H_
