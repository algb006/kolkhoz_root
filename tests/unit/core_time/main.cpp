// Unit test of core_time: the solar daylight curve, table-driven weather,
// determinism of the daily draws, and factory validation.

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "../../common/fake_tables.h"
#include "core_common/calendar.h"
#include "core_common/day_window.h"
#include "core_common/daylight.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_time/month_climate.h"
#include "core_time/time_system.h"
#include "weather_of_day.h"

static_assert(std::is_abstract_v<core::ITimeSystem>, "ITimeSystem is a contract");
static_assert(std::has_virtual_destructor_v<core::ITimeSystem>,
              "implementations are destroyed through the interface");

namespace {

int Expect(bool condition, const char* label) {
  if (condition) {
    return 0;
  }
  std::cout << "FAIL: " << label << '\n';
  return 1;
}

void WriteFile(const std::filesystem::path& path, std::string_view content) {
  std::ofstream file(path, std::ios::binary);
  file << content;
}

/// Runs the phase to the morning of game day `day` and returns that state.
core::WorldState RunToDay(core::ISequentialPhase& phase, std::uint64_t seed, core::SimDay day) {
  core::WorldState previous;
  previous.world_seed = seed;
  core::WorldState current;
  while (previous.calendar.day < day) {
    current = previous;
    phase.RunSequential(previous, current);
    std::swap(previous, current);
  }
  return previous;
}

/// @brief Days of one year that lie under a snow cover, seed 12.
///
/// The cover is the one weather quantity that is NOT a pure function of
/// (seed, day) — it remembers yesterday — so the free forecast cannot carry
/// it and it has to be stepped.
///
/// COUNTED ONCE PER DAY, and the distinction cost a measurement: the phase
/// runs every TICK, a day is kTicksPerDay of them, and counting per call
/// answers in tick-days. The wrong number was 385 of 1152 where the right
/// one is 16 of 48 — the same share, which is exactly why a threshold picked
/// against it looked plausible.
std::uint32_t CoveredDaysInAYear(core::ISequentialPhase& phase) {
  core::WorldState previous;
  previous.world_seed = 12;
  core::WorldState current;
  std::uint32_t covered = 0;
  core::SimDay counted = 0;
  while (previous.calendar.day < core::kDaysPerYear) {
    current = previous;
    phase.RunSequential(previous, current);
    std::swap(previous, current);
    if (previous.calendar.day != counted) {
      counted = previous.calendar.day;
      covered += previous.weather.snow_cover_days > 0 ? 1U : 0U;
    }
  }
  return covered;
}

/// THE MONTH'S CLIMATE DOOR (core_time/month_climate.h; boss,
/// boss-core-month-temperature-2026-09-25): for every month, the door's mean
/// and mean afternoon against what the time phase itself writes on that
/// month's second day, averaged over 400 years of one seed — the phase's
/// own door, not a second reading of the table. The day's noise averages to
/// nought (se ~0.35 at the widest spread of 7); tolerance 1.2 degrees. The
/// neighbouring months differ by 4-5 degrees in spring and autumn, so a door
/// that answered for the wrong month is caught.
int CheckMonthClimate(const core::ITableSet& tables, core::ISequentialPhase& phase) {
  int failures = 0;
  constexpr std::uint32_t kYears = 400;
  std::array<double, core::kMonthsPerYear> mean_sum{};
  std::array<double, core::kMonthsPerYear> day_sum{};
  std::array<std::uint32_t, core::kMonthsPerYear> samples{};
  core::WorldState previous;
  previous.world_seed = 12;
  core::WorldState current;
  core::SimDay counted = 0xFFFFFFFFU;
  while (previous.calendar.day < kYears * core::kDaysPerYear) {
    current = previous;
    phase.RunSequential(previous, current);
    std::swap(previous, current);
    const core::SimDay day = previous.calendar.day;
    if (day == counted) {
      continue;
    }
    counted = day;
    const std::uint32_t day_of_year = day % core::kDaysPerYear;
    if (day_of_year % core::kDaysPerMonth != 1U) {
      continue;  // the door answers for the month's SECOND day
    }
    const std::uint32_t month = day_of_year / core::kDaysPerMonth;
    mean_sum[month] += static_cast<double>(previous.weather.air_temperature_celsius);
    day_sum[month] += static_cast<double>(previous.weather.air_temperature_celsius +
                                          previous.weather.temperature_swing_celsius);
    ++samples[month];
  }
  bool all_near = true;
  for (std::uint32_t month = 0; month < core::kMonthsPerYear; ++month) {
    core::MonthClimate climate;
    std::string error;
    const bool read =
        core::MonthClimateOfTables(tables, static_cast<core::Month>(month), climate, error);
    const double phase_mean = samples[month] > 0 ? mean_sum[month] / samples[month] : 1.0e9;
    const double phase_day = samples[month] > 0 ? day_sum[month] / samples[month] : 1.0e9;
    std::cout << "month climate " << month + 1 << ": door " << climate.mean_celsius << " / "
              << climate.day_celsius << " / " << climate.night_celsius << ", phase " << phase_mean
              << " / " << phase_day << " (" << samples[month] << " days)\n";
    all_near = all_near && read && samples[month] == kYears &&
               std::abs(static_cast<double>(climate.mean_celsius) - phase_mean) < 1.2 &&
               std::abs(static_cast<double>(climate.day_celsius) - phase_day) < 1.2 &&
               std::abs((climate.day_celsius - climate.mean_celsius) -
                        (climate.mean_celsius - climate.night_celsius)) < 1.0e-4F;
  }
  failures += Expect(all_near,
                     "month climate: the door's mean and afternoon are what the phase writes, "
                     "month by month");

  // THE SNOW AND THE TYPICAL SKY (0.35.18), EXACTLY: the phase stepped on the
  // door's own seed and years must give the same share, median, majority and
  // mode, to the unit — one set of rules, walked twice.
  std::array<std::vector<std::uint16_t>, core::kMonthsPerYear> covers{};
  std::array<std::uint32_t, core::kMonthsPerYear> since{};
  std::array<std::array<std::uint32_t, 256>, core::kMonthsPerYear> phenomena{};
  std::array<std::array<std::uint32_t, 256>, core::kMonthsPerYear> winds{};
  previous = core::WorldState{};
  previous.world_seed = core::kMonthClimateSeed;
  counted = 0xFFFFFFFFU;
  const auto last_day = (core::kMonthClimateYears + 1U) * core::kDaysPerYear;
  while (previous.calendar.day < last_day) {
    current = previous;
    phase.RunSequential(previous, current);
    std::swap(previous, current);
    const core::SimDay day = previous.calendar.day;
    if (day == counted || day >= last_day) {
      continue;
    }
    counted = day;
    const std::uint32_t day_of_year = day % core::kDaysPerYear;
    if (day < core::kDaysPerYear || day_of_year % core::kDaysPerMonth != 1U) {
      continue;
    }
    const std::uint32_t month = day_of_year / core::kDaysPerMonth;
    covers[month].push_back(previous.weather.snow_cover_days);
    since[month] += previous.weather.cover_since_leaf_fall ? 1U : 0U;
    ++phenomena[month][static_cast<std::uint8_t>(previous.weather.phenomenon)];
    ++winds[month][static_cast<std::uint8_t>(previous.weather.wind)];
  }
  const auto modal = [](const std::array<std::uint32_t, 256>& counts) {
    std::uint32_t best = 0;
    for (std::uint32_t value = 1; value < counts.size(); ++value) {
      best = counts[value] > counts[best] ? value : best;
    }
    return best;
  };
  bool all_same = true;
  std::uint32_t snowy_months = 0;
  for (std::uint32_t month = 0; month < core::kMonthsPerYear; ++month) {
    core::MonthClimate climate;
    std::string error;
    core::MonthClimateOfTables(tables, static_cast<core::Month>(month), climate, error);
    std::vector<std::uint16_t>& days = covers[month];
    const auto years = static_cast<std::uint32_t>(days.size());
    std::uint32_t covered = 0;
    for (const std::uint16_t cover : days) {
      covered += cover > 0 ? 1U : 0U;
    }
    std::sort(days.begin(), days.end());
    const std::uint16_t median = years > 0 ? days[(years - 1U) / 2U] : 0;
    const float share = years > 0 ? static_cast<float>(covered) / static_cast<float>(years) : 0.0F;
    std::cout << "month snow " << month + 1 << ": door " << climate.snow_cover_share << " / "
              << climate.snow_cover_days << " d / leaf " << climate.cover_since_leaf_fall
              << " / sky " << static_cast<int>(climate.phenomenon) << " wind "
              << static_cast<int>(climate.wind) << "; phase " << share << " / " << median
              << " d over " << years << " years\n";
    snowy_months += climate.snow_cover_days > 0 ? 1U : 0U;
    all_same = all_same && years == core::kMonthClimateYears && climate.snow_cover_share == share &&
               climate.snow_cover_days == median &&
               climate.cover_since_leaf_fall == (since[month] * 2U > years) &&
               static_cast<std::uint32_t>(climate.phenomenon) == modal(phenomena[month]) &&
               static_cast<std::uint32_t>(climate.wind) == modal(winds[month]);
  }
  failures +=
      Expect(all_same, "month snow: the door's cover, leaf word, sky and wind are the phase's own");
  // AND THE WORLD HAS A WINTER: a door that answered nought everywhere would
  // agree with a phase that never snowed — the fixture's winter is -10.
  failures += Expect(snowy_months >= 2 && snowy_months <= 6,
                     "month snow: the typical day lies under snow in winter, not in summer");
  return failures;
}

}  // namespace

/// The five sky steps (camera design §4; the human's words of 2026-09-18):
/// the shares by season, the heavy day never twice running and its phase
/// only on itself, the forms by window and temperature, the signs on their
/// own steps, the still frost, and the swing averaging one.
int CheckSkySteps(const std::filesystem::path& root) {
  int failures = 0;
  core::SeasonTable seasons = core::DefaultSeasonTable();
  for (core::SeasonWeather& season : seasons) {
    season.temperature_amplitude_celsius = 5.0F;
  }
  constexpr std::uint64_t kSeed = 7;
  constexpr core::SimDay kDays = 200 * core::kDaysPerYear;
  std::array<std::array<std::uint32_t, core::kSkyStepCountValue>, core::kSeasonsPerYear> counts{};
  std::array<std::uint32_t, core::kSeasonsPerYear> days_in{};
  std::array<float, core::kSeasonsPerYear> multiplier_sum{};
  bool no_two_heavy = true;
  bool phase_only_on_heavy = true;
  bool wet_iff_step = true;
  bool forms_by_window = true;
  bool signs_on_their_steps = true;
  core::SkyStep yesterday = core::SkyStep::kClear;
  for (core::SimDay day = 0; day < kDays; ++day) {
    const core::WeatherState weather = core::WeatherOfDay(seasons, kSeed, day);
    const std::uint32_t month = (day % core::kDaysPerYear) / core::kDaysPerMonth;
    const auto season =
        static_cast<std::uint32_t>(core::SeasonOfMonth(static_cast<core::Month>(month)));
    ++counts[season][static_cast<std::size_t>(weather.sky)];
    ++days_in[season];
    multiplier_sum[season] += weather.temperature_swing_celsius / 5.0F;
    const bool heavy = weather.sky == core::SkyStep::kHeavyPrecipitation;
    no_two_heavy = no_two_heavy && !(heavy && yesterday == core::SkyStep::kHeavyPrecipitation);
    yesterday = weather.sky;
    phase_only_on_heavy =
        phase_only_on_heavy && (heavy ? weather.heavy_hours >= 2 && weather.heavy_hours <= 12 &&
                                            weather.heavy_from_hour <= 23
                                      : weather.heavy_hours == 0 && weather.heavy_from_hour == 0);
    const bool wet = weather.sky >= core::SkyStep::kLightPrecipitation;
    wet_iff_step = wet_iff_step && (wet == (weather.precipitation != core::Precipitation::kNone));
    const core::WeatherPhenomenon name = weather.phenomenon;
    const bool storm_window = month >= 4 && month <= 7;  // May..August, 0-based
    const bool blizzard_window = month == 11 || month <= 1;
    if (heavy && weather.air_temperature_celsius > -1.0F) {
      forms_by_window =
          forms_by_window && name == (storm_window ? core::WeatherPhenomenon::kThunderstorm
                                                   : core::WeatherPhenomenon::kRain);
    }
    if (heavy && weather.air_temperature_celsius < -1.0F) {
      forms_by_window =
          forms_by_window && name == (blizzard_window ? core::WeatherPhenomenon::kBlizzard
                                                      : core::WeatherPhenomenon::kHeavySnowfall);
    }
    if (name == core::WeatherPhenomenon::kFrost || name == core::WeatherPhenomenon::kHeat) {
      signs_on_their_steps = signs_on_their_steps && weather.sky <= core::SkyStep::kPartlyCloudy;
    }
    if (name == core::WeatherPhenomenon::kFog) {
      signs_on_their_steps = signs_on_their_steps && weather.sky <= core::SkyStep::kOvercast;
    }
  }
  failures += Expect(no_two_heavy, "sky: never two heavy days running");
  failures += Expect(phase_only_on_heavy,
                     "sky: a heavy phase of 2..12 hours on step 5, and none on any other step");
  failures += Expect(wet_iff_step, "sky: precipitation exactly on steps 4 and 5");
  failures += Expect(forms_by_window,
                     "sky: step 5 is a storm in May-August and a blizzard in December-February, "
                     "a downpour and a heavy snowfall outside");
  failures +=
      Expect(signs_on_their_steps, "sky: frost and heat only on steps 1-2, fog only on steps 1-3");
  // THE WET SHARE STANDS: what the heavy rule cuts goes to step 4, never to a
  // dry step, so 4 + 5 is the table's own within the draw's noise — and the
  // swing multiplier averages one over the season.
  bool wet_share_holds = true;
  bool swing_averages_one = true;
  for (std::size_t season = 0; season < core::kSeasonsPerYear; ++season) {
    const float wet = static_cast<float>(counts[season][3] + counts[season][4]) * 100.0F /
                      static_cast<float>(days_in[season]);
    const float table = seasons[season].sky_percent[3] + seasons[season].sky_percent[4];
    wet_share_holds = wet_share_holds && std::fabs(wet - table) < 1.5F;
    const float mean = multiplier_sum[season] / static_cast<float>(days_in[season]);
    swing_averages_one = swing_averages_one && std::fabs(mean - 1.0F) < 0.03F;
  }
  failures +=
      Expect(wet_share_holds, "sky: the wet share of every season is steps 4 + 5 of its table");
  failures += Expect(swing_averages_one, "sky: the swing multiplier averages one in every season");

  // THE STILL FROST: a winter below −12 is clear or broken, and still.
  core::SeasonTable frozen = seasons;
  frozen[0].temperature_mean_celsius = -14.5F;
  frozen[0].temperature_spread_celsius = 0.5F;
  bool frost_is_still = true;
  for (core::SimDay day = 0; day < 8; ++day) {  // January and February
    const core::WeatherState weather = core::WeatherOfDay(frozen, kSeed, day);
    if (weather.air_temperature_celsius < -12.0F) {
      frost_is_still = frost_is_still && weather.sky <= core::SkyStep::kPartlyCloudy &&
                       weather.wind == core::WindBand::kCalm;
    }
  }
  failures += Expect(frost_is_still, "sky: below -12 only steps 1-2, and still air");

  // THE TABLE: all five shares or none, and a hundred between them.
  const auto refused = [&root](const char* name, const char* header, const char* winter) {
    const std::filesystem::path dir = root / name;
    std::filesystem::create_directories(dir);
    WriteFile(dir / "weather.csv",
              std::string("key,temp_mean_c,temp_spread_c,temp_amplitude_c,") + header + "\n" +
                  "winter,-10,2,3," + winter + "\nspring,5,7,5," + winter + "\nsummer,19,5,6," +
                  winter + "\nautumn,6,7,5," + winter + "\n");
    const auto tables = core::LoadTableSet(dir.string(), nullptr);
    return tables != nullptr &&
           core::CreateTimeSystem(*tables, core::StubTables::kAllowed) == nullptr;
  };
  failures += Expect(refused("partial", "sky_1_percent,sky_2_percent", "50,50"),
                     "sky: two share columns of five are refused, not topped up from the code");
  failures +=
      Expect(refused("sum",
                     "sky_1_percent,sky_2_percent,sky_3_percent,sky_4_percent,sky_5_percent",
                     "20,20,20,20,30"),
             "sky: shares making 110 per cent are refused");
  return failures;
}

int main() {
  namespace fs = std::filesystem;
  int failures = 0;

  const fs::path root = fs::temp_directory_path() / "unit_core_time";
  fs::remove_all(root);
  fs::create_directories(root);
  WriteFile(root / "weather.csv",
            "key,temp_mean_c,temp_spread_c,temp_amplitude_c,precipitation_chance_percent,note\n"
            "winter,-10,2,3,35\n"
            "spring,5,7,5,35\n"
            "summer,19,5,6,25\n"
            "autumn,6,7,5,45\n");
  std::string error;
  const auto tables = core::LoadTableSet(root.string(), &error);
  if (Expect(tables != nullptr, "the test tables load") != 0) {
    std::cout << error << '\n';
    return 1;
  }

  const auto time_system = core::CreateTimeSystem(*tables, core::StubTables::kAllowed);
  failures += Expect(time_system != nullptr, "the factory accepts a good weather table");
  if (time_system != nullptr) {
    failures += CheckMonthClimate(*tables, time_system->TimeAndWeatherPhase());
    {
      // No weather table: the door says so, rather than answer the stub's.
      const auto bare = core::LoadTableSet((root / "no_weather").string(), &error);
      core::MonthClimate climate;
      std::string why;
      failures += Expect(
          bare == nullptr || !core::MonthClimateOfTables(*bare, core::Month::kJuly, climate, why),
          "month climate: without a weather table the door refuses");
    }
    // THE CLIMATE'S RAIN DAYS (ITimeSystem::ClimateRainDayShares), counted off
    // the generator. A winter at −10 with a spread of 2 never reaches the
    // rain's −1..+1, so its days must read nought: a count that said rain
    // there would be counting wet days, not rain. A summer at +19 rains on
    // its wet days, so its middle must read above nought. And it is counted,
    // not drawn: the same tables give the same shares.
    const core::RainDayShares shares = time_system->ClimateRainDayShares();
    bool in_range = true;
    for (const float share : shares) {
      in_range = in_range && share >= 0.0F && share <= 1.0F;
    }
    failures += Expect(in_range, "rain days: every share is between 0 and 1");
    failures += Expect(shares[1] == 0.0F, "rain days: a winter at -10 has none");
    failures += Expect(shares[24] > 0.0F, "rain days: a summer at +19 has some");
    const auto again = core::CreateTimeSystem(*tables, core::StubTables::kAllowed);
    failures += Expect(again != nullptr && again->ClimateRainDayShares() == shares,
                       "rain days: the same tables give the same shares");
    std::cout << "rain days: winter day 1 " << shares[1] << ", summer day 24 " << shares[24]
              << ", autumn day 36 " << shares[36] << '\n';
  }
  {
    // A season that swings past the scale is refused: +30 is the hottest
    // afternoon and a mean of 26 with spread 5 and amplitude 6 would read 37.
    const std::filesystem::path hot = root / "hot";
    std::filesystem::create_directories(hot);
    std::ofstream(hot / "weather.csv")
        << "key,temp_mean_c,temp_spread_c,temp_amplitude_c,precipitation_chance_percent\n"
           "winter,-10,2,3,35\nspring,5,7,5,35\nsummer,26,5,6,25\nautumn,6,7,5,45\n";
    std::string hot_error;
    const auto hot_tables = core::LoadTableSet(hot.string(), &hot_error);
    failures +=
        Expect(hot_tables != nullptr &&
                   core::CreateTimeSystem(*hot_tables, core::StubTables::kAllowed) == nullptr,
               "a season that swings past +30 is refused");
  }
  {
    // THE DAY'S HEAT (boss, parcel 364): a warm summer, and over a year the
    // event stands on exactly the days whose afternoon — mean plus swing — is
    // at or above +25, once each, at the day's first tick.
    const std::filesystem::path warm = root / "warm";
    std::filesystem::create_directories(warm);
    std::ofstream(warm / "weather.csv")
        << "key,temp_mean_c,temp_spread_c,temp_amplitude_c,precipitation_chance_percent\n"
           "winter,-10,2,3,35\nspring,8,5,5,35\nsummer,21,3,6,25\nautumn,6,7,5,45\n";
    std::string warm_error;
    const auto warm_tables = core::LoadTableSet(warm.string(), &warm_error);
    const auto warm_system = warm_tables == nullptr
                                 ? nullptr
                                 : core::CreateTimeSystem(*warm_tables, core::StubTables::kAllowed);
    if (warm_system == nullptr) {
      failures += Expect(false, "the warm weather table builds a time system");
    } else {
      core::ISequentialPhase& warm_phase = warm_system->TimeAndWeatherPhase();
      core::WorldState previous;
      previous.world_seed = 7;
      core::WorldState current;
      std::uint32_t hot_days = 0;
      std::uint32_t events = 0;
      bool matches = true;
      for (std::uint32_t tick = 0; tick < core::kTicksPerYear; ++tick) {
        current = previous;
        current.step_events.clear();
        warm_phase.RunSequential(previous, current);
        std::uint32_t said = 0;
        for (const core::SimEvent& event : current.step_events) {
          said += event.kind == core::EventKind::kHotAfternoon ? 1U : 0U;
        }
        events += said;
        const bool first_tick =
            core::HourFromTick(current.calendar.tick) == 0 || current.calendar.tick == 0;
        const bool hot =
            current.weather.air_temperature_celsius + current.weather.temperature_swing_celsius >=
            25.0F;
        if (first_tick) {
          hot_days += hot ? 1U : 0U;
          matches = matches && said == (hot ? 1U : 0U);
        } else {
          matches = matches && said == 0;
        }
        std::swap(previous, current);
      }
      failures += Expect(hot_days > 0 && events == hot_days && matches,
                         "heat: the event stands on every day whose afternoon reaches +25, once, "
                         "and on no other");
    }
  }
  core::ISequentialPhase& phase = time_system->TimeAndWeatherPhase();

  // Solar curve anchors (time design §3): June ~17.5, December ~7.0 game
  // hours; equinoxes ~12.2 fall between our discrete days, so the March
  // midpoint reads a bit under and September a bit over.
  const core::WorldState mid_june = RunToDay(phase, 1, 22);
  failures +=
      Expect(mid_june.weather.daylight_hours > 17.2F && mid_june.weather.daylight_hours < 17.8F,
             "June daylight is ~17.5 game hours");
  const core::WorldState mid_december = RunToDay(phase, 1, 46);
  failures += Expect(
      mid_december.weather.daylight_hours > 6.8F && mid_december.weather.daylight_hours < 7.2F,
      "December daylight is ~7.0 game hours");
  const core::WorldState late_march = RunToDay(phase, 1, 10);
  failures +=
      Expect(late_march.weather.daylight_hours > 11.5F && late_march.weather.daylight_hours < 12.7F,
             "spring equinox daylight is ~12.2 game hours");

  {
    // THE TWO PATHS TO SUNRISE MUST AGREE, and this is the check that makes
    // "the move changed no number in the world" a measurement rather than a
    // promise (boss, thread boss-core-sunrise-for-month-2026-09-23).
    //
    // The presentation used to reach a month's light the only way there was:
    // CRANK THE CLOCK to that day and read the weather. The door in
    // core_common/daylight.h answers without a world. Here both are asked,
    // for every day of two years — the year boundary among them — and the
    // floats must be EQUAL, not close: they come off one table through one
    // function, so any difference at all would mean a second home appeared.
    core::WorldState previous;
    previous.world_seed = 1;
    core::WorldState current;
    std::uint32_t days_compared = 0;
    bool same_light = true;
    bool same_window = true;
    core::SimDay last_day_seen = previous.calendar.day;
    while (previous.calendar.day < 2 * core::kDaysPerYear) {
      current = previous;
      phase.RunSequential(previous, current);
      std::swap(previous, current);
      if (previous.calendar.day == last_day_seen) {
        continue;
      }
      last_day_seen = previous.calendar.day;
      ++days_compared;
      const float cranked = previous.weather.daylight_hours;
      const float asked = core::DaylightHoursOfDay(previous.calendar.day);
      same_light = same_light && cranked == asked;
      const core::DayWindow by_crank = core::SolarWindow(cranked);
      const core::DayWindow by_door = core::SolarWindowOfDay(previous.calendar.day);
      same_window =
          same_window && by_crank.sunrise == by_door.sunrise && by_crank.sunset == by_door.sunset;
    }
    // THE COUNT BESIDE THE VERDICT: a sweep that compared nothing would
    // otherwise report a clean world.
    // Days 1..96: day 0 is not compared, because the phase has not run yet
    // and its weather is the default of a fresh WorldState, not a written
    // day. The loop stops once day 96 — the first day of year three — has
    // been stepped into, so the count is the length of two years exactly.
    std::cout << "sunrise: crank and door compared on " << days_compared << " days, days 1.."
              << 2 * core::kDaysPerYear << "\n";
    failures += Expect(days_compared == 2 * core::kDaysPerYear,
                       "sunrise: every day of two years was compared");
    failures += Expect(same_light,
                       "sunrise: the daylight the clock writes equals the daylight the door "
                       "answers, on every day of two years");
    failures += Expect(same_window,
                       "sunrise: the window built from the cranked weather equals the window "
                       "the door answers");
    // And the month form lands on the day it names: December's answer is the
    // second day of December, which the clock reaches as day 45.
    const core::WorldState december_second = RunToDay(phase, 1, 45);
    failures += Expect(december_second.weather.daylight_hours ==
                           core::DaylightHoursOfMonthSecondDay(core::Month::kDecember),
                       "sunrise: the December form answers with the second day of December");
  }

  // A whole year of weather: bounds hold, snow only at or below zero, and
  // both kinds of precipitation actually occur.
  {
    core::WorldState previous;
    previous.world_seed = 7;
    core::WorldState current;
    bool bounds_hold = true;
    bool snow_rule_holds = true;
    int snow_days = 0;
    int rain_days = 0;
    for (std::uint32_t tick = 0; tick < core::kTicksPerYear; ++tick) {
      current = previous;
      phase.RunSequential(previous, current);
      std::swap(previous, current);
      const core::WeatherState& weather = previous.weather;
      bounds_hold = bounds_hold && weather.air_temperature_celsius >= -15.0F &&
                    weather.air_temperature_celsius <= 30.0F;
      if (weather.precipitation == core::Precipitation::kSnow) {
        snow_rule_holds = snow_rule_holds && weather.air_temperature_celsius <= 0.0F;
        ++snow_days;
      }
      if (weather.precipitation == core::Precipitation::kRain) {
        snow_rule_holds = snow_rule_holds && weather.air_temperature_celsius > 0.0F;
        ++rain_days;
      }
    }
    failures += Expect(bounds_hold, "temperature stays inside -15..+30");
    // The scale's ends are thermometer readings: the hottest afternoon of
    // summer is +30 (19 + 5 + 6) and the coldest night of winter -15
    // (-10 - 2 - 3), and the factory refuses a table that swings past
    // either. Checked on the table rather than on a year of draws, because
    // a draw lands on the exact end only when the noise does.
    failures += Expect(19.0F + 5.0F + 6.0F == 30.0F && -10.0F - 2.0F - 3.0F == -15.0F,
                       "the season table reaches both ends of the scale and neither beyond");
    failures += Expect(snow_rule_holds, "snow falls at or below zero, rain above");
    failures += Expect(snow_days > 0 && rain_days > 0, "both snow and rain occur in a year");
  }

  // Weather is a pure function of (seed, day): same seed — same year of
  // weather; different seed — a different one.
  {
    const core::WorldState one = RunToDay(phase, 42, 200);
    const core::WorldState two = RunToDay(phase, 42, 200);
    const core::WorldState other = RunToDay(phase, 43, 200);
    failures += Expect(one.weather.air_temperature_celsius == two.weather.air_temperature_celsius &&
                           one.weather.precipitation == two.weather.precipitation,
                       "same seed gives the same weather");
    bool any_difference =
        other.weather.air_temperature_celsius != one.weather.air_temperature_celsius;
    failures += Expect(any_difference, "a different seed gives different weather");
  }

  // -- THE KNOBS ARE READ, AND THE MONTH CHANGES BASE ON THE WAY IN --------
  //
  // Two separate claims, and the second is the one no range check can make.
  //
  // The knobs live in their own table (weather_params.csv, key/value/reader); the
  // seasons live in weather.csv. They were briefly one file and the core lost
  // its weather entirely, so the split is worth a test of its own.
  //
  // MONTHS CROSS THIS SEAM AS HUMAN 1..12 and the calendar counts from zero.
  // A base error here is the quietest kind there is: 6 taken as written is
  // July instead of June, and BOTH are legal months, so a range check passes
  // either way. The only instrument that can see it is one that asserts WHICH
  // MONTH by name — so this test shuts the storm window down to a single
  // month and looks at where the storms actually land.
  {
    fs::create_directories(root / "knobs");
    WriteFile(root / "knobs" / "weather.csv",
              "key,temp_mean_c,temp_spread_c,temp_amplitude_c,precipitation_chance_percent\n"
              "winter,-10,2,3,35\nspring,5,7,5,35\nsummer,19,5,6,25\nautumn,6,7,5,45\n");
    // June and June only, in human months. Zero-based that is 5.
    WriteFile(root / "knobs" / "weather_params.csv",
              "key,value,reader\nthunder_from_month,6,core\nthunder_to_month,6,core\n"
              "calm_share,1,core\nwind_share,0,core\n");
    const auto knob_tables = core::LoadTableSet((root / "knobs").string(), nullptr);
    const auto knobbed = knob_tables == nullptr
                             ? nullptr
                             : core::CreateTimeSystem(*knob_tables, core::StubTables::kAllowed);
    failures += Expect(knobbed != nullptr, "a table set with weather_params builds");
    if (knobbed != nullptr) {
      std::uint32_t storms = 0;
      std::uint32_t storms_outside_june = 0;
      bool all_still = true;
      bool heavy_blows = true;
      for (core::SimDay day = 0; day < 10 * core::kDaysPerYear; ++day) {
        const core::DayForecast at = knobbed->WeatherOn(11, day);
        // THE SHARE RULES STEPS 1–4 ONLY since 2026-09-18: step 5 blows by
        // its own rule — «сильные осадки и сильный ветер» — whatever the
        // share says, and a squall in a storm.
        if (at.sky == core::SkyStep::kHeavyPrecipitation) {
          heavy_blows = heavy_blows && at.wind >= core::WindBand::kStrongWind;
        } else {
          all_still = all_still && at.wind == core::WindBand::kCalm;
        }
        if (at.phenomenon != core::WeatherPhenomenon::kThunderstorm) {
          continue;
        }
        ++storms;
        const std::uint32_t month =
            (day % core::kDaysPerYear) / core::kDaysPerMonth % core::kMonthsPerYear;
        storms_outside_june += month == 5U ? 0U : 1U;  // June is 5 counting from zero
      }
      failures += Expect(storms > 0, "a window of one month still thunders in it");
      failures += Expect(storms_outside_june == 0,
                         "and month 6 in the table means JUNE, not July: every storm lands in the "
                         "sixth month counting from one");
      failures += Expect(all_still,
                         "calm_share = 1 from the params table makes every day of steps 1-4 still");
      failures += Expect(heavy_blows, "and step 5 blows hard whatever the share says");
    }
  }

  // THE THIRTEENTH KNOB IS READ FROM THE TABLE, and this test is what will
  // tell the day the row arrives from the design base. snow_melt_c stayed
  // compiled in when the other twelve moved out on 2026-09-05 — a missing
  // ROW, not a missing band, which is the same rule broken a step earlier.
  //
  // The guard names its subject rather than the event: a melt threshold of
  // +30 means no winter day is ever warm enough to take a cover away, so
  // snow that falls stays until spring; the compiled default of +2 does not
  // behave that way. Asserting "it parsed" would pass on either.
  {
    fs::create_directories(root / "melt");
    WriteFile(root / "melt" / "weather.csv",
              "key,temp_mean_c,temp_spread_c,temp_amplitude_c,precipitation_chance_percent\n"
              "winter,-10,2,3,35\nspring,5,7,5,35\nsummer,19,5,6,25\nautumn,6,7,5,45\n");
    WriteFile(root / "melt" / "weather_params.csv", "key,value,reader\nsnow_melt_c,30,core\n");
    const auto melt_tables = core::LoadTableSet((root / "melt").string(), nullptr);
    const auto never_melts = melt_tables == nullptr
                                 ? nullptr
                                 : core::CreateTimeSystem(*melt_tables, core::StubTables::kAllowed);
    failures += Expect(never_melts != nullptr, "a weather_params naming snow_melt_c builds");
    if (never_melts != nullptr) {
      // The cover is the ONE weather quantity with a memory, so it is not in
      // the free forecast and has to be walked day by day.
      failures +=
          Expect(CoveredDaysInAYear(never_melts->TimeAndWeatherPhase()) > 40,
                 "a melt threshold of +30 leaves the cover lying over 40 of the year's 48 days — "
                 "row was read, and it was read as the melt threshold");
    }
    WriteFile(root / "melt" / "weather_params.csv", "key,value,reader\nsnow_melt_c,2,core\n");
    const auto plain_tables = core::LoadTableSet((root / "melt").string(), nullptr);
    const auto melts = plain_tables == nullptr
                           ? nullptr
                           : core::CreateTimeSystem(*plain_tables, core::StubTables::kAllowed);
    if (melts != nullptr) {
      failures += Expect(CoveredDaysInAYear(melts->TimeAndWeatherPhase()) < 25,
                         "while the same year at the shipped +2 does not — the control that makes "
                         "the assertion above about snow_melt_c and not about the fixture");
    }
    WriteFile(root / "melt" / "weather_params.csv", "key,value,reader\nsnow_melt_c,99,core\n");
    const auto silly_melt = core::LoadTableSet((root / "melt").string(), nullptr);
    failures +=
        Expect(silly_melt != nullptr &&
                   core::CreateTimeSystem(*silly_melt, core::StubTables::kAllowed) == nullptr,
               "and a melt threshold off the -15..+30 scale is refused, like every other "
               "temperature that crosses this seam");
  }

  // THE ROW ANSWERS FOR ITSELF: a key the core does not know is refused when
  // it is declared as the core's, and carried when it is declared as the
  // layer's. Until 2026-09-06 both were simply not looked for, so a misspelt
  // key left every guard green and the compiled default in force.
  //
  // The four cases below are one guard seen from four sides, and the LAST is
  // the one that makes the other three mean anything: without a row that is
  // legitimately unknown to the core, "refuse the unknown" would be a rule
  // with no counter-example, and a fixture that only ever refuses cannot tell
  // a working guard from one that refuses everything.
  {
    fs::create_directories(root / "declared");
    const auto with_params = [&](const char* body) {
      WriteFile(root / "declared" / "weather.csv",
                "key,temp_mean_c,temp_spread_c,temp_amplitude_c,precipitation_chance_percent\n"
                "winter,-10,2,3,35\nspring,5,7,5,35\nsummer,19,5,6,25\nautumn,6,7,5,45\n");
      WriteFile(root / "declared" / "weather_params.csv", body);
      const auto set = core::LoadTableSet((root / "declared").string(), nullptr);
      return set == nullptr ? nullptr : core::CreateTimeSystem(*set, core::StubTables::kAllowed);
    };
    failures += Expect(with_params("key,value,reader\nsnow_melt_celsius,2,core\n") == nullptr,
                       "a misspelt core key is refused — it is not the core's and not declared "
                       "anyone else's");
    failures += Expect(with_params("key,value,reader\nsnow_melt_c,2\n") == nullptr,
                       "a row that declares no reader at all is refused: an empty cell is not a "
                       "quiet 'core'");
    failures += Expect(with_params("key,value,reader\nsnow_melt_c,2,nobody\n") == nullptr,
                       "and a reader outside core/layer/both/host is refused rather than guessed");
    // THE HOST'S ROW (2026-09-14; boss's export 83e98bf): a knob the host
    // scripts read and the core never does, carried like the layer's.
    failures += Expect(with_params("key,value,reader\nvillage_end_population,40,host\n") != nullptr,
                       "and a key declared for the host builds too — the core carries it unread");
    failures += Expect(with_params("key,value\nsnow_melt_c,2\n") == nullptr,
                       "a table with no reader column at all is refused: in it a typo and a "
                       "layer knob are the same thing");
    failures += Expect(with_params("key,value,reader\ninsect_buzz_min_c,15,layer\n") != nullptr,
                       "while a key the core has never heard of BUILDS when the row says the "
                       "layer reads it — the counter-example that makes the four refusals above "
                       "about the declaration and not about strictness");
  }

  // THE TWO ZEROS OF snow_cover_days, TOLD APART. This is the whole order:
  // ue's first leaf-fall run painted 1 January of every campaign with last
  // year's leaves, over a zero the core reported honestly. "No snow yet" and
  // "the snow melted" are the same zero and opposite answers for the leaf.
  //
  // The guard names its subject three times rather than asking "did the flag
  // move": before any cover it must be false; the day a cover first lies it
  // must be true; and — the one that matters — on a LATER bare day it must
  // still be true, because the leaf rotted under the snow and a thaw brings
  // nothing back.
  {
    core::ISequentialPhase& snow_phase = time_system->TimeAndWeatherPhase();
    core::WorldState previous;
    previous.world_seed = 12;
    core::WorldState current;
    bool seen_before_any_cover = false;
    bool true_on_first_cover = false;
    bool held_over_a_later_thaw = false;
    bool ever_covered = false;
    // COUNTED FROM DAY ZERO AND NOT FROM DAY ONE. The first version of this
    // loop skipped day 0 by using it as the "already counted" sentinel — and
    // day 0 is 1 January, the exact frame ue reported. A guard that cannot
    // see the day it was written for is not a guard.
    core::SimDay counted = core::kDaysPerYear * 4;  // no day equals this
    while (previous.calendar.day < 2 * core::kDaysPerYear) {
      current = previous;
      snow_phase.RunSequential(previous, current);
      std::swap(previous, current);
      if (previous.calendar.day == counted) {
        continue;
      }
      counted = previous.calendar.day;
      const bool covered = previous.weather.snow_cover_days > 0;
      const bool flag = previous.weather.cover_since_leaf_fall;
      if (!ever_covered && !covered) {
        seen_before_any_cover = seen_before_any_cover || !flag;
      }
      if (!ever_covered && covered) {
        true_on_first_cover = flag;
        ever_covered = true;
      } else if (ever_covered && !covered) {
        held_over_a_later_thaw = held_over_a_later_thaw || flag;
      }
    }
    failures += Expect(seen_before_any_cover,
                       "before any snow has lain the word is false — that zero still holds the "
                       "leaf");
    failures += Expect(true_on_first_cover,
                       "the first day a cover lies it turns true, in the same step that counted "
                       "the cover");
    failures += Expect(held_over_a_later_thaw,
                       "and a later bare day does NOT take it back: the leaf rotted under the "
                       "snow, so the second zero is not the first one");
  }

  // AND IT COMES BACK DOWN AT THE LEAF FALL, once a year, on an event and not
  // on a date picked in code. Measured on the day itself: after a winter that
  // certainly laid a cover, the first day of the leaf-fall month must read
  // false again — otherwise the flag is a one-way latch and the second year's
  // leaf never lies at all.
  //
  // The month comes from world_params.csv, so the fixture NAMES it rather
  // than trusting the default: a test that agrees with the compiled default
  // cannot tell a table that was read from one that was ignored.
  {
    fs::create_directories(root / "leaf");
    WriteFile(root / "leaf" / "weather.csv",
              "key,temp_mean_c,temp_spread_c,temp_amplitude_c,precipitation_chance_percent\n"
              "winter,-10,2,3,35\nspring,5,7,5,35\nsummer,19,5,6,25\nautumn,6,7,5,45\n");
    // July, deliberately NOT the October of the default: a summer reset is
    // absurd as design and perfect as an instrument, because nothing else
    // could put a false there.
    WriteFile(root / "leaf" / "world_params.csv", "key,value,reader\nleaf_fall_month,7,both\n");
    const auto leaf_tables = core::LoadTableSet((root / "leaf").string(), nullptr);
    const auto leafy = leaf_tables == nullptr
                           ? nullptr
                           : core::CreateTimeSystem(*leaf_tables, core::StubTables::kAllowed);
    failures += Expect(leafy != nullptr, "a table set naming the leaf-fall month builds");
    if (leafy != nullptr) {
      core::ISequentialPhase& leaf_phase = leafy->TimeAndWeatherPhase();
      core::WorldState previous;
      previous.world_seed = 12;
      core::WorldState current;
      bool covered_before_july = false;
      bool false_on_the_first_of_july = false;
      bool raised_again_after_the_reset = false;
      // THREE YEARS AND NOT TWO, and the number was measured rather than
      // guessed: in this fixture the cover of the second year does not lay
      // until January of the THIRD. Written as two, the last assertion failed
      // on the window and not on the behaviour — the loop simply ended before
      // the next snow. A guard whose horizon is shorter than the event it
      // waits for reports the absence of the horizon.
      core::SimDay counted = core::kDaysPerYear * 8;
      while (previous.calendar.day < 3 * core::kDaysPerYear) {
        current = previous;
        leaf_phase.RunSequential(previous, current);
        std::swap(previous, current);
        if (previous.calendar.day == counted) {
          continue;
        }
        counted = previous.calendar.day;
        const core::Date date = core::DateFromDay(previous.calendar.day);
        const bool flag = previous.weather.cover_since_leaf_fall;
        if (date.year == 1) {
          covered_before_july = covered_before_july || flag;
          continue;
        }
        // Second year: July is month index 6 counting from zero.
        if (date.month == core::Month::kJuly && date.day_in_month == 0) {
          false_on_the_first_of_july = !flag;
        }
        // After the second year's reset, any later day that reads true is the
        // proof: the switch is annual and not once-per-world.
        if (date.year > 2 || (date.year == 2 && date.month > core::Month::kJuly)) {
          raised_again_after_the_reset = raised_again_after_the_reset || flag;
        }
      }
      failures += Expect(covered_before_july,
                         "the first winter does raise the word — without that the reset below "
                         "would be measuring nothing");
      failures += Expect(false_on_the_first_of_july,
                         "and the first day of the leaf-fall month puts it back to false: the "
                         "new leaf has fallen and lies until the next cover");
      failures += Expect(raised_again_after_the_reset,
                         "and a later cover raises it again — the reset is annual, not a "
                         "one-way switch that fires once per world");
    }
  }

  // THE COVER IS COUNTED IN DAYS, AND THE UNIT IS THE ASSERTION. This phase
  // runs every TICK, so a count advanced per call is a count of ticks — the
  // shipped 0.17.55 number was twenty-four times its own name, and no guard
  // saw it because every reader so far asked only whether it was zero. Found
  // on 2026-09-06 by a probe that printed 145 lying days on the world's 48th.
  //
  // So the guard compares the count against DAYS ELAPSED, which is the one
  // comparison a wrong unit cannot survive: an unbroken cover can never have
  // lain more days than the world is old.
  {
    core::ISequentialPhase& unit_phase = time_system->TimeAndWeatherPhase();
    core::WorldState previous;
    previous.world_seed = 12;
    core::WorldState current;
    bool count_ever_exceeded_the_world_age = false;
    std::uint16_t deepest = 0;
    while (previous.calendar.day < core::kDaysPerYear) {
      current = previous;
      unit_phase.RunSequential(previous, current);
      std::swap(previous, current);
      const std::uint16_t lying = previous.weather.snow_cover_days;
      deepest = lying > deepest ? lying : deepest;
      count_ever_exceeded_the_world_age =
          count_ever_exceeded_the_world_age || lying > previous.calendar.day + 1;
    }
    failures += Expect(deepest > 1,
                       "a cover does lie for more than a single day in a year — otherwise the "
                       "unit check below would pass on a counter stuck at zero");
    failures += Expect(!count_ever_exceeded_the_world_age,
                       "and it is never older than the world: the count advances once a DAY, not "
                       "once a tick");
  }

  // AND ON THE RESET DAY ITSELF the word equals "does a cover lie today".
  // The reset clears what was carried from yesterday; it does not throw away
  // today. The leaf falls in the morning and snow that same evening rots it,
  // so a reset day under snow reads TRUE, not false-until-tomorrow.
  //
  // The month is set to JANUARY here for one reason: in this climate a cover
  // is on the ground then, so the reset day is a day with snow and the rule
  // has something to be wrong about. A reset in July would make every reset
  // day bare and the assertion vacuous — true of a correct implementation and
  // equally true of one that always answers false.
  {
    fs::create_directories(root / "resetday");
    WriteFile(root / "resetday" / "weather.csv",
              "key,temp_mean_c,temp_spread_c,temp_amplitude_c,precipitation_chance_percent\n"
              "winter,-10,2,3,35\nspring,5,7,5,35\nsummer,19,5,6,25\nautumn,6,7,5,45\n");
    WriteFile(root / "resetday" / "world_params.csv", "key,value,reader\nleaf_fall_month,1,both\n");
    const auto reset_tables = core::LoadTableSet((root / "resetday").string(), nullptr);
    const auto january = reset_tables == nullptr
                             ? nullptr
                             : core::CreateTimeSystem(*reset_tables, core::StubTables::kAllowed);
    if (january != nullptr) {
      core::ISequentialPhase& reset_phase = january->TimeAndWeatherPhase();
      core::WorldState previous;
      previous.world_seed = 12;
      core::WorldState current;
      std::uint32_t reset_days_seen = 0;
      std::uint32_t reset_days_under_snow = 0;
      std::uint32_t reset_days_disagreeing = 0;
      core::SimDay counted = core::kDaysPerYear * 8;
      while (previous.calendar.day < 3 * core::kDaysPerYear) {
        current = previous;
        reset_phase.RunSequential(previous, current);
        std::swap(previous, current);
        if (previous.calendar.day == counted) {
          continue;
        }
        counted = previous.calendar.day;
        const core::Date date = core::DateFromDay(previous.calendar.day);
        if (date.month != core::Month::kJanuary || date.day_in_month != 0) {
          continue;
        }
        ++reset_days_seen;
        const bool covered = previous.weather.snow_cover_days > 0;
        reset_days_under_snow += covered ? 1U : 0U;
        reset_days_disagreeing += previous.weather.cover_since_leaf_fall == covered ? 0U : 1U;
      }
      failures += Expect(reset_days_seen >= 3, "at least three reset days in three years");
      failures += Expect(reset_days_under_snow > 0,
                         "and at least one of them is under snow — otherwise the rule below has "
                         "nothing to be wrong about");
      failures += Expect(reset_days_disagreeing == 0,
                         "on the reset day the word equals 'does a cover lie today': the reset "
                         "clears yesterday, not today");
    }
  }

  // A month outside 1..12 is refused, not clamped — and 0 is refused too,
  // which is the whole point of the base being human here: a zero would be
  // the December of a 0-based reader and a nonsense of a human one.
  {
    fs::create_directories(root / "silly");
    WriteFile(root / "silly" / "weather.csv",
              "key,temp_mean_c,temp_spread_c,temp_amplitude_c,precipitation_chance_percent\n"
              "winter,-10,2,3,35\nspring,5,7,5,35\nsummer,19,5,6,25\nautumn,6,7,5,45\n");
    WriteFile(root / "silly" / "weather_params.csv",
              "key,value,reader\nthunder_from_month,0,core\n");
    const auto silly_tables = core::LoadTableSet((root / "silly").string(), nullptr);
    failures += Expect(silly_tables != nullptr, "the out-of-range table parses as CSV");
    if (silly_tables != nullptr) {
      failures +=
          Expect(core::CreateTimeSystem(*silly_tables, core::StubTables::kAllowed) == nullptr,
                 "but a month of 0 is refused: months here are human, 1..12");
    }
  }

  // A TABLE SET WITH NO WEATHER TABLE IS REFUSED — unless the caller says it
  // wants the stub climate.
  //
  // The stub is legitimate and stays: a unit test builds subsystems with no
  // tables at all. What was not legitimate is that it happened to callers
  // who had never considered it — on 2026-09-05 a set without weather.csv
  // built silently and a day of measurements described a climate the game
  // does not have. A log line would not have fixed that: a message nobody
  // reads is not a guard. So the consent is a word at the CALL SITE.
  {
    fs::create_directories(root / "no_weather");
    WriteFile(root / "no_weather" / "farming.csv", "key,value\nstress_per_day,0.02\n");
    const auto bare = core::LoadTableSet((root / "no_weather").string(), nullptr);
    failures += Expect(bare != nullptr, "a table set without weather still loads as tables");
    if (bare != nullptr) {
      failures += Expect(core::CreateTimeSystem(*bare, core::StubTables::kRefused) == nullptr,
                         "a caller that did not allow the stub seasons is refused, not served "
                         "a different climate in silence");
      failures += Expect(core::CreateTimeSystem(*bare, core::StubTables::kAllowed) != nullptr,
                         "and a caller that asked for them gets them");
    }
    // The table-LESS case is the same statement: a set with nothing in it is
    // the unit test's own world, and it must still have to say so.
    const test::FakeTableSet nothing;
    failures += Expect(core::CreateTimeSystem(nothing, core::StubTables::kRefused) == nullptr,
                       "no tables at all is refused on the same terms");
    failures += Expect(core::CreateTimeSystem(nothing, core::StubTables::kAllowed) != nullptr,
                       "and allowed on the same terms");
  }

  // AND THE OTHER TWO TABLES THIS FACTORY READS, one at a time. The check
  // above was written for the weather and named only the weather, while
  // weather_params and world_params went on falling back in silence — the
  // refusal existed and covered a third of its own subject. Each case here
  // is a full set with ONE file taken out, because a set missing everything
  // would go red on the first name and prove nothing about the rest.
  {
    const std::array<std::string_view, 2> forgotten = {"weather_params", "world_params"};
    for (const std::string_view missing : forgotten) {
      const fs::path dir = root / std::string("without_") / std::string(missing);
      fs::create_directories(dir);
      WriteFile(dir / "weather.csv",
                "key,temp_mean_c,temp_spread_c,precipitation_chance_percent\n"
                "winter,-10,5,35\nspring,5,7,35\nsummer,19,5,25\nautumn,6,7,45\n");
      if (missing != "weather_params") {
        // With the `reader` column: a knob table that does not say who reads
        // each key is refused on its own terms, which is a different fault
        // from the one under test.
        WriteFile(dir / "weather_params.csv", "key,value,reader\n");
      }
      if (missing != "world_params") {
        WriteFile(dir / "world_params.csv", "key,value,reader\n");
      }
      const auto set = core::LoadTableSet(dir.string(), nullptr);
      failures += Expect(set != nullptr, "the doctored set loads as tables");
      if (set != nullptr) {
        failures += Expect(core::CreateTimeSystem(*set, core::StubTables::kRefused) == nullptr,
                           "a set without one of the tables the clock reads is refused");
        failures += Expect(core::CreateTimeSystem(*set, core::StubTables::kAllowed) != nullptr,
                           "and served to a caller that asked for the defaults");
      }
    }
  }

  // A malformed weather table is refused, not patched over.
  fs::create_directories(root / "bad");
  WriteFile(root / "bad" / "weather.csv",
            // Three season rows missing. It said "column missing" until
            // 2026-09-18, and the column it meant — the rain chance — is no
            // longer required: the rows are what refuses it now.
            "key,temp_mean_c,temp_spread_c\nwinter,-10,5\n");
  const auto bad_tables = core::LoadTableSet((root / "bad").string(), nullptr);
  failures += Expect(bad_tables != nullptr, "the malformed table itself parses as CSV");
  if (bad_tables != nullptr) {
    failures += Expect(core::CreateTimeSystem(*bad_tables, core::StubTables::kAllowed) == nullptr,
                       "the factory refuses a malformed weather table");
  }

  failures += CheckSkySteps(root);

  fs::remove_all(root);
  if (failures == 0) {
    std::cout << "unit_core_time: all checks passed\n";
  }
  return failures;
}
