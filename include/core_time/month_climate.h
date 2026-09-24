/// @file
/// @brief The air temperature of a MONTH — its mean, its mean day and its
///        mean night — read from the balance tables without a world.
/// @threading PARALLEL_READONLY
/// A pure function of the table set and the month: no world, no simulation,
/// no state. Callable from any thread, and from outside the simulation.
///
/// WHY IT IS PUBLIC (boss, boss-core-month-temperature-2026-09-25; the
/// human's word: «Температура в режиме просмотра нужна иначе как мир
/// перестроится по времени года ?»). The viewing mode stages a chosen month
/// over the core's own January, and the only way to that month's
/// temperature was to run the simulation forward to it. Beside
/// core_common/daylight.h's SolarWindowOfMonthSecondDay, and on its
/// convention: the SECOND of the month's four days.
///
/// NOT constexpr, unlike the daylight: the climate is balance, in
/// tables/weather.csv and weather_params.csv, parsed by the same reading the
/// time phase builds its seasons from — one home, no second table.
///
/// NO HOURS. The design knows a day and a night — «День = среднее + размах,
/// ночь = среднее − размах» (camera design §4) — and no curve between them;
/// boss, same thread [3]: the hourly form is not wanted.

#ifndef CORE_TIME_MONTH_CLIMATE_H_
#define CORE_TIME_MONTH_CLIMATE_H_

#include <cstdint>
#include <string>

#include "core_common/calendar.h"
#include "core_common/world_state.h"

namespace core {

class ITableSet;

/// @brief A month's air temperature, degrees Celsius.
struct MonthClimate {
  /// The mean of the month's second day: the season means interpolated over
  /// the year (SeasonalMeanTemperature) — what every day's draw is spread
  /// about. The day's own noise averages to nought.
  float mean_celsius = 0.0F;

  /// The mean afternoon: mean + the season's half-swing. A given day's sky
  /// widens or narrows it (×1.4 clear to ×0.6 heavy), and the season's mean
  /// multiplier is one by construction — so this is the month's typical
  /// afternoon, not any one day's.
  float day_celsius = 0.0F;

  /// The mean night: mean − the season's half-swing, on the same terms.
  float night_celsius = 0.0F;

  // -- the snow, and the typical sky (boss, same thread [5]; 0.35.18) ----------
  //
  // THE SNOW IS HISTORY, not a function of the day: a cover lies because it
  // fell and has not melted (SnowCoverAfter). So these are read off the same
  // rules the time phase runs — WeatherOfDay, SnowCoverAfter, the leaf-fall
  // reset — walked day by day over kMonthClimateYears years of one seed,
  // taking the month's SECOND day of each year after the first (a cover
  // cannot be inherited by the first January). No world, no second table.

  /// Share of those years in which the day lies under a snow cover, 0..1.
  float snow_cover_share = 0.0F;

  /// The typical day's cover in DAYS lying — the median over all those
  /// years, so 0 when fewer than half are covered. The core knows days of
  /// cover, not a depth, and derives none.
  std::uint16_t snow_cover_days = 0;

  /// Whether a cover has lain since the leaf fall, in most of those years
  /// (WeatherState::cover_since_leaf_fall: the fallen leaf has rotted).
  bool cover_since_leaf_fall = false;

  /// The day's most frequent phenomenon and wind over those years — the
  /// typical, not any one day's.
  WeatherPhenomenon phenomenon = WeatherPhenomenon::kNone;
  WindBand wind = WindBand::kCalm;
};

/// @brief Years the snow and the typical sky are read over, after one year
///        of warm-up.
inline constexpr std::uint32_t kMonthClimateYears = 200;

/// @brief The seed those years are drawn from: fixed, so the door answers
///        the same for the same tables.
inline constexpr std::uint64_t kMonthClimateSeed = 1;

/// @brief The air temperature of `month`, from the weather tables of `tables`.
/// @param climate Written on success, untouched on failure.
/// @param error Why it failed: no `weather` table, or a malformed one.
/// @return false when the set has no weather table or it does not parse.
bool MonthClimateOfTables(const ITableSet& tables,
                          Month month,
                          MonthClimate& climate,
                          std::string& error);

}  // namespace core

#endif  // CORE_TIME_MONTH_CLIMATE_H_
