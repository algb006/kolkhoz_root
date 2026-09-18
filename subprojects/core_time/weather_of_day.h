/// @file
/// @brief The weather of one day from nothing but the seed and the day
/// number: the season table, the five sky steps, and the rules that read the
/// rest of the day from the step (camera design §4, «Небо — пять ступеней»).
/// @threading PARALLEL_READONLY
/// Pure functions of their arguments; the time-and-weather slot (phase 1)
/// and the forecast call them, and neither holds state here.
///
/// WHY A FILE OF ITS OWN (2026-09-18). The generator lived inside
/// time_system.cpp beside the phase and the table parsers, and the five
/// steps would have taken that file past the thousand-line limit. The cut
/// follows the one question each half answers: this file says what a day IS,
/// time_system.cpp says when it is written and where its numbers come from.

#ifndef CORE_TIME_WEATHER_OF_DAY_H_
#define CORE_TIME_WEATHER_OF_DAY_H_

#include <array>
#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/world_state.h"

namespace core {

// Temperature scale bounds (visual design §4): no extremes by design.
inline constexpr float kTemperatureMinCelsius = -15.0F;
inline constexpr float kTemperatureMaxCelsius = 30.0F;

/// @brief Per-season weather parameters: tables/weather.csv by season, and
/// the climate-wide knobs of tables/weather_params.csv copied into every
/// season (one reading, four copies — never four readings).
struct SeasonWeather {
  float temperature_mean_celsius = 5.0F;

  float temperature_spread_celsius = 5.0F;

  /// Half the diurnal swing: day = mean + amplitude, night = mean −
  /// amplitude (camera design §4). The step multiplies it (sky_swing).
  float temperature_amplitude_celsius = 0.0F;

  /// AR(1) memory of the day's temperature, 0..0.95. Zero in the shipped
  /// table, and deliberately: memory closed the sowing window
  /// (tables/weather.csv's header has the measured price).
  float temperature_memory = 0.0F;

  /// THE SKY'S SHARES, percent of the season's days by step, SkyStep order;
  /// they sum to 100 (tables/weather.csv `sky_1_percent`..`sky_5_percent`).
  /// Steps 4 + 5 are the season's WET SHARE — the one home of that number
  /// since 2026-09-18, when `precipitation_chance_percent` was retired for
  /// saying the same thing a second time. The defaults are boss's table as
  /// relaid that day so that 4 + 5 keeps the measured 35 / 35 / 25 / 45.
  std::array<float, kSkyStepCountValue> sky_percent = {20.0F, 20.0F, 30.0F, 20.0F, 10.0F};

  /// The swing's PROPORTIONS by step (weather_params `sky_swing_1`..`_5`):
  /// clear ×1.4 down to heavy ×0.6. Normalised by season against the shares
  /// above so that the season's mean multiplier is exactly one by
  /// construction — the swing moves, the season's mean swing does not
  /// (SkySwingMultiplier).
  std::array<float, kSkyStepCountValue> sky_swing = {1.4F, 1.15F, 0.8F, 0.7F, 0.6F};

  // -- wind and the names of days -------------------------------------------

  /// Share of days that are still, 0..1, on steps 1–4. The rest split
  /// between kWind and kStrongWind by `wind_share`.
  float calm_share = 0.35F;
  float wind_share = 0.5F;

  /// Thunderstorm window, 0-BASED months inclusive — May to August, 4..7.
  /// Step 5 in the warm inside it is ALWAYS a thunderstorm (the human's word,
  /// 2026-09-18); outside it, a downpour. The table spells months 1..12 and
  /// the parser converts in one place.
  std::uint8_t thunder_from_month = 4;
  std::uint8_t thunder_to_month = 7;

  /// Blizzard window, 0-based, WRAPPING the year end — December to February,
  /// 11..1. Step 5 in the cold inside it is ALWAYS a blizzard; outside it,
  /// heavy snowfall.
  std::uint8_t blizzard_from_month = 11;
  std::uint8_t blizzard_to_month = 1;

  /// Below this daily mean the day is still and clear or partly cloudy and
  /// nothing else: «самый жестокий мороз — в самый тихий и ясный день». −12.
  float still_frost_celsius = -12.0F;

  /// The forms' temperature bounds: a wet day's mean under −1 is snow, over
  /// +1 is rain, and between is WET SNOW — drawn as snow, counted as rain.
  float snow_below_celsius = -1.0F;
  float rain_above_celsius = 1.0F;

  /// The heavy phase of step 5, hours, both ends inclusive: 2..12.
  float heavy_hours_min = 2.0F;
  float heavy_hours_max = 12.0F;

  /// A dry clear night at or below this is named a frost — inside the frost
  /// window only, because every winter night is under zero.
  float frost_night_celsius = 0.0F;
  std::uint8_t frost_from_month = 3;
  std::uint8_t frost_to_month = 9;

  /// Afternoon at or above which a clear day is named ЗНОЙ. +28. Not
  /// `drought_temp_c` and not the +25 of ЖАРА — three facts (boss,
  /// 2026-09-05).
  float sultry_afternoon_celsius = 28.0F;

  /// Afternoon at or above which the day is HOT — ЖАРА, +25, said by
  /// EventKind::kHotAfternoon whatever the sky.
  float hot_afternoon_celsius = 25.0F;

  /// Share of still or lightly windy dry days on steps 1–3 that fog over.
  float fog_share = 0.2F;

  /// Daily mean at or above which a snow cover melts away, in one day.
  float snow_melt_celsius = 2.0F;
};

using SeasonTable = std::array<SeasonWeather, kSeasonsPerYear>;

/// @brief STUB defaults for a table set without a weather table (unit tests,
/// early runs): mild seasons inside the design bounds, and boss's sky table.
/// A present-but-malformed table is an error, never a silent fallback.
SeasonTable DefaultSeasonTable();

/// @brief Season mean temperature interpolated over the circular year.
float SeasonalMeanTemperature(const SeasonTable& seasons, std::uint32_t day_of_year);

/// @brief The season row a day of the year belongs to.
const SeasonWeather& SeasonOfDayOfYear(const SeasonTable& seasons, std::uint32_t day_of_year);

/// @brief The swing multiplier of `step` in `season`: its proportion divided
/// by the season's share-weighted mean proportion, so that the multiplier
/// averages to exactly one over the table's own shares.
float SkySwingMultiplier(const SeasonWeather& season, SkyStep step);

/// @brief The weather of ONE DAY, past or future, from the seed and the day
/// number alone. ONE HOME FOR THE RULE: the phase writes what this returns
/// and the forecast asks it about days that have not happened.
/// @note snow_cover_days and cover_since_leaf_fall are left at their
/// defaults: they are history, carried by the phase (SnowCoverAfter).
WeatherState WeatherOfDay(const SeasonTable& seasons, std::uint64_t world_seed, SimDay day);

/// @brief Yesterday's snow cover carried into today: snow lays a cover, a
/// warm day takes it away, any other day leaves it lying and one day older.
std::uint16_t SnowCoverAfter(const SeasonTable& seasons,
                             const WeatherState& yesterday,
                             const WeatherState& today,
                             SimDay day);

/// @brief The day of the year at the middle of each season, winter first.
inline constexpr std::array<float, kSeasonsPerYear> kSeasonCenterDay = {1.5F, 13.5F, 25.5F, 37.5F};

}  // namespace core

#endif  // CORE_TIME_WEATHER_OF_DAY_H_
