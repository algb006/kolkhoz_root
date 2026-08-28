// Unit test of core_time: the solar daylight curve, table-driven weather,
// determinism of the daily draws, and factory validation.

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>

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
            "key,temp_mean_c,temp_spread_c,precipitation_chance_percent\n"
            "winter,-10,5,35\n"
            "spring,5,7,35\n"
            "summer,19,5,25\n"
            "autumn,6,7,45\n");
  std::string error;
  const auto tables = core::LoadTableSet(root.string(), &error);
  if (Expect(tables != nullptr, "the test tables load") != 0) {
    std::cout << error << '\n';
    return 1;
  }

  const auto time_system = core::CreateTimeSystem(*tables);
  failures += Expect(time_system != nullptr, "the factory accepts a good weather table");
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

  // A malformed weather table is refused, not patched over.
  fs::create_directories(root / "bad");
  WriteFile(root / "bad" / "weather.csv",
            "key,temp_mean_c,temp_spread_c\nwinter,-10,5\n");  // column missing
  const auto bad_tables = core::LoadTableSet((root / "bad").string(), nullptr);
  failures += Expect(bad_tables != nullptr, "the malformed table itself parses as CSV");
  if (bad_tables != nullptr) {
    failures += Expect(core::CreateTimeSystem(*bad_tables) == nullptr,
                       "the factory refuses a malformed weather table");
  }

  fs::remove_all(root);
  if (failures == 0) {
    std::cout << "unit_core_time: all checks passed\n";
  }
  return failures;
}
