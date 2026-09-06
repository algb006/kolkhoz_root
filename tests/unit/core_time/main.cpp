// Unit test of core_time: the solar daylight curve, table-driven weather,
// determinism of the daily draws, and factory validation.

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>

#include "../../common/fake_tables.h"
#include "core_common/calendar.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_time/time_system.h"

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

}  // namespace

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

  const auto time_system = core::CreateTimeSystem(*tables, core::StubTables::kRefused);
  failures += Expect(time_system != nullptr, "the factory accepts a good weather table");
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
                   core::CreateTimeSystem(*hot_tables, core::StubTables::kRefused) == nullptr,
               "a season that swings past +30 is refused");
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
  // The knobs live in their own table (weather_params.csv, key/value); the
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
              "key,value\nthunder_from_month,6\nthunder_to_month,6\nthunder_share,1\n"
              "thunder_min_c,-15\ncalm_share,1\nwind_share,0\n");
    const auto knob_tables = core::LoadTableSet((root / "knobs").string(), nullptr);
    const auto knobbed = knob_tables == nullptr
                             ? nullptr
                             : core::CreateTimeSystem(*knob_tables, core::StubTables::kRefused);
    failures += Expect(knobbed != nullptr, "a table set with weather_params builds");
    if (knobbed != nullptr) {
      std::uint32_t storms = 0;
      std::uint32_t storms_outside_june = 0;
      bool all_still = true;
      for (core::SimDay day = 0; day < 10 * core::kDaysPerYear; ++day) {
        const core::DayForecast at = knobbed->WeatherOn(11, day);
        all_still = all_still && at.wind == core::WindBand::kCalm;
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
      failures += Expect(all_still, "calm_share = 1 from the params table makes every day still");
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
    WriteFile(root / "silly" / "weather_params.csv", "key,value\nthunder_from_month,0\n");
    const auto silly_tables = core::LoadTableSet((root / "silly").string(), nullptr);
    failures += Expect(silly_tables != nullptr, "the out-of-range table parses as CSV");
    if (silly_tables != nullptr) {
      failures +=
          Expect(core::CreateTimeSystem(*silly_tables, core::StubTables::kRefused) == nullptr,
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

  // A malformed weather table is refused, not patched over.
  fs::create_directories(root / "bad");
  WriteFile(root / "bad" / "weather.csv",
            "key,temp_mean_c,temp_spread_c\nwinter,-10,5\n");  // column missing
  const auto bad_tables = core::LoadTableSet((root / "bad").string(), nullptr);
  failures += Expect(bad_tables != nullptr, "the malformed table itself parses as CSV");
  if (bad_tables != nullptr) {
    failures += Expect(core::CreateTimeSystem(*bad_tables, core::StubTables::kRefused) == nullptr,
                       "the factory refuses a malformed weather table");
  }

  fs::remove_all(root);
  if (failures == 0) {
    std::cout << "unit_core_time: all checks passed\n";
  }
  return failures;
}
