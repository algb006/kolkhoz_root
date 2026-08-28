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

/// Per-season weather parameters, parsed from tables/weather.csv.
struct SeasonWeather {
  float temperature_mean_celsius = 5.0F;

  float temperature_spread_celsius = 5.0F;

  float precipitation_chance_percent = 30.0F;
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

/// Phase 1 slot: clock, calendar caches, the day's weather.
class TimeAndWeatherSlot final : public ISequentialPhase {
 public:
  explicit TimeAndWeatherSlot(const SeasonTable& seasons) : seasons_(seasons) {}

  void RunSequential(const WorldState& previous, WorldState& current) override {
    current.calendar.tick = previous.calendar.tick + 1;
    RefreshCalendarCaches(current.calendar);

    // Weather is a function of the day alone: recomputing it every tick of
    // the same day writes the same values (frozen for the rest of the step).
    const SimDay day = current.calendar.day;
    const std::uint32_t day_of_year = day % kDaysPerYear;
    current.weather.daylight_hours = kDaylightGameHours[day_of_year];

    const SeasonWeather& season = seasons_[static_cast<std::uint32_t>(current.calendar.season)];
    const float noise =
        CounterHashUnitFloat(current.world_seed, day, 0, kTemperatureSalt) * 2.0F - 1.0F;
    float temperature =
        SeasonalMeanTemperature(seasons_, day_of_year) + noise * season.temperature_spread_celsius;
    temperature = temperature < kTemperatureMinCelsius ? kTemperatureMinCelsius : temperature;
    temperature = temperature > kTemperatureMaxCelsius ? kTemperatureMaxCelsius : temperature;
    current.weather.air_temperature_celsius = temperature;

    const float precipitation_roll =
        CounterHashUnitFloat(current.world_seed, day, 0, kPrecipitationSalt) * 100.0F;
    if (precipitation_roll < season.precipitation_chance_percent) {
      current.weather.precipitation =
          temperature <= 0.0F ? Precipitation::kSnow : Precipitation::kRain;
    } else {
      current.weather.precipitation = Precipitation::kNone;
    }
  }

 private:
  SeasonTable seasons_;
};

class TimeSystem final : public ITimeSystem {
 public:
  explicit TimeSystem(const SeasonTable& seasons) : phase_(seasons) {}

  ISequentialPhase& TimeAndWeatherPhase() override { return phase_; }

 private:
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
    seasons[season] = {.temperature_mean_celsius = *mean,
                       .temperature_spread_celsius = *spread,
                       .precipitation_chance_percent = *chance};
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
