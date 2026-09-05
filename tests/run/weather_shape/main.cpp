// Simulation run: the SHAPE of the weather, measured rather than argued.
//
// The generator gained memory and a sky on 2026-09-04 (boss parcel
// core-cloud-swing). Both changes are made under one invariant, and the
// invariant is the whole reason this run exists:
//
//   MEMORY REDISTRIBUTES WHEN, NOT HOW MUCH, and cloud redistributes the
//   swing between clear days and overcast ones. The season's mean
//   temperature, its share of wet days and its mean swing must come out as
//   they did WITHOUT any of it — otherwise the pretty phenomenon is a silent
//   climate shift, and nobody would connect next winter's firewood to a
//   decision about clouds.
//
// The reference is the shipped table's own memoryless twin, not a constant
// written down here. A constant would have been wrong: the daily mean is
// interpolated between season centres, so the average over a season's days
// is NOT the season's row, and a check against the row fails on correct
// work. The twin has the same climate and no memory, which is exactly the
// difference under test.
//
// It drives the time phase alone — no fields, no people, no ledger. Weather
// is a pure function of (seed, day), so nothing else can inform it, and
// leaving the rest of the step out makes two hundred years cheap.
//
// It is also the instrument for what the core cannot yet feel. Frost has no
// consumer in the simulation today (no heating, no cold death, boss's own
// plan §11 stub), so "did frosts become more common" cannot be asked of the
// population. It is asked of the nights directly.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "../common/run_harness.h"
#include "core_common/calendar.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "core_tables/tables.h"
#include "core_time/time_system.h"

namespace {

/// Long enough that a season's share of wet days is a measurement and not a
/// coin toss: 200 years is 2400 days a season, so a share near 35% carries a
/// standard error under one point.
constexpr std::uint32_t kYears = 200;
constexpr std::uint64_t kDefaultSeed = 20260904;

/// The seed under measurement. A run argument rather than a constant because
/// the first question this instrument was asked was "is that winter share a
/// sampling wobble or a shape in the hash", and the only way to answer it is
/// to move the seed and look again.
std::uint64_t g_seed = kDefaultSeed;

/// The three columns that make the weather remember. Stripping them is how
/// the control is built.
constexpr std::array<std::string_view, 3> kMemoryColumns = {
    "temp_memory", "wet_persistence", "cloud_swing"};

struct SeasonStats {
  double temperature_sum = 0.0;
  double temperature_square_sum = 0.0;
  double swing_sum = 0.0;
  double cloud_sum = 0.0;
  std::uint32_t days = 0;
  std::uint32_t wet_days = 0;
  std::uint32_t frost_nights = 0;

  double MeanTemperature() const { return days == 0 ? 0.0 : temperature_sum / days; }

  double TemperatureDeviation() const {
    if (days == 0) {
      return 0.0;
    }
    const double mean = MeanTemperature();
    const double variance = temperature_square_sum / days - mean * mean;
    return variance <= 0.0 ? 0.0 : std::sqrt(variance);
  }

  double MeanSwing() const { return days == 0 ? 0.0 : swing_sum / days; }

  double MeanCloud() const { return days == 0 ? 0.0 : cloud_sum / days; }

  double WetShare() const { return days == 0 ? 0.0 : 100.0 * wet_days / days; }
};

/// How the lengths of alike-day runs distribute. THE POINT OF MEMORY IS THIS
/// AND NOTHING ELSE: with independent days a run of n has probability p^n,
/// so "drawn-out rains" never happen at any strength of rain.
struct RunLengths {
  std::vector<std::uint32_t> counts;

  void Add(std::uint32_t length) {
    if (length == 0) {
      return;
    }
    if (counts.size() <= length) {
      counts.resize(length + 1, 0);
    }
    ++counts[length];
  }

  std::uint32_t Longest() const {
    for (std::size_t at = counts.size(); at > 0; --at) {
      if (counts[at - 1] != 0) {
        return static_cast<std::uint32_t>(at - 1);
      }
    }
    return 0;
  }

  std::uint32_t AtLeast(std::uint32_t length) const {
    std::uint32_t total = 0;
    for (std::size_t at = length; at < counts.size(); ++at) {
      total += counts[at];
    }
    return total;
  }
};

struct Shape {
  std::array<SeasonStats, core::kSeasonsPerYear> seasons;
  RunLengths wet_runs;
  RunLengths dry_runs;
  RunLengths hot_runs;

  /// HOW OFTEN EACH NAME IS USED, and how often each wind band. Counted per
  /// season as well as in total, because the two rulings this measures are
  /// both SEASONAL: a thunderstorm outside May-August is a defect, and a
  /// blizzard is what winter has instead of a hard frost.
  std::array<std::array<std::uint32_t, core::kWeatherPhenomenonCountValue>, core::kSeasonsPerYear>
      phenomena{};

  std::array<std::array<std::uint32_t, core::kWindBandCountValue>, core::kSeasonsPerYear> winds{};

  /// The coldest day on which a blizzard was named, and the coldest day
  /// there was at all. The gap between them is the ruling.
  double coldest_blizzard = 1000.0;

  double coldest_day = 1000.0;

  /// Squalls seen, and squalls seen outside a thunderstorm. The second must
  /// be zero: a squall is a part of a storm and of nothing else.
  std::uint32_t squalls = 0;

  std::uint32_t squalls_without_a_storm = 0;

  /// SNOW AND A STRONG WIND THAT WAS STILL NOT A BLIZZARD — which is exactly
  /// the set the cold rule refused, since a weaker wind is the only other
  /// way to be a snowfall. Counted as a SHARE and not as a temperature on
  /// boss's condition (2026-09-05), and the reason is that the measurement
  /// ages and the assertion does not: the shipped climate has both memory
  /// knobs at zero, and the day somebody turns them on the distribution
  /// moves while the threshold stays. A threshold that refuses NOBODY is a
  /// comment, not a rule (architecture §8аб), and only a share can say so
  /// years from now.
  std::uint32_t snowfall_denied_by_cold = 0;

  std::uint32_t snowy_days = 0;

  /// WET DAYS INSIDE THE STORM WINDOW, split by what refused them a storm.
  /// Asked because the cold ceiling on a blizzard turned out to be a rule
  /// that could not fire, and `thunder_min_c` is a threshold of exactly the
  /// same shape: if the window is never cold enough to refuse anybody, the
  /// number is a comment (architecture §8аб) and belongs in no table.
  std::uint32_t wet_in_storm_window = 0;

  std::uint32_t refused_by_cold = 0;
};

const char* SeasonName(std::size_t season) {
  constexpr std::array<const char*, core::kSeasonsPerYear> kNames = {
      "winter", "spring", "summer", "autumn"};
  return kNames[season];
}

/// @brief Copies tables/weather.csv without the memory columns.
/// @return false if the source is not where the run expects it.
bool WriteMemorylessWeather(const std::filesystem::path& into) {
  std::ifstream source("tables/weather.csv");
  if (!source) {
    return false;
  }
  std::ofstream sink(into, std::ios::binary);
  std::string line;
  std::vector<std::size_t> drop;
  bool header_seen = false;
  while (std::getline(source, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty() || line[0] == '#') {
      sink << line << '\n';
      continue;
    }
    std::vector<std::string> cells;
    std::stringstream fields(line);
    std::string cell;
    while (std::getline(fields, cell, ',')) {
      cells.push_back(cell);
    }
    if (!header_seen) {
      for (std::size_t at = 0; at < cells.size(); ++at) {
        if (std::find(kMemoryColumns.begin(), kMemoryColumns.end(), cells[at]) !=
            kMemoryColumns.end()) {
          drop.push_back(at);
        }
      }
      header_seen = true;
    }
    bool first = true;
    for (std::size_t at = 0; at < cells.size(); ++at) {
      if (std::find(drop.begin(), drop.end(), at) != drop.end()) {
        continue;
      }
      sink << (first ? "" : ",") << cells[at];
      first = false;
    }
    sink << '\n';
  }
  return true;
}

/// @brief Drives the time phase over `kYears` and collects the shape.
/// @return false when the table set or the system refused to build.
std::uint32_t forecast_disagreements = 0;

bool Measure(const std::string& tables_dir, Shape& shape) {
  std::string error;
  const auto tables = core::LoadTableSet(tables_dir, &error);
  if (tables == nullptr) {
    std::cout << "FAIL: " << tables_dir << " did not load (" << error << ")\n";
    return false;
  }
  const auto system = core::CreateTimeSystem(*tables);
  if (system == nullptr) {
    std::cout << "FAIL: the time system did not build over " << tables_dir << '\n';
    return false;
  }

  constexpr float kDroughtAfternoonCelsius = 25.0F;  // tables/farming.csv drought_temp_c
  core::WorldState previous;
  core::WorldState current;
  previous.world_seed = g_seed;
  current.world_seed = g_seed;
  bool previous_wet = false;
  std::uint32_t wet_run = 0;
  std::uint32_t dry_run = 0;
  std::uint32_t hot_run = 0;

  for (std::uint32_t day = 1; day <= kYears * core::kDaysPerYear; ++day) {
    // One tick per day is enough and is the whole point: the weather is a
    // function of the day, and the slot recomputes the same values on every
    // tick of it.
    previous.calendar.tick = static_cast<core::Tick>(day) * core::kTicksPerDay - 1;
    core::RefreshCalendarCaches(previous.calendar);
    current = previous;
    system->TimeAndWeatherPhase().RunSequential(previous, current);

    // THE FORECAST IS THE SAME ARITHMETIC OR IT IS A SECOND WEATHER. The
    // phase wrote today's name and wind into `current`; WeatherOn is what
    // the boundary hands the player three days early. They come from one
    // function (time_system.cpp, WeatherOfDay), and this is the check that
    // says so out loud — a copy of the rule would part from it on the first
    // edit to either, and nobody would notice until a forecast promised a
    // clear day and the storm came.
    const core::DayForecast ahead = system->WeatherOn(g_seed, current.calendar.day);
    if (ahead.phenomenon != current.weather.phenomenon || ahead.wind != current.weather.wind) {
      ++forecast_disagreements;
    }

    const auto season = static_cast<std::size_t>(current.calendar.season);
    SeasonStats& into = shape.seasons[season];
    // THE NAMES, counted per season, because both rulings under test are
    // seasonal ones (camera design §4).
    shape.phenomena[season][static_cast<std::size_t>(current.weather.phenomenon)] += 1U;
    shape.winds[season][static_cast<std::size_t>(current.weather.wind)] += 1U;
    const auto today = static_cast<double>(current.weather.air_temperature_celsius);
    shape.coldest_day = today < shape.coldest_day ? today : shape.coldest_day;
    if (current.weather.phenomenon == core::WeatherPhenomenon::kBlizzard) {
      shape.coldest_blizzard = today < shape.coldest_blizzard ? today : shape.coldest_blizzard;
    }
    // The window is May..August, 0-based months 4..7 — the same months the
    // generator tests, spelled here so the instrument does not read the
    // table it is measuring.
    const std::uint32_t month_now =
        (current.calendar.day % core::kDaysPerYear) / core::kDaysPerMonth % core::kMonthsPerYear;
    if (current.weather.precipitation == core::Precipitation::kRain && month_now >= 4U &&
        month_now <= 7U) {
      ++shape.wet_in_storm_window;
      // Not a storm, and warm enough to have been one: then it was the draw
      // that refused it, not the cold. The complement is what the ceiling
      // actually costs.
      const double afternoon = static_cast<double>(current.weather.air_temperature_celsius) +
                               static_cast<double>(current.weather.temperature_swing_celsius);
      shape.refused_by_cold += afternoon < 15.0 ? 1U : 0U;
    }
    if (current.weather.precipitation == core::Precipitation::kSnow) {
      ++shape.snowy_days;
      shape.snowfall_denied_by_cold +=
          current.weather.phenomenon == core::WeatherPhenomenon::kSnowfall &&
                  current.weather.wind >= core::WindBand::kStrongWind
              ? 1U
              : 0U;
    }
    if (current.weather.wind == core::WindBand::kSquall) {
      ++shape.squalls;
      shape.squalls_without_a_storm +=
          current.weather.phenomenon == core::WeatherPhenomenon::kThunderstorm ? 0U : 1U;
    }
    const auto mean = static_cast<double>(current.weather.air_temperature_celsius);
    const auto swing = static_cast<double>(current.weather.temperature_swing_celsius);
    into.temperature_sum += mean;
    into.temperature_square_sum += mean * mean;
    into.swing_sum += swing;
    into.cloud_sum += static_cast<double>(current.weather.cloud_cover);
    ++into.days;
    const bool wet = current.weather.precipitation != core::Precipitation::kNone;
    into.wet_days += wet ? 1 : 0;
    // THE NIGHT, WHICH IS THE HALF THE SKY DEEPENS. A frost is decided by
    // the minimum, and the minimum is the mean less the swing.
    into.frost_nights += (mean - swing) < 0.0 ? 1 : 0;

    if (day > 1 && wet != previous_wet) {
      (previous_wet ? shape.wet_runs : shape.dry_runs).Add(previous_wet ? wet_run : dry_run);
      wet_run = 0;
      dry_run = 0;
    }
    (wet ? wet_run : dry_run) += 1;
    previous_wet = wet;

    if ((mean + swing) >= static_cast<double>(kDroughtAfternoonCelsius)) {
      ++hot_run;
    } else {
      shape.hot_runs.Add(hot_run);
      hot_run = 0;
    }
  }
  return true;
}

void Print(const char* label, const Shape& shape) {
  std::cout << "weather_shape [" << label << "]\n";
  for (std::size_t season = 0; season < core::kSeasonsPerYear; ++season) {
    const SeasonStats& at = shape.seasons[season];
    std::cout << "  " << SeasonName(season) << ": mean " << at.MeanTemperature() << " +- "
              << at.TemperatureDeviation() << " C, wet " << at.WetShare() << "%, swing "
              << at.MeanSwing() << " C, cloud " << at.MeanCloud() << ", frost nights "
              << at.frost_nights << " of " << at.days << '\n';
  }
  std::cout << "  wet spells: longest " << shape.wet_runs.Longest() << ", 3+ "
            << shape.wet_runs.AtLeast(3) << ", 5+ " << shape.wet_runs.AtLeast(5) << '\n';
  std::cout << "  dry spells: longest " << shape.dry_runs.Longest() << ", 3+ "
            << shape.dry_runs.AtLeast(3) << ", 5+ " << shape.dry_runs.AtLeast(5) << '\n';
  std::cout << "  hot afternoons in a row: longest " << shape.hot_runs.Longest() << ", 3+ "
            << shape.hot_runs.AtLeast(3) << ", 5+ " << shape.hot_runs.AtLeast(5) << '\n';
  constexpr std::array<const char*, core::kWeatherPhenomenonCountValue> kPhenomenonNames = {
      "clear", "fog", "rain", "thunderstorm", "snowfall", "blizzard", "frost", "heat"};
  constexpr std::array<const char*, core::kWindBandCountValue> kWindNames = {
      "calm", "wind", "strong", "squall"};
  for (std::size_t name = 0; name < kPhenomenonNames.size(); ++name) {
    std::cout << "  " << kPhenomenonNames[name] << ":";
    for (std::size_t season = 0; season < core::kSeasonsPerYear; ++season) {
      std::cout << ' ' << SeasonName(season) << ' ' << shape.phenomena[season][name];
    }
    std::cout << '\n';
  }
  std::cout << "  wind:";
  for (std::size_t band = 0; band < kWindNames.size(); ++band) {
    std::uint32_t total = 0;
    for (std::size_t season = 0; season < core::kSeasonsPerYear; ++season) {
      total += shape.winds[season][band];
    }
    std::cout << ' ' << kWindNames[band] << ' ' << total;
  }
  std::cout << '\n';
  std::cout << "  coldest blizzard " << shape.coldest_blizzard << " C, coldest day "
            << shape.coldest_day << " C; squalls " << shape.squalls << ", of them outside a storm "
            << shape.squalls_without_a_storm << '\n';
  std::cout << "  wet days in the storm window " << shape.wet_in_storm_window
            << ", of them too cool to thunder " << shape.refused_by_cold << '\n';
  std::cout << "  snowy days " << shape.snowy_days << ", of them blown but too cold for a blizzard "
            << shape.snowfall_denied_by_cold << " ("
            << (shape.snowy_days == 0 ? 0.0
                                      : 100.0 * shape.snowfall_denied_by_cold / shape.snowy_days)
            << "% of snow)\n";
}

}  // namespace

/// @brief Writes tables/weather.csv with temp_memory set to `memory` in every
/// season, everything else untouched.
///
/// WHY A SWEEP AND NOT A CHOICE. The knob was shipped at zero on measured
/// grounds (69-reconciliation.md §13.11): at 0.3 the connected cold spells
/// close the sowing window in three first years out of twenty, which is the
/// design's own red line. What that measurement never asked is what the
/// SMALLEST memory is that still gives heat a tail — the question a decision
/// needs and a single value cannot answer.
bool WriteWeatherWithMemory(const std::filesystem::path& into, float memory) {
  std::ifstream source("tables/weather.csv");
  if (!source) {
    return false;
  }
  std::ofstream sink(into, std::ios::binary);
  std::string line;
  std::size_t column = 0;
  bool header_seen = false;
  while (std::getline(source, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty() || line[0] == '#') {
      sink << line << '\n';
      continue;
    }
    std::vector<std::string> cells;
    std::stringstream fields(line);
    std::string cell;
    while (std::getline(fields, cell, ',')) {
      cells.push_back(cell);
    }
    if (!header_seen) {
      for (std::size_t at = 0; at < cells.size(); ++at) {
        column = cells[at] == "temp_memory" ? at : column;
      }
      header_seen = true;
    } else if (column < cells.size()) {
      cells[column] = std::to_string(memory);
    }
    for (std::size_t at = 0; at < cells.size(); ++at) {
      sink << (at == 0 ? "" : ",") << cells[at];
    }
    sink << '\n';
  }
  return true;
}

int main(int argc, char** argv) {
  namespace fs = std::filesystem;
  if (argc > 1 && std::string_view(argv[1]).substr(0, 2) != "--") {
    g_seed = std::strtoull(argv[1], nullptr, 10);
  }
  float sweep = -1.0F;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument.starts_with("--memory=")) {
      sweep = std::strtof(std::string(argument.substr(9)).c_str(), nullptr);
    }
  }
  int failures = 0;

  if (sweep >= 0.0F) {
    const fs::path dir = fs::temp_directory_path() / "run_weather_shape_sweep";
    fs::remove_all(dir);
    fs::create_directories(dir);
    if (!WriteWeatherWithMemory(dir / "weather.csv", sweep)) {
      std::cout << "FAIL: tables/weather.csv not found — run from the repo root\n";
      return 1;
    }
    Shape swept;
    if (!Measure(dir.string(), swept)) {
      return 1;
    }
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "weather_shape: " << kYears << " years, seed " << g_seed << ", temp_memory "
              << sweep << '\n';
    Print("temp_memory sweep", swept);
    return 0;
  }

  const fs::path control_dir = fs::temp_directory_path() / "run_weather_shape_control";
  fs::remove_all(control_dir);
  fs::create_directories(control_dir);
  if (!WriteMemorylessWeather(control_dir / "weather.csv")) {
    std::cout << "FAIL: tables/weather.csv not found — run from the repo root\n";
    return 1;
  }

  Shape memoryless;
  Shape shipped;
  if (!Measure(control_dir.string(), memoryless) || !Measure("tables", shipped)) {
    return 1;
  }
  std::cout << std::fixed << std::setprecision(3);
  std::cout << "weather_shape: " << kYears << " years, seed " << g_seed << '\n';
  Print("memoryless control", memoryless);
  Print("shipped tables", shipped);

  // -- THE INVARIANTS, as checks and not as prose -------------------------
  //
  // The tolerances are sampling error, not slack: 2400 days a season at
  // p=0.35 has a standard error of one point, so two and a half points is
  // rather more than two sigma. A systematic drift — which is the entire
  // worry — does not hide inside a sampling error; it walks past it.
  constexpr double kWetSharePoints = 2.5;
  constexpr double kMeanCelsius = 0.4;
  constexpr double kSwingCelsius = 0.15;
  for (std::size_t season = 0; season < core::kSeasonsPerYear; ++season) {
    const SeasonStats& was = memoryless.seasons[season];
    const SeasonStats& now = shipped.seasons[season];
    const std::string name = SeasonName(season);
    failures += run::Expect(std::abs(now.MeanTemperature() - was.MeanTemperature()) <= kMeanCelsius,
                            (name + ": memory did not move the season's mean temperature").c_str());
    failures +=
        run::Expect(std::abs(now.WetShare() - was.WetShare()) <= kWetSharePoints,
                    (name + ": memory did not move the season's share of wet days").c_str());
    // AND THE SWING AVERAGES TO WHAT IT WAS. Clear days buy their extra
    // swing from overcast ones; a mean multiplier below one would make every
    // night milder than the design says, with nothing pointing at the sky.
    failures += run::Expect(
        std::abs(now.MeanSwing() - was.MeanSwing()) <= kSwingCelsius,
        (name + ": the sky redistributed the diurnal swing without changing its mean").c_str());
  }

  failures += run::Expect(forecast_disagreements == 0,
                          "the forecast and the phase are the same weather, day for day");

  // -- the three rulings of 2026-09-05, as checks and not as prose ---------
  //
  // Each of these can FAIL, and that is the point: a guard that cannot fail
  // is indistinguishable from a missing one and worse than a signal that
  // never fired, because it says "all well" every run (architecture §8ц).
  // Every one of the three was verified by damaging the rule it guards.
  {
    std::uint32_t storms_out_of_season = 0;
    for (std::size_t season = 0; season < core::kSeasonsPerYear; ++season) {
      // Winter and autumn hold no month of May..August between them; spring
      // holds May and summer holds the other three, so those two are the
      // only seasons a storm may appear in at all.
      const bool may_storm = season == static_cast<std::size_t>(core::Season::kSpring) ||
                             season == static_cast<std::size_t>(core::Season::kSummer);
      storms_out_of_season += may_storm ? 0U
                                        : shipped.phenomena[season][static_cast<std::size_t>(
                                              core::WeatherPhenomenon::kThunderstorm)];
    }
    failures +=
        run::Expect(storms_out_of_season == 0, "a thunderstorm never happens outside May..August");
    failures += run::Expect(
        shipped.phenomena[static_cast<std::size_t>(core::Season::kSummer)]
                         [static_cast<std::size_t>(core::WeatherPhenomenon::kThunderstorm)] > 0,
        "and inside the window it does happen — the check has something to check");
  }
  failures += run::Expect(shipped.squalls > 0, "squalls happen");
  failures += run::Expect(shipped.squalls_without_a_storm == 0,
                          "and every one of them is inside a thunderstorm");
  // THE COLD RULING, and it needs BOTH halves. That blizzards exist proves
  // nothing; that the coldest day of two hundred years was too cold to be
  // one is the ruling. Damage it and the two numbers meet.
  failures += run::Expect(
      shipped.coldest_blizzard > shipped.coldest_day + 0.5,
      "the coldest days are too cold for a blizzard — the stillest day is the cruellest");
  // AND THE SAME RULING AS A SHARE, which is the half that survives a change
  // of climate. The two numbers above are a MEASUREMENT of today's
  // distribution and they age silently; this one says what the rule must
  // always do — refuse somebody — and it cannot age (boss, 2026-09-05).
  failures += run::Expect(shipped.snowfall_denied_by_cold > 0,
                          "and the cold rule refuses a non-zero share of blown snowy days: a "
                          "threshold outside what the world produces is a comment, not a rule");

  // A stopped generator passes every invariant above — it would report the
  // same day forever, which is why the measure has to prove it measured.
  failures += run::Expect(shipped.wet_runs.Longest() > 0 && shipped.dry_runs.Longest() > 0,
                          "the generator produced both wet and dry days");
  // And the thing the memory exists FOR. The shipped table may legitimately
  // have it switched off — it is off today, and why is written beside the
  // knobs — so the check is stated as the invariant that holds either way:
  // memory never makes spells SHORTER, and when it is on it makes them
  // longer. A future edit that quietly zeroes the knobs is then visible here
  // as a printed line rather than as somebody wondering where the drought
  // went.
  const std::uint32_t long_wet_now = shipped.wet_runs.AtLeast(4);
  const std::uint32_t long_wet_was = memoryless.wet_runs.AtLeast(4);
  if (long_wet_now == long_wet_was) {
    std::cout << "  memory is OFF in the shipped table: the two runs above are the same weather\n";
  } else {
    failures += run::Expect(long_wet_now > long_wet_was,
                            "memory made long wet spells more common, not less");
  }

  std::cout << (failures == 0 ? "weather_shape: all checks passed\n"
                              : "weather_shape: FAILURES ABOVE\n");
  return failures;
}
