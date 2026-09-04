// Implementation of the core_time boundary (include/core_time/time_system.h).
// Stage 2: the phase advances the clock, refreshes the calendar caches and
// writes the day's weather — daylight from the solar curve (daylight_table.h),
// temperature and precipitation from the per-season weather table.
//
// Weather is a pure function of (world_seed, day) via the counter hash, so it
// is identical for a given seed no matter what else the simulation did — no
// draw from the sequential RNG, replay-stable by construction.

#include "core_time/time_system.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

#include "core_common/calendar.h"
#include "core_common/random.h"
#include "core_common/world_state.h"
#include "core_log/log.h"
#include "core_tables/tables.h"
#include "daylight_table.h"

namespace core {
namespace {

// Temperature scale bounds (visual design §4): no extremes by design.
constexpr float kTemperatureMinCelsius = -15.0F;
constexpr float kTemperatureMaxCelsius = 30.0F;

// Counter-hash salts of this module's daily draws.
constexpr std::uint64_t kTemperatureSalt = 0x7431;
constexpr std::uint64_t kPrecipitationSalt = 0x7432;
constexpr std::uint64_t kCloudSalt = 0x7433;
constexpr std::uint64_t kPrecipitationAnchorSalt = 0x7434;

/// HOW FAR BACK TODAY REMEMBERS. The weather keeps being a pure function of
/// (seed, day) — nothing is carried in world state — but the function now
/// walks a window of days forward instead of drawing one number, so today
/// depends on the days before it.
///
/// The window has to be long enough that its far end is forgotten: the
/// anchor's weight is a^W for the temperature (0.7^24 = 1.9e-4) and s^W for
/// the wet chain (0.5^24 = 6e-8). Shorter and the anchor would show through
/// as a seam every W days.
constexpr std::uint32_t kWeatherWindowDays = 24;

/// How much wetter a rain day's sky is, in latent units, and the dry day's
/// counterweight. THE BIAS AVERAGES TO EXACTLY ZERO by construction: a wet
/// day gets +bias, a dry day gets -bias*p/(1-p), and p*bias equals
/// (1-p)*bias*p/(1-p) for any p. That is the whole reason it is written this
/// way rather than as "rain means overcast": rain implies cloud, and if the
/// implication were a floor it would lift the season's mean cloud, which
/// would quietly shrink every swing — the silent climate shift this parcel
/// exists to avoid.
constexpr float kRainCloudBias = 0.9F;

/// A signed unit draw, -1..1, mean zero.
float SignedUnitDraw(std::uint64_t seed, SimDay day, std::uint64_t salt) {
  return CounterHashUnitFloat(seed, day, 0, salt) * 2.0F - 1.0F;
}

/// @brief An AR(1) latent: x = memory*x + sqrt(1-memory^2)*draw, unrolled
/// over the window from an independent anchor.
///
/// The scaling is what keeps this honest. sqrt(1-a^2) is exactly the factor
/// at which the recursion's stationary VARIANCE equals the draw's, so the
/// spread the table asks for is the spread that comes out; only the ORDER of
/// the days changes. Memory redistributes when, not how much.
///
/// The result is clamped to -1..1 like the single draw it replaces: a sum of
/// scaled draws can reach sqrt((1+a)/(1-a)) = 2.4 at a=0.7, and the table's
/// bound check (mean +- spread +- amplitude inside -15..+30) is written for
/// a latent of one. The clamp is symmetric, so it costs a little of the
/// spread and none of the mean.
float MemoryLatent(std::uint64_t seed, SimDay day, std::uint64_t salt, float memory) {
  const float step = std::sqrt(1.0F - memory * memory);
  // EVERY DAY DRAWS AT ITS OWN INDEX, and that is not a matter of taste: at
  // memory 0 the recursion collapses to `latent = draw(day)`, which is bit
  // for bit the single draw this function replaced. So "no memory" is the
  // weather as it was published, and every difference measured against that
  // control belongs to the memory and to nothing else. An offset index made
  // the control a THIRD weather, and the two first-year reference runs went
  // red with every knob at zero — which is how it was caught.
  //
  // The window reaches back before day zero, so the arithmetic is signed;
  // the cast to the hash's unsigned index wraps there, deterministically and
  // harmlessly, because a hash input is an identity and not a quantity.
  const auto today = static_cast<std::int64_t>(day);
  const auto draw_at = [seed, salt](std::int64_t at) {
    return SignedUnitDraw(seed, static_cast<SimDay>(at), salt);
  };
  float latent =
      SignedUnitDraw(seed, static_cast<SimDay>(today - kWeatherWindowDays), salt ^ 0x5A5AU);
  for (std::int64_t at = today - kWeatherWindowDays + 1; at <= today; ++at) {
    latent = memory * latent + step * draw_at(at);
  }
  return latent < -1.0F ? -1.0F : (latent > 1.0F ? 1.0F : latent);
}

/// Per-season weather parameters, parsed from tables/weather.csv.
struct SeasonWeather {
  float temperature_mean_celsius = 5.0F;

  float temperature_spread_celsius = 5.0F;

  /// Half the diurnal swing: day = mean + amplitude, night = mean - amplitude
  /// (camera design §4). The stored weather stays ONE number, the daily mean;
  /// the readers that want the afternoon (heat, drought) or the night
  /// (frost) add or subtract this themselves.
  float temperature_amplitude_celsius = 0.0F;

  float precipitation_chance_percent = 30.0F;

  /// AR(1) memory of the day's temperature and of the sky, 0..0.95. Zero is
  /// the weather this module had until 2026-09-04: every day drawn on its
  /// own, so a run of N alike days decayed geometrically and the phenomena
  /// the design names by DURATION — "long heat without rain", "drawn-out
  /// rains" — were nearly absent from the model whatever their strength.
  /// Strength changes how deep, memory changes how long.
  float temperature_memory = 0.0F;

  /// Persistence of the wet/dry chain, 0..0.95, on the same terms. The
  /// chain's stationary share is the chance above EXACTLY, for any
  /// persistence — see WetDay.
  float wet_persistence = 0.0F;

  /// How far the sky moves the diurnal swing, 0..1: a clear day swings by
  /// (1 + this), an overcast one by (1 - this). Clear noon is hotter and
  /// clear midnight colder, which is one phenomenon in two directions.
  float cloud_swing = 0.0F;
};

using SeasonTable = std::array<SeasonWeather, kSeasonsPerYear>;

/// STUB defaults for a table set without a weather table (unit tests, early
/// runs): mild seasons inside the design bounds. A present-but-malformed
/// table is an error, never a silent fallback.
constexpr SeasonTable kDefaultSeasons = {{
    {.temperature_mean_celsius = -8.0F,
     .temperature_spread_celsius = 5.0F,
     .precipitation_chance_percent = 30.0F},  // winter
    {.temperature_mean_celsius = 5.0F,
     .temperature_spread_celsius = 6.0F,
     .precipitation_chance_percent = 30.0F},  // spring
    {.temperature_mean_celsius = 19.0F,
     .temperature_spread_celsius = 5.0F,
     .precipitation_chance_percent = 25.0F},  // summer
    {.temperature_mean_celsius = 6.0F,
     .temperature_spread_celsius = 6.0F,
     .precipitation_chance_percent = 40.0F},  // autumn
}};

/// Central day of each season inside the circular 48-day year, season order
/// winter, spring, summer, autumn. Winter is Dec+Jan+Feb (days 44..47 and
/// 0..7), so its center wraps to 1.5; the others are contiguous.
constexpr std::array<float, kSeasonsPerYear> kSeasonCenterDay = {1.5F, 13.5F, 25.5F, 37.5F};

/// @brief Season mean temperature interpolated over the circular year:
/// linear between the two nearest season centers.
float SeasonalMeanTemperature(const SeasonTable& seasons, std::uint32_t day_of_year) {
  const auto day = static_cast<float>(day_of_year);
  for (std::uint32_t season = 0; season < kSeasonsPerYear; ++season) {
    const std::uint32_t next = (season + 1) % kSeasonsPerYear;
    float from = kSeasonCenterDay[season];
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

/// @brief The season row a day of the year belongs to.
const SeasonWeather& SeasonOfDayOfYear(const SeasonTable& seasons, std::uint32_t day_of_year) {
  const auto month = static_cast<Month>((day_of_year / kDaysPerMonth) % kMonthsPerYear);
  return seasons[static_cast<std::uint32_t>(SeasonOfMonth(month))];
}

/// @brief Is this a wet day? A two-state chain, unrolled over the window.
///
/// THE SHARE OF WET DAYS IS THE TABLE'S, PERSISTENCE OR NO PERSISTENCE, and
/// that is arithmetic rather than luck. With P(wet|wet) = p + s(1-p) and
/// P(wet|dry) = p(1-s), the stationary probability is
/// r / (1 - q + r) = p(1-s) / (1 - p - s + ps + p - ps) = p(1-s) / (1-s) = p
/// for every s < 1. So `s` moves the LENGTHS of wet and dry spells and
/// nothing else — which is the invariant this parcel is built around.
///
/// Each step uses the chance of ITS OWN day's season, so the chain crosses
/// a season boundary the way weather does: the new climate takes over within
/// a few days rather than at midnight.
bool WetDay(std::uint64_t seed, SimDay day, const SeasonTable& seasons) {
  const auto today = static_cast<std::int64_t>(day);
  // The season of a day that may sit before day zero. Signed modulo, brought
  // back into 0..47 — and this is where the first version of the chain was
  // wrong: it took the season of the HASH INDEX rather than of the day, so
  // every season ran on the rain chance of the one 24 days ahead, and the
  // shares came out swapped around the year (winter 23% for its 35%).
  const auto season_of = [&seasons](std::int64_t at) -> const SeasonWeather& {
    const auto year = static_cast<std::int64_t>(kDaysPerYear);
    const auto day_of_year = static_cast<std::uint32_t>(((at % year) + year) % year);
    return SeasonOfDayOfYear(seasons, day_of_year);
  };
  const std::int64_t anchor = today - kWeatherWindowDays;
  bool wet = CounterHashUnitFloat(seed, static_cast<SimDay>(anchor), 0, kPrecipitationAnchorSalt) <
             season_of(anchor).precipitation_chance_percent / 100.0F;
  for (std::int64_t at = anchor + 1; at <= today; ++at) {
    const SeasonWeather& season = season_of(at);
    const float chance = season.precipitation_chance_percent / 100.0F;
    const float persistence = season.wet_persistence;
    const float roll = CounterHashUnitFloat(seed, static_cast<SimDay>(at), 0, kPrecipitationSalt);
    // At persistence 0 both arms are `chance`, so this reads `roll < chance`
    // — the published draw, unchanged. Same reason as in MemoryLatent.
    wet = roll < (wet ? chance + persistence * (1.0F - chance) : chance * (1.0F - persistence));
  }
  return wet;
}

/// @brief The weather of ONE DAY, past or future, from nothing but the seed
/// and the day number.
///
/// ONE HOME FOR THE RULE. The phase writes what this returns and the
/// forecast asks it about days that have not happened; a second copy of the
/// arithmetic would be a second weather, and the two would part on the first
/// edit to either. It takes the season by day-of-year rather than from the
/// calendar cache precisely so that a day ahead of the clock can be asked.
WeatherState WeatherOfDay(const SeasonTable& seasons, std::uint64_t world_seed, SimDay day) {
  WeatherState weather;
  const std::uint32_t day_of_year = day % kDaysPerYear;
  weather.daylight_hours = kDaylightGameHours[day_of_year];
  const SeasonWeather& season = SeasonOfDayOfYear(seasons, day_of_year);

  const float noise = MemoryLatent(world_seed, day, kTemperatureSalt, season.temperature_memory);
  float temperature =
      SeasonalMeanTemperature(seasons, day_of_year) + noise * season.temperature_spread_celsius;
  temperature = temperature < kTemperatureMinCelsius ? kTemperatureMinCelsius : temperature;
  temperature = temperature > kTemperatureMaxCelsius ? kTemperatureMaxCelsius : temperature;
  weather.air_temperature_celsius = temperature;

  const bool wet = WetDay(world_seed, day, seasons);
  weather.precipitation = wet ? (temperature <= 0.0F ? Precipitation::kSnow : Precipitation::kRain)
                              : Precipitation::kNone;

  // The sky. Rain implies cloud and cloud does not imply rain, so a wet day
  // is pulled TOWARDS overcast and a dry one away from it — and the pull is
  // applied to the finished 0..1 cloud rather than to the latent, because
  // the latent has to be clipped and clipping would eat the wet day's push
  // while leaving the dry day's intact. That is not a hypothetical: the
  // first version did exactly that and the mean cloud came out at 0.42 in
  // winter instead of 0.5 — a season of quietly wider swings.
  //
  // Here the mean survives by arithmetic: a wet day reads c + b(1-c) and a
  // dry one c - b*(p/(1-p))*c, so the mean over the season is
  //   c + p*b*(1-c) - (1-p)*b*p/(1-p)*c = c + p*b*(1 - 2c),
  // which is c exactly when c averages a half — and it does, the latent
  // being symmetric.
  const float chance = season.precipitation_chance_percent / 100.0F;
  const float sky = MemoryLatent(world_seed, day, kCloudSalt, season.temperature_memory);
  const float plain_cloud = 0.5F + 0.5F * sky;
  const float dry_pull = chance >= 1.0F ? 1.0F : kRainCloudBias * chance / (1.0F - chance);
  weather.cloud_cover = wet ? plain_cloud + kRainCloudBias * (1.0F - plain_cloud)
                            : plain_cloud * (1.0F - (dry_pull > 1.0F ? 1.0F : dry_pull));

  // AND THE SWING, WHICH IS THE ONLY THING THE SKY IS ALLOWED TO TOUCH. The
  // daily MEAN above is untouched by cloud: the background of every other
  // system stands where the table put it, and what moves is how far the day
  // departs from it in either direction. The season's mean multiplier is 1
  // because the mean cloud is a half — a claim measured in the run, not
  // asserted here (69-reconciliation.md §13.11).
  weather.temperature_swing_celsius =
      season.temperature_amplitude_celsius *
      (1.0F + season.cloud_swing * (1.0F - 2.0F * weather.cloud_cover));
  return weather;
}

/// Phase 1 slot: clock, calendar caches, the day's weather.
class TimeAndWeatherSlot final : public ISequentialPhase {
 public:
  explicit TimeAndWeatherSlot(const SeasonTable& seasons) : seasons_(seasons) {}

  void RunSequential(const WorldState& previous, WorldState& current) override {
    current.calendar.tick = previous.calendar.tick + 1;
    RefreshCalendarCaches(current.calendar);

    // Weather is a function of the day alone: recomputing it every tick of
    // the same day writes the same values (frozen for the rest of the step).
    // The arithmetic lives in WeatherOfDay because the forecast asks the
    // same question about days ahead — one rule, one home.
    current.weather = WeatherOfDay(seasons_, current.world_seed, current.calendar.day);
  }

 private:
  SeasonTable seasons_;
};

class TimeSystem final : public ITimeSystem {
 public:
  explicit TimeSystem(const SeasonTable& seasons) : seasons_(seasons), phase_(seasons) {}

  ISequentialPhase& TimeAndWeatherPhase() override { return phase_; }

  Precipitation PrecipitationOn(std::uint64_t world_seed, SimDay day) const override {
    return WeatherOfDay(seasons_, world_seed, day).precipitation;
  }

 private:
  SeasonTable seasons_;
  TimeAndWeatherSlot phase_;
};

/// @brief Parses tables/weather.csv into the season table.
/// @return false on a malformed table (missing season row or column, cell
/// that does not parse) — a present table is validated strictly.
bool ParseWeatherTable(const ITable& table, SeasonTable& seasons, std::string& error) {
  constexpr std::array<std::string_view, kSeasonsPerYear> kSeasonKeys = {
      "winter", "spring", "summer", "autumn"};
  const std::uint32_t mean_column = table.FindColumn("temp_mean_c");
  const std::uint32_t spread_column = table.FindColumn("temp_spread_c");
  const std::uint32_t chance_column = table.FindColumn("precipitation_chance_percent");
  // Optional: a table from before the diurnal swing keeps an amplitude of 0.
  const std::uint32_t amplitude_column = table.FindColumn("temp_amplitude_c");
  // Optional on the same terms: a table from before the weather had memory
  // reads as memoryless, which is exactly what it was.
  const std::uint32_t memory_column = table.FindColumn("temp_memory");
  const std::uint32_t persistence_column = table.FindColumn("wet_persistence");
  const std::uint32_t cloud_column = table.FindColumn("cloud_swing");
  if (mean_column == kNoTableColumn || spread_column == kNoTableColumn ||
      chance_column == kNoTableColumn) {
    error = "weather: a required column is missing";
    return false;
  }
  for (std::uint32_t season = 0; season < kSeasonsPerYear; ++season) {
    const std::uint32_t row = table.FindRowByKey(kSeasonKeys[season]);
    if (row == kNoTableRow) {
      error = "weather: no row for season '" + std::string(kSeasonKeys[season]) + "'";
      return false;
    }
    const std::optional<float> mean = table.CellReal(row, mean_column);
    const std::optional<float> spread = table.CellReal(row, spread_column);
    const std::optional<float> chance = table.CellReal(row, chance_column);
    if (!mean || !spread || !chance) {
      error = "weather: a cell of season '" + std::string(kSeasonKeys[season]) +
              "' is empty or not a number";
      return false;
    }
    float amplitude = 0.0F;
    if (amplitude_column != kNoTableColumn) {
      const std::optional<float> cell = table.CellReal(row, amplitude_column);
      if (!cell) {
        error = "weather: temp_amplitude_c of season '" + std::string(kSeasonKeys[season]) +
                "' is empty or not a number";
        return false;
      }
      amplitude = *cell;
    }
    // THE BOUNDS ARE THERMOMETER READINGS, not means: the hottest afternoon
    // and the coldest night must fit the scale (camera design §4).
    if (*mean + *spread + amplitude > kTemperatureMaxCelsius ||
        *mean - *spread - amplitude < kTemperatureMinCelsius) {
      error = "weather: season '" + std::string(kSeasonKeys[season]) +
              "' swings past the -15..+30 scale (mean +- spread +- amplitude)";
      return false;
    }
    // The three memory knobs, each 0..0.95 or 0..1: a value outside is a
    // table error and not a clamp, because a persistence of 1 is weather
    // that never changes its mind and would look like a hang.
    const auto knob = [&table, row, &error](
                          std::uint32_t column, const char* name, float top, float* out) {
      if (column == kNoTableColumn) {
        return true;
      }
      const std::optional<float> cell = table.CellReal(row, column);
      if (!cell || !(*cell >= 0.0F && *cell <= top)) {
        error = std::string("weather: ") + name + " must be a number in 0.." + std::to_string(top);
        return false;
      }
      *out = *cell;
      return true;
    };
    float memory = 0.0F;
    float persistence = 0.0F;
    float cloud_swing = 0.0F;
    if (!knob(memory_column, "temp_memory", 0.95F, &memory) ||
        !knob(persistence_column, "wet_persistence", 0.95F, &persistence) ||
        !knob(cloud_column, "cloud_swing", 1.0F, &cloud_swing)) {
      return false;
    }
    seasons[season] = {.temperature_mean_celsius = *mean,
                       .temperature_spread_celsius = *spread,
                       .temperature_amplitude_celsius = amplitude,
                       .precipitation_chance_percent = *chance,
                       .temperature_memory = memory,
                       .wet_persistence = persistence,
                       .cloud_swing = cloud_swing};
  }
  return true;
}

}  // namespace

std::unique_ptr<ITimeSystem> CreateTimeSystem(const ITableSet& tables) {
  SeasonTable seasons = kDefaultSeasons;
  if (const ITable* weather = tables.FindTable("weather")) {
    std::string error;
    if (!ParseWeatherTable(*weather, seasons, error)) {
      LogError(error);
      return nullptr;
    }
  }
  return std::make_unique<TimeSystem>(seasons);
}

}  // namespace core
