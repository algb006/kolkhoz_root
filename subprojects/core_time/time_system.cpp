// Implementation of the core_time boundary (include/core_time/time_system.h).
// Stage 2: the phase advances the clock, refreshes the calendar caches and
// writes the day's weather — daylight from the solar curve
// (core_common/daylight.h),
// the sky, the temperature and the rest from the per-season weather table.
//
// WHAT A DAY IS lives in weather_of_day.cpp since 2026-09-18; this file says
// WHEN it is written (the phase), what the forecast asks, and where the
// numbers come from (the parsers).

#include "core_time/time_system.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core_catalog/table_value.h"
#include "core_common/calendar.h"
#include "core_common/daylight.h"
#include "core_common/emit_event.h"
#include "core_common/world_state.h"
#include "core_log/log.h"
#include "core_tables/required_tables.h"
#include "core_tables/tables.h"
#include "core_time/month_climate.h"
#include "weather_of_day.h"

namespace core {
namespace {

/// The month the leaf falls, 0-based, when world_params.csv does not say.
/// October, which is what the design's prose says today — and it is a DEFAULT
/// and not the rule: the rule is the row, and the row exists so that no code
/// has to hold a month.
constexpr std::uint8_t kDefaultLeafFallMonth = 9;

/// How far the sky's five shares may miss a hundred and still be a hundred:
/// the table is written in whole or half per cent by a person.
constexpr float kShareSumTolerance = 0.5F;

/// WeatherState::cover_since_leaf_fall of `day` from yesterday's word and
/// today's cover. ONE HOME for the time phase and the month's climate door
/// (0.35.18).
///
/// THE RESET IS AN EVENT, NOT A DATE ON A CALENDAR I PICKED. It falls on
/// the first day of the leaf-fall month, and that month arrives as a row
/// (world_params.csv) precisely so that nobody has to guess it here. "This
/// winter" was the first shape asked for and it needed a winter boundary;
/// by this core's own weather the snow can lay in March, in which year the
/// leaf must lie until March — a December window would cut it short and
/// say nothing.
///
/// THE RESET CLEARS YESTERDAY, NOT TODAY. Written first as
/// `!new_leaf_fall && (carried || cover)`, which also threw away a cover
/// lying on the reset day itself: the leaf falls that morning and snow on
/// it that same evening rots it, so the day would have read false and
/// corrected itself only tomorrow. One wrong day a year, in the one field
/// whose whole purpose is telling two days apart.
bool CoverSinceLeafFallAfter(bool carried,
                             std::uint16_t cover_today,
                             SimDay day,
                             std::uint8_t leaf_fall_month) {
  const Date date = DateFromDay(day);
  const bool new_leaf_fall =
      static_cast<std::uint8_t>(date.month) == leaf_fall_month && date.day_in_month == 0;
  return (!new_leaf_fall && carried) || cover_today > 0;
}

/// Phase 1 slot: clock, calendar caches, the day's weather.
class TimeAndWeatherSlot final : public ISequentialPhase {
 public:
  TimeAndWeatherSlot(const SeasonTable& seasons, std::uint8_t leaf_fall_month)
      : seasons_(seasons), leaf_fall_month_(leaf_fall_month) {}

  void RunSequential(const WorldState& previous, WorldState& current) override {
    current.calendar.tick = previous.calendar.tick + 1;
    RefreshCalendarCaches(current.calendar);

    // Weather is a function of the day alone: recomputing it every tick of
    // the same day writes the same values (frozen for the rest of the step).
    // The arithmetic lives in WeatherOfDay because the forecast asks the
    // same question about days ahead — one rule, one home.
    current.weather = WeatherOfDay(seasons_, current.world_seed, current.calendar.day);
    // AND THE ONE THING THE DAY CANNOT DRAW FOR ITSELF: whether snow lies.
    // It carries over from yesterday, because that is what a cover IS — it
    // is on the ground because it fell and has not melted (world_state.h).
    // Read from `previous` and written to `current`, like everything else in
    // this phase, so a re-run of the same tick writes the same number.
    // ONCE A DAY, NOT ONCE A TICK. This phase runs every tick and the cover
    // is a count of DAYS, so advancing it per call made it a count of ticks —
    // twenty-four times its own name. Shipped that way on 2026-09-05 and
    // found on 2026-09-06 by a probe that printed 145 lying days on the
    // world's 48th: `> 0` tests never noticed, because the error is in the
    // MAGNITUDE and every reader so far asked only whether it was zero. The
    // seam publishes this number to the layer as days.
    //
    // Carrying the value unchanged inside a day is what keeps a re-run of the
    // same tick writing the same number (buffer law): the update is a pure
    // function of the day boundary, not of how many times the phase ran.
    const bool first_tick_of_a_day =
        current.calendar.day != previous.calendar.day || previous.calendar.tick == 0;
    current.weather.snow_cover_days =
        first_tick_of_a_day
            ? SnowCoverAfter(seasons_, previous.weather, current.weather, current.calendar.day)
            : previous.weather.snow_cover_days;
    // AND THE WORD THAT SEPARATES THE COUNT'S TWO ZEROS. It rises the first
    // day a cover lies and does not fall when the cover melts — that is the
    // whole point of it: the leaf rotted under the snow, so a thaw brings
    // nothing back (world_state.h). The reset's rule: CoverSinceLeafFallAfter.
    current.weather.cover_since_leaf_fall =
        CoverSinceLeafFallAfter(previous.weather.cover_since_leaf_fall,
                                current.weather.snow_cover_days,
                                current.calendar.day,
                                leaf_fall_month_);
    // РАСПУТИЦА, once a day like the cover: a function of the day, but one
    // that walks the autumn back to 1 September, so it is not re-walked every
    // tick of the same day (world_state.h, WeatherState::mud).
    current.weather.mud = first_tick_of_a_day
                              ? MudOnDay(seasons_, current.world_seed, current.calendar.day)
                              : previous.weather.mud;
    // THE HEAT IS SAID ONCE A DAY, at its first tick, whatever the day is
    // called (boss, parcel 364): the afternoon — the mean plus the day's
    // swing — against +25. A rainy hot day is still a hot day.
    if (first_tick_of_a_day) {
      const SeasonWeather& season =
          SeasonOfDayOfYear(seasons_, current.calendar.day % kDaysPerYear);
      const float afternoon =
          current.weather.air_temperature_celsius + current.weather.temperature_swing_celsius;
      if (afternoon >= season.hot_afternoon_celsius) {
        SimEvent& hot = EmitEvent(current, EventKind::kHotAfternoon, EventSeverity::kRoutine);
        hot.amount = static_cast<std::int64_t>(std::lround(afternoon * 10.0F));
      }
    }
  }

 private:
  SeasonTable seasons_;

  std::uint8_t leaf_fall_month_ = kDefaultLeafFallMonth;
};

/// Campaigns the climate's rain share is counted over: each day of the year
/// is drawn once per campaign seed, 1..kClimateSampleSeeds. At a share near
/// a third, 400 draws put the count within about 2.4 points of the truth
/// (one standard error), and the gathering alarm sums a dozen such days, so
/// its error is smaller still. About twenty thousand draws, once per
/// assembled world.
constexpr std::uint64_t kClimateSampleSeeds = 400;

/// The generator's own rain days, counted: the share of campaigns in which
/// each day of the year is a Precipitation::kRain day (ITimeSystem::
/// ClimateRainDayShares says why it is counted and not derived).
RainDayShares CountRainDays(const SeasonTable& seasons) {
  RainDayShares shares{};
  for (std::uint32_t day = 0; day < kDaysPerYear; ++day) {
    std::uint32_t rainy = 0;
    for (std::uint64_t seed = 1; seed <= kClimateSampleSeeds; ++seed) {
      rainy += WeatherOfDay(seasons, seed, day).precipitation == Precipitation::kRain ? 1U : 0U;
    }
    shares[day] = static_cast<float>(rainy) / static_cast<float>(kClimateSampleSeeds);
  }
  return shares;
}

class TimeSystem final : public ITimeSystem {
 public:
  TimeSystem(const SeasonTable& seasons, std::uint8_t leaf_fall_month)
      : seasons_(seasons),
        rain_day_shares_(CountRainDays(seasons)),
        phase_(seasons, leaf_fall_month) {}

  ISequentialPhase& TimeAndWeatherPhase() override { return phase_; }

  DayForecast WeatherOn(std::uint64_t world_seed, SimDay day) const override {
    const WeatherState weather = WeatherOfDay(seasons_, world_seed, day);
    return DayForecast{.sky = weather.sky, .phenomenon = weather.phenomenon, .wind = weather.wind};
  }

  std::uint32_t GrowingSeasonLastDay() const override {
    // WALKED FORWARD FROM MIDSUMMER, because the mean crosses freezing TWICE
    // in a year and only the autumn crossing is the one being asked about.
    // Starting the walk at day 0 would find the spring thaw and answer with a
    // day in March.
    constexpr auto kMidsummer = static_cast<std::uint32_t>(kSeasonCenterDay[2]);
    for (std::uint32_t day = kMidsummer; day < kDaysPerYear; ++day) {
      if (SeasonalMeanTemperature(seasons_, day) <= 0.0F) {
        return day > 0 ? day - 1U : 0U;
      }
    }
    // A climate whose autumn never reaches freezing: the year itself is the
    // limit. Not a fallback for a broken table — a legitimate warm world, and
    // the answer it deserves.
    return kDaysPerYear - 1U;
  }

  RainDayShares ClimateRainDayShares() const override { return rain_day_shares_; }

 private:
  SeasonTable seasons_;
  RainDayShares rain_day_shares_;
  TimeAndWeatherSlot phase_;
};

/// @brief Parses tables/weather.csv into the season table.
///
/// THE SKY'S SHARES ARE READ HERE since 2026-09-18, and the season's wet
/// share with them: steps 4 + 5. `precipitation_chance_percent`,
/// `wet_persistence` and `cloud_swing` said the same things a second way —
/// the wet share, a wet chain nobody runs, a cloud the step now reads — and
/// are no longer read; the design base drops them.
/// @return false on a malformed table (missing season row or column, cell
/// that does not parse, shares that do not make a hundred).
bool ParseWeatherTable(const ITable& table, SeasonTable& seasons, std::string& error) {
  constexpr std::array<std::string_view, kSeasonsPerYear> kSeasonKeys = {
      "winter", "spring", "summer", "autumn"};
  constexpr std::array<std::string_view, kSkyStepCountValue> kShareColumns = {
      "sky_1_percent", "sky_2_percent", "sky_3_percent", "sky_4_percent", "sky_5_percent"};
  const std::uint32_t mean_column = table.FindColumn("temp_mean_c");
  const std::uint32_t spread_column = table.FindColumn("temp_spread_c");
  // Optional: a table from before the diurnal swing keeps an amplitude of 0.
  const std::uint32_t amplitude_column = table.FindColumn("temp_amplitude_c");
  // Optional on the same terms: a table from before the weather had memory
  // reads as memoryless, which is exactly what it was.
  const std::uint32_t memory_column = table.FindColumn("temp_memory");
  std::array<std::uint32_t, kSkyStepCountValue> share_columns = {};
  std::uint32_t shares_present = 0;
  for (std::size_t index = 0; index < kSkyStepCountValue; ++index) {
    share_columns[index] = table.FindColumn(kShareColumns[index]);
    shares_present += share_columns[index] != kNoTableColumn ? 1U : 0U;
  }
  if (mean_column == kNoTableColumn || spread_column == kNoTableColumn) {
    error = "weather: a required column is missing";
    return false;
  }
  // ALL FIVE OR NONE. Absent, the season keeps boss's table (the defaults);
  // partly present, a share would come from the table and its neighbour from
  // the code, and their sum would be nobody's.
  if (shares_present != 0 && shares_present != kSkyStepCountValue) {
    error = "weather: sky_1_percent..sky_5_percent must be all present or all absent";
    return false;
  }
  const SeasonTable defaults = DefaultSeasonTable();
  for (std::uint32_t season = 0; season < kSeasonsPerYear; ++season) {
    const std::uint32_t row = table.FindRowByKey(kSeasonKeys[season]);
    const std::string name(kSeasonKeys[season]);
    if (row == kNoTableRow) {
      error = "weather: no row for season '" + name + "'";
      return false;
    }
    const std::optional<float> mean = table.CellReal(row, mean_column);
    const std::optional<float> spread = table.CellReal(row, spread_column);
    if (!mean || !spread) {
      error = "weather: a cell of season '" + name + "' is empty or not a number";
      return false;
    }
    float amplitude = 0.0F;
    if (amplitude_column != kNoTableColumn) {
      const std::optional<float> cell = table.CellReal(row, amplitude_column);
      if (!cell) {
        error = "weather: temp_amplitude_c of season '" + name + "' is empty or not a number";
        return false;
      }
      amplitude = *cell;
    }
    // THE BOUNDS ARE THERMOMETER READINGS, not means: the hottest afternoon
    // and the coldest night must fit the scale (camera design §4). The step
    // multiplies the amplitude, but the bound is on the table's own number.
    if (*mean + *spread + amplitude > kTemperatureMaxCelsius ||
        *mean - *spread - amplitude < kTemperatureMinCelsius) {
      error = "weather: season '" + name +
              "' swings past the -15..+30 scale (mean +- spread +- amplitude)";
      return false;
    }
    SeasonWeather into = defaults[season];
    if (memory_column != kNoTableColumn) {
      const std::optional<float> cell = table.CellReal(row, memory_column);
      // A value outside is a table error and not a clamp: a memory of 1 is
      // weather that never changes its mind and would look like a hang.
      if (!cell || !(*cell >= 0.0F && *cell <= 0.95F)) {
        error = "weather: temp_memory of season '" + name + "' must be a number in 0..0.95";
        return false;
      }
      into.temperature_memory = *cell;
    }
    if (shares_present == kSkyStepCountValue) {
      float sum = 0.0F;
      for (std::size_t index = 0; index < kSkyStepCountValue; ++index) {
        const std::optional<float> cell = table.CellReal(row, share_columns[index]);
        if (!cell || !(*cell >= 0.0F && *cell <= 100.0F)) {
          error = "weather: " + std::string(kShareColumns[index]) + " of season '" + name +
                  "' must be a number in 0..100";
          return false;
        }
        into.sky_percent[index] = *cell;
        sum += *cell;
      }
      if (std::fabs(sum - 100.0F) > kShareSumTolerance) {
        error = "weather: the five sky shares of season '" + name + "' make " +
                std::to_string(sum) + " per cent, not a hundred";
        return false;
      }
    }
    into.temperature_mean_celsius = *mean;
    into.temperature_spread_celsius = *spread;
    into.temperature_amplitude_celsius = amplitude;
    seasons[season] = into;
  }
  return true;
}

/// @brief The world_params.csv keys THIS MODULE reads.
///
/// Named here so that the assembly can union it with every other module's
/// and judge the table as a whole; a module cannot judge "the core" because
/// a module is not the core (core_catalog/table_value.h).
constexpr std::array<std::string_view, 1> kTimeWorldParamKeys = {"leaf_fall_month"};

/// @brief Reads world_params.csv: constants of the world that are not weather.
///
/// A SEPARATE FILE AND NOT A ROW IN weather_params.csv, because that file's
/// NAME is its contract: a month of leaf fall put there would make the name a
/// lie.
bool ParseWorldParams(const ITable& table, std::uint8_t& leaf_fall_month, std::string& error) {
  const std::uint32_t value_column = table.FindColumn("value");
  if (value_column == kNoTableColumn) {
    error = "world_params: no 'value' column";
    return false;
  }
  std::string local;
  // HUMAN 1..12 across the seam, as everywhere else, and turned to the
  // calendar's zero base HERE and nowhere else.
  float human = static_cast<float>(leaf_fall_month) + 1.0F;
  if (!OptionalValue(
          table, kTimeWorldParamKeys[0], Range{.low = 1.0F, .high = 12.0F}, human, local)) {
    error = "world_params: " + local;
    return false;
  }
  leaf_fall_month = static_cast<std::uint8_t>(human - 1.0F);
  // The declared-readers check lives in the assembly since 2026-09-06, the
  // day world_params.csv gained a second core reader.
  return true;
}

/// @brief Parses tables/weather_params.csv — the climate's knobs, one per
/// row, `key,value,reader` — into every season of `seasons`.
///
/// A MISSING TABLE OR A MISSING ROW IS NOT AN ERROR: SeasonWeather carries
/// the design's own numbers as defaults. A row that IS there is validated
/// strictly, and a row declared for the core that nobody reads is refused.
///
/// @return false on a malformed value; `error` then says which knob and why.
bool ParseWeatherParams(const ITable& table, SeasonTable& seasons, std::string& error) {
  const std::uint32_t value_column = table.FindColumn("value");
  if (value_column == kNoTableColumn) {
    error = "weather_params: no 'value' column";
    return false;
  }
  // Read once into one season and copy: the knobs name rules of the whole
  // climate, not of a season.
  SeasonWeather knobs = seasons[0];
  std::string local;
  // THE SET OF KEYS THE CORE KNOWS, filled BY THE READERS THEMSELVES rather
  // than written out a second time beside them.
  std::vector<std::string_view> knows;
  const auto degrees = Range{.low = kTemperatureMinCelsius, .high = kTemperatureMaxCelsius};
  const auto human_months = Range{.low = 1.0F, .high = 12.0F};
  // A month crosses this seam as a HUMAN 1..12 and the calendar counts from
  // zero, so the base changes HERE and in no other place.
  const auto month = [&](const char* key, std::uint8_t* out) {
    knows.push_back(key);
    float human = static_cast<float>(*out) + 1.0F;
    if (!OptionalValue(table, key, human_months, human, local)) {
      error = "weather_params: " + local;
      return false;
    }
    *out = static_cast<std::uint8_t>(human - 1.0F);
    return true;
  };
  const auto number = [&](const char* key, Range range, float* out) {
    knows.push_back(key);
    if (!OptionalValue(table, key, range, *out, local)) {
      error = "weather_params: " + local;
      return false;
    }
    return true;
  };
  // The swing's proportions: positive, and no step more than twice the
  // season's mean day — past that the normalisation would push the clear
  // afternoon off the thermometer's scale.
  const auto proportion = Range{.low = 0.1F, .high = 2.0F};
  const auto hours = Range{.low = 1.0F, .high = 24.0F};
  if (!number("calm_share", Range::Unit(), &knobs.calm_share) ||
      !number("wind_share", Range::Unit(), &knobs.wind_share) ||
      !number("fog_share", Range::Unit(), &knobs.fog_share) ||
      !number("frost_night_c", degrees, &knobs.frost_night_celsius) ||
      !number("sultry_afternoon_c", degrees, &knobs.sultry_afternoon_celsius) ||
      !number("snow_melt_c", degrees, &knobs.snow_melt_celsius) ||
      !number("hot_afternoon_c", degrees, &knobs.hot_afternoon_celsius) ||
      !number("still_frost_c", degrees, &knobs.still_frost_celsius) ||
      !number("snow_below_c", degrees, &knobs.snow_below_celsius) ||
      !number("rain_above_c", degrees, &knobs.rain_above_celsius) ||
      !number("heavy_hours_min", hours, &knobs.heavy_hours_min) ||
      !number("heavy_hours_max", hours, &knobs.heavy_hours_max) ||
      !number("sky_swing_1", proportion, &knobs.sky_swing[0]) ||
      !number("sky_swing_2", proportion, &knobs.sky_swing[1]) ||
      !number("sky_swing_3", proportion, &knobs.sky_swing[2]) ||
      !number("sky_swing_4", proportion, &knobs.sky_swing[3]) ||
      !number("sky_swing_5", proportion, &knobs.sky_swing[4]) ||
      !month("thunder_from_month", &knobs.thunder_from_month) ||
      !month("thunder_to_month", &knobs.thunder_to_month) ||
      !month("blizzard_from_month", &knobs.blizzard_from_month) ||
      !month("blizzard_to_month", &knobs.blizzard_to_month) ||
      !month("frost_from_month", &knobs.frost_from_month) ||
      !month("frost_to_month", &knobs.frost_to_month)) {
    return false;
  }
  // RETIRED 2026-09-18, known and not read, until the design base drops the
  // rows and the export follows — then these lines go with them. Each was a
  // rule of the weather before the sky steps: `thunder_share` and
  // `thunder_min_c` chose which warm wet day thundered (now every step 5 in
  // May–August does, the human's word), and `blizzard_min_c` kept a blizzard
  // out of a hard frost at −10 (now the still frost at −12 keeps step 5 out,
  // and inside December–February every cold step 5 is a blizzard).
  knows.emplace_back("thunder_share");
  knows.emplace_back("thunder_min_c");
  knows.emplace_back("blizzard_min_c");
  if (!CheckDeclaredReaders(table, "weather_params", knows, error)) {
    return false;
  }
  // The two shares decide a third between them: what is left after calm and
  // wind is the strong wind, and a pair summing past one would name a
  // negative share.
  if (knobs.calm_share + knobs.wind_share > 1.0F) {
    error = "weather_params: calm_share + wind_share must not exceed 1 — the rest is strong wind";
    return false;
  }
  if (knobs.heavy_hours_min > knobs.heavy_hours_max) {
    error = "weather_params: heavy_hours_min must not exceed heavy_hours_max";
    return false;
  }
  // Rain above, snow below, wet snow between: bounds the wrong way round
  // would leave a band that is both.
  if (knobs.snow_below_celsius > knobs.rain_above_celsius) {
    error = "weather_params: snow_below_c must not exceed rain_above_c";
    return false;
  }
  for (SeasonWeather& season : seasons) {
    season.calm_share = knobs.calm_share;
    season.wind_share = knobs.wind_share;
    season.fog_share = knobs.fog_share;
    season.frost_night_celsius = knobs.frost_night_celsius;
    season.sultry_afternoon_celsius = knobs.sultry_afternoon_celsius;
    season.snow_melt_celsius = knobs.snow_melt_celsius;
    season.hot_afternoon_celsius = knobs.hot_afternoon_celsius;
    season.still_frost_celsius = knobs.still_frost_celsius;
    season.snow_below_celsius = knobs.snow_below_celsius;
    season.rain_above_celsius = knobs.rain_above_celsius;
    season.heavy_hours_min = knobs.heavy_hours_min;
    season.heavy_hours_max = knobs.heavy_hours_max;
    season.sky_swing = knobs.sky_swing;
    season.thunder_from_month = knobs.thunder_from_month;
    season.thunder_to_month = knobs.thunder_to_month;
    season.blizzard_from_month = knobs.blizzard_from_month;
    season.blizzard_to_month = knobs.blizzard_to_month;
    season.frost_from_month = knobs.frost_from_month;
    season.frost_to_month = knobs.frost_to_month;
  }
  return true;
}

}  // namespace

std::span<const std::string_view> TimeWorldParamKeys() {
  return kTimeWorldParamKeys;
}

namespace {

/// The season table out of the set: the stub seasons, then `weather` and
/// `weather_params` over them where present. ONE READING for the time phase
/// and the month's climate door, so the two cannot read two climates.
bool ReadSeasonTable(const ITableSet& tables, SeasonTable& seasons, std::string& error) {
  seasons = DefaultSeasonTable();
  if (const ITable* weather = tables.FindTable("weather")) {
    if (!ParseWeatherTable(*weather, seasons, error)) {
      return false;
    }
  }
  if (const ITable* params = tables.FindTable("weather_params")) {
    if (!ParseWeatherParams(*params, seasons, error)) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool MonthClimateOfTables(const ITableSet& tables,
                          Month month,
                          MonthClimate& climate,
                          std::string& error) {
  SeasonTable seasons;
  if (tables.FindTable("weather") == nullptr) {
    error = "month climate: the table set has no weather table";
    return false;
  }
  if (!ReadSeasonTable(tables, seasons, error)) {
    return false;
  }
  const std::uint32_t day_of_year = SecondDayOfMonth(month) % kDaysPerYear;
  const SeasonWeather& season = SeasonOfDayOfYear(seasons, day_of_year);
  climate.mean_celsius = SeasonalMeanTemperature(seasons, day_of_year);
  // THE SEASON'S SWING, NOT A DAY'S: the sky's multiplier averages to one over
  // the season by construction (SkySwingMultiplier), so the mean day and the
  // mean night of the month are the mean plus and minus the season's
  // amplitude (camera design §4, «День = среднее + размах»).
  climate.day_celsius = climate.mean_celsius + season.temperature_amplitude_celsius;
  climate.night_celsius = climate.mean_celsius - season.temperature_amplitude_celsius;

  // THE SNOW AND THE TYPICAL SKY, walked by the time phase's own rules (the
  // cover is history: SnowCoverAfter, CoverSinceLeafFallAfter), one warm-up
  // year and then kMonthClimateYears of this month's second day.
  std::uint8_t leaf_fall_month = kDefaultLeafFallMonth;
  if (const ITable* world = tables.FindTable("world_params")) {
    if (!ParseWorldParams(*world, leaf_fall_month, error)) {
      return false;
    }
  }
  std::vector<std::uint16_t> covers;
  covers.reserve(kMonthClimateYears);
  std::uint32_t covered = 0;
  std::uint32_t since_leaf_fall = 0;
  std::array<std::uint32_t, 256> phenomena{};
  std::array<std::uint32_t, 256> winds{};
  WeatherState yesterday;
  const auto last_day = static_cast<SimDay>((kMonthClimateYears + 1U) * kDaysPerYear);
  for (SimDay day = 0; day < last_day; ++day) {
    WeatherState today = WeatherOfDay(seasons, kMonthClimateSeed, day);
    today.snow_cover_days = SnowCoverAfter(seasons, yesterday, today, day);
    today.cover_since_leaf_fall = CoverSinceLeafFallAfter(
        yesterday.cover_since_leaf_fall, today.snow_cover_days, day, leaf_fall_month);
    if (day >= kDaysPerYear && day % kDaysPerYear == day_of_year) {
      covers.push_back(today.snow_cover_days);
      covered += today.snow_cover_days > 0 ? 1U : 0U;
      since_leaf_fall += today.cover_since_leaf_fall ? 1U : 0U;
      ++phenomena[static_cast<std::uint8_t>(today.phenomenon)];
      ++winds[static_cast<std::uint8_t>(today.wind)];
    }
    yesterday = today;
  }
  const auto years = static_cast<std::uint32_t>(covers.size());
  climate.snow_cover_share =
      years > 0 ? static_cast<float>(covered) / static_cast<float>(years) : 0.0F;
  // THE MEDIAN, the typical day: the lower middle of an even count, so a
  // month covered in exactly half its years reads the uncovered half's 0.
  std::ranges::sort(covers);
  climate.snow_cover_days = years > 0 ? covers[(years - 1U) / 2U] : 0;
  climate.cover_since_leaf_fall = since_leaf_fall * 2U > years;
  const auto modal = [](const std::array<std::uint32_t, 256>& counts) {
    std::uint32_t best = 0;
    for (std::uint32_t value = 1; value < counts.size(); ++value) {
      best = counts[value] > counts[best] ? value : best;  // ties keep the lower value
    }
    return static_cast<std::uint8_t>(best);
  };
  climate.phenomenon = static_cast<WeatherPhenomenon>(modal(phenomena));
  climate.wind = static_cast<WindBand>(modal(winds));
  return true;
}

std::unique_ptr<ITimeSystem> CreateTimeSystem(const ITableSet& tables, StubTables stubs) {
  SeasonTable seasons;
  std::string error;
  // THE STATE IS LEGITIMATE AND THE SILENCE WAS NOT. Building on the stub
  // seasons is what a table-less unit test wants; it is never what a game
  // wants. A caller that means it says so in its own call (StubTables).
  if (!RequireTables(
          tables, stubs, "time", {"weather", "weather_params", "world_params"}, nullptr)) {
    return nullptr;
  }
  if (!ReadSeasonTable(tables, seasons, error)) {
    LogError(error);
    return nullptr;
  }
  // The month of leaf fall, which is not weather and so has a table of its
  // own. It comes here because the flag it clears lives in the weather block
  // and is maintained by this phase.
  std::uint8_t leaf_fall_month = kDefaultLeafFallMonth;
  if (const ITable* world = tables.FindTable("world_params")) {
    if (!ParseWorldParams(*world, leaf_fall_month, error)) {
      LogError(error);
      return nullptr;
    }
  }
  return std::make_unique<TimeSystem>(seasons, leaf_fall_month);
}

}  // namespace core
