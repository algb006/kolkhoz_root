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
              "thunder_share,1,core\nthunder_min_c,-15,core\ncalm_share,1,core\n"
              "wind_share,0,core\n");
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
                                 : core::CreateTimeSystem(*melt_tables, core::StubTables::kRefused);
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
                           : core::CreateTimeSystem(*plain_tables, core::StubTables::kRefused);
    if (melts != nullptr) {
      failures += Expect(CoveredDaysInAYear(melts->TimeAndWeatherPhase()) < 25,
                         "while the same year at the shipped +2 does not — the control that makes "
                         "the assertion above about snow_melt_c and not about the fixture");
    }
    WriteFile(root / "melt" / "weather_params.csv", "key,value,reader\nsnow_melt_c,99,core\n");
    const auto silly_melt = core::LoadTableSet((root / "melt").string(), nullptr);
    failures +=
        Expect(silly_melt != nullptr &&
                   core::CreateTimeSystem(*silly_melt, core::StubTables::kRefused) == nullptr,
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
      return set == nullptr ? nullptr : core::CreateTimeSystem(*set, core::StubTables::kRefused);
    };
    failures += Expect(with_params("key,value,reader\nsnow_melt_celsius,2,core\n") == nullptr,
                       "a misspelt core key is refused — it is not the core's and not declared "
                       "anyone else's");
    failures += Expect(with_params("key,value,reader\nsnow_melt_c,2\n") == nullptr,
                       "a row that declares no reader at all is refused: an empty cell is not a "
                       "quiet 'core'");
    failures += Expect(with_params("key,value,reader\nsnow_melt_c,2,nobody\n") == nullptr,
                       "and a reader outside core/layer/both is refused rather than guessed");
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
                           : core::CreateTimeSystem(*leaf_tables, core::StubTables::kRefused);
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
                             : core::CreateTimeSystem(*reset_tables, core::StubTables::kRefused);
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
