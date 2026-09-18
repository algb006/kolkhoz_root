// The weather of one day (core_time/weather_of_day.h).
//
// Weather is a pure function of (world_seed, day) via the counter hash, so it
// is identical for a given seed no matter what else the simulation did — no
// draw from the sequential RNG, replay-stable by construction, and the
// three-day forecast costs nothing.

#include "weather_of_day.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "core_common/random.h"
#include "daylight_table.h"

namespace core {
namespace {

// Counter-hash salts of this module's daily draws. SEPARATE SALTS ARE THE
// WHOLE POINT for the wind: it is meant to be unrelated to the sky, and
// sharing a stream would make it a function of it (Кожаный босс, 2026-09-05).
// 0x7432 (the wet chain), 0x7433 (the sky latent), 0x7434 (its anchor) and
// 0x7436 (the thunder draw) belonged to the weather before the sky steps and
// are not reused: a salt read two ways is two weathers under one number.
constexpr std::uint64_t kTemperatureSalt = 0x7431;
constexpr std::uint64_t kWindSalt = 0x7435;
constexpr std::uint64_t kFogSalt = 0x7437;
constexpr std::uint64_t kSkySalt = 0x7438;
constexpr std::uint64_t kHeavyStartSalt = 0x7439;
constexpr std::uint64_t kHeavyLengthSalt = 0x743A;

/// HOW FAR BACK TODAY'S TEMPERATURE REMEMBERS: long enough that the anchor
/// is forgotten (0.7^24 = 1.9e-4). Memory is off in the shipped table.
constexpr std::uint32_t kWeatherWindowDays = 24;

/// THE CLOUD OF EACH STEP, for the readers of `cloud_cover` that predate the
/// steps. A reading of the step and nothing more — the step is the seam's
/// word, and nothing in the core decides by this number any longer.
constexpr std::array<float, kSkyStepCountValue> kCloudOfStep = {0.05F, 0.4F, 0.85F, 0.9F, 1.0F};

constexpr std::uint32_t kHoursPerDay = 24;

/// A signed unit draw, -1..1, mean zero.
float SignedUnitDraw(std::uint64_t seed, SimDay day, std::uint64_t salt) {
  return CounterHashUnitFloat(seed, day, 0, salt) * 2.0F - 1.0F;
}

/// @brief An AR(1) latent unrolled over the window from an independent
/// anchor, scaled by sqrt(1-a^2) so that its stationary variance equals the
/// draw's: memory redistributes WHEN, not how much. At memory 0 it is bit for
/// bit the single draw of the day. Clamped symmetrically to -1..1.
float MemoryLatent(std::uint64_t seed, SimDay day, std::uint64_t salt, float memory) {
  const float step = std::sqrt(1.0F - memory * memory);
  const auto today = static_cast<std::int64_t>(day);
  float latent =
      SignedUnitDraw(seed, static_cast<SimDay>(today - kWeatherWindowDays), salt ^ 0x5A5AU);
  for (std::int64_t at = today - kWeatherWindowDays + 1; at <= today; ++at) {
    latent = memory * latent + step * SignedUnitDraw(seed, static_cast<SimDay>(at), salt);
  }
  return std::clamp(latent, -1.0F, 1.0F);
}

/// The 0-based month a day of the year falls in.
std::uint8_t MonthOfDayOfYear(std::uint32_t day_of_year) {
  return static_cast<std::uint8_t>((day_of_year / kDaysPerMonth) % kMonthsPerYear);
}

/// Whether `month` lies in [from, to], 0-based inclusive, the window
/// WRAPPING the year end when `from` is past `to` — the blizzard's December
/// to February is one window, not two.
bool InMonthWindow(std::uint8_t month, std::uint8_t from, std::uint8_t to) {
  return from <= to ? (month >= from && month <= to) : (month >= from || month <= to);
}

/// The day's mean temperature: the season's interpolated mean plus its noise,
/// held inside the thermometer's scale.
float MeanTemperatureOfDay(const SeasonTable& seasons, std::uint64_t seed, SimDay day) {
  const std::uint32_t day_of_year = day % kDaysPerYear;
  const SeasonWeather& season = SeasonOfDayOfYear(seasons, day_of_year);
  const float noise = MemoryLatent(seed, day, kTemperatureSalt, season.temperature_memory);
  return std::clamp(
      SeasonalMeanTemperature(seasons, day_of_year) + noise * season.temperature_spread_celsius,
      kTemperatureMinCelsius,
      kTemperatureMaxCelsius);
}

/// @brief The day's step BEFORE the rule that forbids two heavy days running:
/// one draw against the season's shares, then the still frost's cut.
///
/// A DRAW PER DAY, NOT A SERIES (boss, 2026-09-18). Series of 2–4 days were
/// in the first reading of the section and were taken out of it: a sky that
/// remembers is a wet spell by another name, and wet spells are exactly what
/// closed the sowing window when the weather had memory (tables/weather.csv).
/// The condition to bring them back is written in the section.
SkyStep StepBeforeHeavyRule(const SeasonTable& seasons, std::uint64_t seed, SimDay day) {
  const SeasonWeather& season = SeasonOfDayOfYear(seasons, day % kDaysPerYear);
  const float roll = CounterHashUnitFloat(seed, day, 0, kSkySalt) * 100.0F;
  float below = 0.0F;
  auto step = SkyStep::kHeavyPrecipitation;  // what a rounding shortfall lands on
  for (std::size_t index = 0; index < kSkyStepCountValue; ++index) {
    below += season.sky_percent[index];
    if (roll < below) {
      step = static_cast<SkyStep>(index);
      break;
    }
  }
  // THE STILL FROST, the older rule: below −12 the day is clear or partly
  // cloudy and nothing else. In the shipped climate it cannot fire — the
  // coldest day in two hundred years of it is −11.3 (weather_shape,
  // 2026-09-18) — and the run prints how many days it cut, so its nought is
  // read beside the count of days that were cold enough to be asked.
  if (MeanTemperatureOfDay(seasons, seed, day) < season.still_frost_celsius &&
      step > SkyStep::kPartlyCloudy) {
    step = SkyStep::kPartlyCloudy;
  }
  return step;
}

}  // namespace

SeasonTable DefaultSeasonTable() {
  SeasonTable seasons;
  const std::array<float, kSeasonsPerYear> means = {-8.0F, 5.0F, 19.0F, 6.0F};
  const std::array<float, kSeasonsPerYear> spreads = {5.0F, 6.0F, 5.0F, 6.0F};
  // Boss's table as relaid 2026-09-18 (camera design §4): 4 + 5 keeps the
  // measured wet shares 35 / 35 / 25 / 45.
  const std::array<std::array<float, kSkyStepCountValue>, kSeasonsPerYear> shares = {{
      {20.0F, 15.0F, 30.0F, 28.0F, 7.0F},   // winter
      {15.0F, 20.0F, 30.0F, 23.0F, 12.0F},  // spring
      {30.0F, 30.0F, 15.0F, 17.0F, 8.0F},   // summer
      {10.0F, 15.0F, 30.0F, 32.0F, 13.0F},  // autumn
  }};
  for (std::size_t season = 0; season < kSeasonsPerYear; ++season) {
    seasons[season].temperature_mean_celsius = means[season];
    seasons[season].temperature_spread_celsius = spreads[season];
    seasons[season].sky_percent = shares[season];
  }
  return seasons;
}

float SeasonalMeanTemperature(const SeasonTable& seasons, std::uint32_t day_of_year) {
  const auto day = static_cast<float>(day_of_year);
  for (std::uint32_t season = 0; season < kSeasonsPerYear; ++season) {
    const std::uint32_t next = (season + 1) % kSeasonsPerYear;
    const float from = kSeasonCenterDay[season];
    float to = kSeasonCenterDay[next];
    float position = day;
    if (to < from) {  // the autumn -> winter segment wraps the year end
      to += static_cast<float>(kDaysPerYear);
      if (position < from) {
        position += static_cast<float>(kDaysPerYear);
      }
    }
    if (position >= from && position <= to) {
      const float blend = (position - from) / (to - from);
      return seasons[season].temperature_mean_celsius * (1.0F - blend) +
             seasons[next].temperature_mean_celsius * blend;
    }
  }
  return seasons[0].temperature_mean_celsius;  // unreachable: segments cover the year
}

const SeasonWeather& SeasonOfDayOfYear(const SeasonTable& seasons, std::uint32_t day_of_year) {
  const auto month = static_cast<Month>((day_of_year / kDaysPerMonth) % kMonthsPerYear);
  return seasons[static_cast<std::uint32_t>(SeasonOfMonth(month))];
}

float SkySwingMultiplier(const SeasonWeather& season, SkyStep step) {
  float mean = 0.0F;
  for (std::size_t index = 0; index < kSkyStepCountValue; ++index) {
    mean += season.sky_percent[index] / 100.0F * season.sky_swing[index];
  }
  const auto index = static_cast<std::size_t>(step);
  return mean > 0.0F && index < kSkyStepCountValue ? season.sky_swing[index] / mean : 1.0F;
}

WeatherState WeatherOfDay(const SeasonTable& seasons, std::uint64_t world_seed, SimDay day) {
  WeatherState weather;
  const std::uint32_t day_of_year = day % kDaysPerYear;
  weather.daylight_hours = kDaylightGameHours[day_of_year];
  const SeasonWeather& season = SeasonOfDayOfYear(seasons, day_of_year);
  const float temperature = MeanTemperatureOfDay(seasons, world_seed, day);
  weather.air_temperature_celsius = temperature;

  // -- the sky, first ---------------------------------------------------------
  //
  // NEVER TWO HEAVY DAYS RUNNING, and the rule reads yesterday's step as it
  // stood BEFORE this same rule — which keeps the function pure (no walk back
  // to the start of the world) and still makes two heavy days in a row
  // impossible: today can be heavy only if yesterday was not heavy before
  // the rule, and yesterday was heavy after it only if it was heavy before.
  // What the rule cuts goes to step 4, never to a dry step, so the wet share
  // stands (boss, 2026-09-18). Day nought has no yesterday in the campaign.
  weather.sky = StepBeforeHeavyRule(seasons, world_seed, day);
  if (weather.sky == SkyStep::kHeavyPrecipitation && day > 0 &&
      StepBeforeHeavyRule(seasons, world_seed, day - 1U) == SkyStep::kHeavyPrecipitation) {
    weather.sky = SkyStep::kLightPrecipitation;
  }
  weather.cloud_cover = kCloudOfStep[static_cast<std::size_t>(weather.sky)];

  // THE SWING IS THE ONLY THING THE SKY MAY TOUCH of the temperature: the mean
  // stays where the season table put it, so nothing else in the simulation
  // drifts, and the multiplier averages to one over the season's own shares.
  weather.temperature_swing_celsius =
      season.temperature_amplitude_celsius * SkySwingMultiplier(season, weather.sky);

  const bool wet = weather.sky >= SkyStep::kLightPrecipitation;
  const bool heavy = weather.sky == SkyStep::kHeavyPrecipitation;
  const bool cold = temperature < season.snow_below_celsius;
  const bool warm = temperature > season.rain_above_celsius;
  const std::uint8_t month = MonthOfDayOfYear(day_of_year);
  // WET SNOW IS COUNTED AS RAIN: between −1 and +1 the precipitation is rain
  // for every rule that reads it — the harvest stops, no cover is laid — and
  // only its picture is snow.
  weather.precipitation =
      !wet ? Precipitation::kNone : (cold ? Precipitation::kSnow : Precipitation::kRain);

  // -- the form of a wet day --------------------------------------------------
  if (heavy) {
    if (cold) {
      weather.phenomenon =
          InMonthWindow(month, season.blizzard_from_month, season.blizzard_to_month)
              ? WeatherPhenomenon::kBlizzard
              : WeatherPhenomenon::kHeavySnowfall;
    } else {
      // «Так же, как в тепле» for wet snow on step 5: a downpour or a storm.
      weather.phenomenon = InMonthWindow(month, season.thunder_from_month, season.thunder_to_month)
                               ? WeatherPhenomenon::kThunderstorm
                               : WeatherPhenomenon::kRain;
    }
    // THE HEAVY PHASE: an hour it begins and a length of 2..12, the rest of
    // the day step 4. Its own two draws, so neither moves any other number.
    const float start = CounterHashUnitFloat(world_seed, day, 0, kHeavyStartSalt);
    const float length = CounterHashUnitFloat(world_seed, day, 0, kHeavyLengthSalt);
    const auto span = static_cast<std::uint32_t>(season.heavy_hours_max - season.heavy_hours_min);
    weather.heavy_from_hour = static_cast<std::uint8_t>(
        std::min(static_cast<std::uint32_t>(start * kHoursPerDay), kHoursPerDay - 1U));
    weather.heavy_hours = static_cast<std::uint8_t>(
        static_cast<std::uint32_t>(season.heavy_hours_min) +
        std::min(static_cast<std::uint32_t>(length * static_cast<float>(span + 1U)), span));
  } else if (wet) {
    weather.phenomenon = warm ? WeatherPhenomenon::kRain : WeatherPhenomenon::kSnowfall;
  }

  // -- the wind ---------------------------------------------------------------
  //
  // Its own salt on steps 1–4: «в остальных случаях ветер рандом». On step 5
  // it is strong — a squall in a storm — and it blows inside the heavy phase,
  // whose hours the layer has. Below the still frost it is still, and that is
  // the one exception to "random", older than the steps.
  const float gust = CounterHashUnitFloat(world_seed, day, 0, kWindSalt);
  if (temperature < season.still_frost_celsius) {
    weather.wind = WindBand::kCalm;
  } else if (heavy) {
    weather.wind = weather.phenomenon == WeatherPhenomenon::kThunderstorm ? WindBand::kSquall
                                                                          : WindBand::kStrongWind;
  } else {
    weather.wind = gust < season.calm_share                       ? WindBand::kCalm
                   : gust < season.calm_share + season.wind_share ? WindBand::kWind
                                                                  : WindBand::kStrongWind;
  }

  // -- one sign beside a dry sky ---------------------------------------------
  //
  // Frost, fog, heat, in that order when two could stand: the frost kills the
  // sowing in a night and matters most. Each has the steps it belongs to.
  if (!wet) {
    const float afternoon = temperature + weather.temperature_swing_celsius;
    const float night = temperature - weather.temperature_swing_celsius;
    const bool clear_or_broken = weather.sky <= SkyStep::kPartlyCloudy;
    if (clear_or_broken && night <= season.frost_night_celsius &&
        InMonthWindow(month, season.frost_from_month, season.frost_to_month)) {
      weather.phenomenon = WeatherPhenomenon::kFrost;
    } else if (weather.wind <= WindBand::kWind &&
               CounterHashUnitFloat(world_seed, day, 0, kFogSalt) < season.fog_share) {
      weather.phenomenon = WeatherPhenomenon::kFog;  // steps 1–3: every dry step
    } else if (clear_or_broken && afternoon >= season.sultry_afternoon_celsius) {
      weather.phenomenon = WeatherPhenomenon::kHeat;
    }
  }
  return weather;
}

std::uint16_t SnowCoverAfter(const SeasonTable& seasons,
                             const WeatherState& yesterday,
                             const WeatherState& today,
                             SimDay day) {
  // A day whose precipitation is snow always leaves a cover; a day whose MEAN
  // is at or above the melt threshold clears it whole; anything else leaves
  // it as it was, one day older. A dusting that thaws never reaches its
  // second day, which is what makes the count mean "settled". Saturates
  // rather than wrapping into "bare ground" in the middle of February.
  constexpr std::uint16_t kLongestCover = 65000U;
  const SeasonWeather& season = SeasonOfDayOfYear(seasons, day % kDaysPerYear);
  const std::uint16_t lying = yesterday.snow_cover_days;
  const std::uint16_t older =
      lying < kLongestCover ? static_cast<std::uint16_t>(lying + 1U) : lying;
  if (today.precipitation == Precipitation::kSnow) {
    return older;
  }
  if (today.air_temperature_celsius >= season.snow_melt_celsius || lying == 0) {
    return 0;
  }
  return older;
}

}  // namespace core
