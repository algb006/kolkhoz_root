// Simulation run: ten years of the (still empty) world through the standard
// wiring. The stage-2 criterion: the run yields the right number of days,
// seasons and working days (plan §4), and the solar daylight matches the
// design anchors along the way.

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "core_common/calendar.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

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

}  // namespace

int main() {
  namespace fs = std::filesystem;
  int failures = 0;
  constexpr std::uint32_t kYears = 10;
  constexpr std::uint32_t kDays = kYears * core::kDaysPerYear;  // 480
  constexpr std::uint32_t kTicks = kDays * core::kTicksPerDay;  // 11 520

  const fs::path root = fs::temp_directory_path() / "run_ten_years";
  fs::remove_all(root);
  fs::create_directories(root);
  WriteFile(root / "campaign.csv", "key,value\nday_zero_weekday,monday\n");
  WriteFile(root / "weather.csv",
            "key,temp_mean_c,temp_spread_c,precipitation_chance_percent\n"
            "winter,-10,5,35\nspring,5,7,35\nsummer,19,5,25\nautumn,6,7,45\n");
  std::string error;
  const auto tables = core::LoadTableSet(root.string(), &error);
  if (tables == nullptr) {
    std::cout << "FAIL: tables did not load: " << error << '\n';
    return 1;
  }

  core::StandardSimulationConfig config;
  config.tables = tables.get();
  config.world_seed = 1929;
  config.worker_count = 1;
  const auto simulation = core::CreateStandardSimulation(config);
  if (simulation == nullptr) {
    std::cout << "FAIL: the simulation did not assemble\n";
    return 1;
  }

  // Walk ten years, counting per-day facts at each day boundary (hour 0).
  std::array<std::uint32_t, core::kSeasonsPerYear> season_days = {};
  std::uint32_t sundays = 0;
  std::uint32_t year_rollovers = 0;
  float june_daylight = 0.0F;
  float december_daylight = 0.0F;
  std::uint16_t last_year = 1;
  for (std::uint32_t tick = 0; tick < kTicks; ++tick) {
    simulation->AdvanceStep();
    const core::WorldState& state = simulation->CompletedState();
    if (core::HourFromTick(state.calendar.tick) != 0) {
      continue;
    }
    // One visit per day.
    ++season_days[static_cast<std::uint32_t>(state.calendar.season)];
    if (state.calendar.weekday == core::Weekday::kSunday) {
      ++sundays;
    }
    if (state.calendar.date.year != last_year) {
      ++year_rollovers;
      last_year = state.calendar.date.year;
    }
    if (state.calendar.date.month == core::Month::kJune && state.calendar.date.day_in_month == 2) {
      june_daylight = state.weather.daylight_hours;
    }
    if (state.calendar.date.month == core::Month::kDecember &&
        state.calendar.date.day_in_month == 2) {
      december_daylight = state.weather.daylight_hours;
    }
  }

  const core::WorldState& final_state = simulation->CompletedState();
  failures += Expect(final_state.calendar.tick == kTicks, "ten years is 11 520 ticks");
  failures += Expect(final_state.calendar.day == kDays, "ten years is 480 days");
  failures +=
      Expect(final_state.calendar.date.year == kYears + 1, "the run ends at the start of year 11");
  failures += Expect(year_rollovers == kYears, "ten year boundaries were crossed");

  const std::uint32_t total_days =
      season_days[0] + season_days[1] + season_days[2] + season_days[3];
  failures += Expect(total_days == kDays, "every day was visited exactly once");
  bool seasons_even = true;
  for (const std::uint32_t days : season_days) {
    seasons_even = seasons_even && days == kDays / core::kSeasonsPerYear;
  }
  failures += Expect(seasons_even, "each season holds 12 days per year");

  // The week runs independently of months: 480 days from a Monday hold 68
  // Sundays, so the working count is ~41 days a year before holidays
  // (holidays arrive with the event system).
  failures += Expect(sundays == 68, "480 days from a Monday hold 68 Sundays");
  const std::uint32_t working_days = kDays - sundays;
  failures += Expect(working_days / kYears == 41, "about 41 working days per year");

  failures += Expect(june_daylight > 17.2F && june_daylight < 17.8F,
                     "June daylight matches the design anchor");
  failures += Expect(december_daylight > 6.8F && december_daylight < 7.2F,
                     "December daylight matches the design anchor");

  std::cout << "ten_years: " << kTicks << " ticks, " << total_days << " days, " << sundays
            << " Sundays, " << working_days / kYears << " working days/year, June " << june_daylight
            << " h, December " << december_daylight << " h\n";
  fs::remove_all(root);
  if (failures == 0) {
    std::cout << "ten_years: all checks passed\n";
  }
  return failures;
}
