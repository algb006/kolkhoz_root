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
// Wind and the two phenomena that need a draw of their own. SEPARATE SALTS
// ARE THE WHOLE POINT for the wind: it is meant to be almost unrelated to the
// weather, and sharing a stream with the temperature or the sky would make it
// a function of them (Кожаный босс, 2026-09-05: "a windy sunny summer day").
constexpr std::uint64_t kWindSalt = 0x7435;
constexpr std::uint64_t kThunderSalt = 0x7436;
constexpr std::uint64_t kFogSalt = 0x7437;

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

  // -- wind and the names of days (Кожаный босс, 2026-09-05) ---------------
  //
  // ALL OPTIONAL COLUMNS WITH THE DESIGN'S OWN NUMBERS AS DEFAULTS, on the
  // same terms as the memory knobs above: tables/weather.csv is generated
  // from db/design.db and this module does not edit it, so the numbers stand
  // here until boss adds the columns. ParseWeatherTable READS EVERY ONE OF
  // THEM by name — the promise and the mechanism are checked against each
  // other, and this comment is the place the check was failed once.

  /// Share of days that are still, 0..1. The rest split between kWind and
  /// kStrongWind by `wind_share`.
  float calm_share = 0.35F;

  /// Share of days at kWind. What is left after this and `calm_share` is
  /// kStrongWind, so the three always sum to one by construction.
  float wind_share = 0.5F;

  /// Thunderstorm window, 0-based months, inclusive: MAY TO AUGUST, which is
  /// 4..7. Outside it a thunderstorm cannot happen at all.
  std::uint8_t thunder_from_month = 4;

  std::uint8_t thunder_to_month = 7;

  /// Share of wet days inside the window that thunder rather than just rain.
  float thunder_share = 0.35F;

  /// Afternoon below which a wet day is rain and not a storm.
  float thunder_min_celsius = 15.0F;

  /// A blizzard needs snow, a strong wind, and a temperature NOT below this:
  /// it comes about zero and down to -10..-15, never in a hard frost.
  float blizzard_min_celsius = -10.0F;

  /// A dry night at or below this is named a frost — but only inside the
  /// window below, because every winter night is under zero and a name that
  /// fires on all of them says nothing.
  float frost_night_celsius = 0.0F;

  /// Frost window, 0-based months inclusive: the growing half of the year,
  /// where a frost is the one that kills seedlings.
  std::uint8_t frost_from_month = 3;

  std::uint8_t frost_to_month = 9;

  /// Afternoon at or above which a dry day is named heat. The drought
  /// threshold of tables/farming.csv, kept here as the naming rule's own.
  float heat_afternoon_celsius = 25.0F;

  /// Share of still dry days that fog over.
  float fog_share = 0.2F;
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

/// @brief The 0-based month a day of the year falls in — the unit the
/// phenomenon windows are written in, the same one the crop tables use.
std::uint8_t MonthOfDayOfYear(std::uint32_t day_of_year) {
  return static_cast<std::uint8_t>((day_of_year / kDaysPerMonth) % kMonthsPerYear);
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

  // -- the wind, drawn on its own ------------------------------------------
  //
  // Its own salt and no memory: the design says wind is random and almost
  // unrelated to the weather, so nothing above feeds into it. The band comes
  // out of one uniform draw by season shares, and kSquall is NOT reachable
  // here — a squall belongs to a thunderstorm and is assigned below, after
  // the day has a name.
  const float gust = CounterHashUnitFloat(world_seed, day, 0, kWindSalt);
  weather.wind = gust < season.calm_share                       ? WindBand::kCalm
                 : gust < season.calm_share + season.wind_share ? WindBand::kWind
                                                                : WindBand::kStrongWind;

  // -- and then what the day is CALLED --------------------------------------
  //
  // ONE NAME PER DAY, so the roster is a PRIORITY and not a set: a day can be
  // raining and foggy and above +25 at once, and the presentation shows one
  // icon. The order is the trouble's own — what would make the player act
  // first stands first — and it is fixed, so two runs never disagree about
  // what a day was called.
  const float afternoon = temperature + weather.temperature_swing_celsius;
  const float night = temperature - weather.temperature_swing_celsius;
  const std::uint8_t month = MonthOfDayOfYear(day_of_year);
  if (weather.precipitation == Precipitation::kSnow) {
    // A blizzard is snow AND a strong wind AND not a hard frost. The last is
    // Кожаный босс's ruling and it is physics: a blizzard comes about zero
    // and down to -10..-15, while at -25 the sky is clear and the air stands
    // still. It buys a live signal for nothing — the cruellest cold is the
    // stillest, clearest day — so the two winter troubles look like
    // opposites and neither reads as the other.
    weather.phenomenon =
        weather.wind >= WindBand::kStrongWind && temperature >= season.blizzard_min_celsius
            ? WeatherPhenomenon::kBlizzard
            : WeatherPhenomenon::kSnowfall;
  } else if (weather.precipitation == Precipitation::kRain) {
    // A thunderstorm is rain inside the storm window and warm enough, with a
    // draw of its own for how often. OUTSIDE THE WINDOW IT CANNOT HAPPEN at
    // all — not by climate and not by a quest's order, which is why the
    // window is tested before the draw and not after.
    const bool in_window = month >= season.thunder_from_month && month <= season.thunder_to_month;
    const bool warm = afternoon >= season.thunder_min_celsius;
    const bool struck =
        CounterHashUnitFloat(world_seed, day, 0, kThunderSalt) < season.thunder_share;
    weather.phenomenon =
        in_window && warm && struck ? WeatherPhenomenon::kThunderstorm : WeatherPhenomenon::kRain;
    // The squall is a part of the storm and nothing else's: it is the squall
    // that lays the corn, not the rain. A storm that already blows hard gets
    // it; a still storm stays still.
    if (weather.phenomenon == WeatherPhenomenon::kThunderstorm &&
        weather.wind >= WindBand::kStrongWind) {
      weather.wind = WindBand::kSquall;
    }
  } else if (night <= season.frost_night_celsius && month >= season.frost_from_month &&
             month <= season.frost_to_month) {
    // Frost is named only in the growing half of the year, and that is not a
    // simplification: every winter night is below zero, so a name that fired
    // on all of them would say nothing. What the design wants named is the
    // frost that KILLS SEEDLINGS — the one out of season.
    weather.phenomenon = WeatherPhenomenon::kFrost;
  } else if (afternoon >= season.heat_afternoon_celsius) {
    weather.phenomenon = WeatherPhenomenon::kHeat;
  } else if (weather.wind == WindBand::kCalm &&
             CounterHashUnitFloat(world_seed, day, 0, kFogSalt) < season.fog_share) {
    // Fog needs still air, and that is the one place wind touches the name of
    // the day. It is not a contradiction of "wind is unrelated to weather":
    // the wind is still drawn first and on its own, and the day is named
    // around it rather than the other way about.
    weather.phenomenon = WeatherPhenomenon::kFog;
  } else {
    weather.phenomenon = WeatherPhenomenon::kClear;
  }
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

  DayForecast WeatherOn(std::uint64_t world_seed, SimDay day) const override {
    const WeatherState weather = WeatherOfDay(seasons_, world_seed, day);
    return DayForecast{.phenomenon = weather.phenomenon, .wind = weather.wind};
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
  // The wind and the naming rules (the wind parcel, 2026-09-05), optional on
  // exactly the same terms: a table from before them keeps the defaults of
  // SeasonWeather, which are the design's own numbers.
  //
  // THESE ARE READ AND NOT MERELY DECLARED, and that distinction is the
  // finding this loop exists because of: the struct's comment promised that
  // a column added to weather.csv "wins the moment it appears", while the
  // parser knew none of these names — so the column would have been dropped
  // without a word, in a parser that is otherwise strict about every name it
  // does know. A promise about a mechanism is a description of it, and an
  // undescribed silence is the worse half.
  const std::uint32_t calm_column = table.FindColumn("calm_share");
  const std::uint32_t wind_column = table.FindColumn("wind_share");
  const std::uint32_t thunder_from_column = table.FindColumn("thunder_from_month");
  const std::uint32_t thunder_to_column = table.FindColumn("thunder_to_month");
  const std::uint32_t thunder_share_column = table.FindColumn("thunder_share");
  const std::uint32_t thunder_warm_column = table.FindColumn("thunder_min_c");
  const std::uint32_t blizzard_column = table.FindColumn("blizzard_min_c");
  const std::uint32_t frost_night_column = table.FindColumn("frost_night_c");
  const std::uint32_t frost_from_column = table.FindColumn("frost_from_month");
  const std::uint32_t frost_to_column = table.FindColumn("frost_to_month");
  const std::uint32_t heat_column = table.FindColumn("heat_afternoon_c");
  const std::uint32_t fog_column = table.FindColumn("fog_share");
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
    // A number that may be negative — a temperature — needs its own reader:
    // `knob` bounds 0..top, and a blizzard's ceiling is below zero by nature.
    const auto degrees = [&table, row, &error](std::uint32_t column, const char* name, float* out) {
      if (column == kNoTableColumn) {
        return true;
      }
      const std::optional<float> cell = table.CellReal(row, column);
      if (!cell || !(*cell >= kTemperatureMinCelsius && *cell <= kTemperatureMaxCelsius)) {
        error = std::string("weather: ") + name + " must be a temperature on the -15..+30 scale";
        return false;
      }
      *out = *cell;
      return true;
    };
    // And a month is 0..11 and whole. Out of range is an error and not a
    // clamp, for the reason the memory knobs give: a window quietly widened
    // to the whole year would look like a working rule.
    const auto month = [&table, row, &error](
                           std::uint32_t column, const char* name, std::uint8_t* out) {
      if (column == kNoTableColumn) {
        return true;
      }
      const std::optional<float> cell = table.CellReal(row, column);
      if (!cell || !(*cell >= 0.0F && *cell <= 11.0F)) {
        error = std::string("weather: ") + name + " must be a 0-based month, 0..11";
        return false;
      }
      *out = static_cast<std::uint8_t>(*cell);
      return true;
    };
    SeasonWeather into;
    if (!knob(memory_column, "temp_memory", 0.95F, &into.temperature_memory) ||
        !knob(persistence_column, "wet_persistence", 0.95F, &into.wet_persistence) ||
        !knob(cloud_column, "cloud_swing", 1.0F, &into.cloud_swing) ||
        !knob(calm_column, "calm_share", 1.0F, &into.calm_share) ||
        !knob(wind_column, "wind_share", 1.0F, &into.wind_share) ||
        !knob(thunder_share_column, "thunder_share", 1.0F, &into.thunder_share) ||
        !knob(fog_column, "fog_share", 1.0F, &into.fog_share) ||
        !degrees(thunder_warm_column, "thunder_min_c", &into.thunder_min_celsius) ||
        !degrees(blizzard_column, "blizzard_min_c", &into.blizzard_min_celsius) ||
        !degrees(frost_night_column, "frost_night_c", &into.frost_night_celsius) ||
        !degrees(heat_column, "heat_afternoon_c", &into.heat_afternoon_celsius) ||
        !month(thunder_from_column, "thunder_from_month", &into.thunder_from_month) ||
        !month(thunder_to_column, "thunder_to_month", &into.thunder_to_month) ||
        !month(frost_from_column, "frost_from_month", &into.frost_from_month) ||
        !month(frost_to_column, "frost_to_month", &into.frost_to_month)) {
      return false;
    }
    // THE TWO SHARES TOGETHER DECIDE A THIRD, so they are checked together:
    // what is left after calm and wind is the strong-wind share, and a table
    // whose two sum past one would name a negative one.
    if (into.calm_share + into.wind_share > 1.0F) {
      error = "weather: calm_share + wind_share must not exceed 1 — the rest is the strong wind";
      return false;
    }
    into.temperature_mean_celsius = *mean;
    into.temperature_spread_celsius = *spread;
    into.temperature_amplitude_celsius = amplitude;
    into.precipitation_chance_percent = *chance;
    seasons[season] = into;
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
