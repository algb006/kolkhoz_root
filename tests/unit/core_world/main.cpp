// Unit test of core_world: the wiring config contract and the O0 world
// genesis STUB. CreateStandardSimulation coverage arrives with task O2.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../../common/fake_tables.h"
#include "core_common/calendar.h"
#include "core_common/herd_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_log/log.h"
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

/// @brief One table held in memory, so a test can hand genesis a cell no

/// @brief Replaces every value of `column` in a CSV with `value`, keeping the
/// file otherwise as it is. Returns false when the column is not there, so a
/// renamed column fails the test instead of silently emptying it.
bool SpoilLivestockCell(const std::filesystem::path& path,
                        std::string_view column,
                        std::string_view value) {
  std::ifstream input(path);
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(input, line)) {
    lines.push_back(line);
  }
  input.close();

  std::size_t header_index = lines.size();
  std::size_t target = 0;
  for (std::size_t index = 0; index < lines.size(); ++index) {
    if (lines[index].empty() || lines[index][0] == '#') {
      continue;
    }
    header_index = index;
    std::size_t position = 0;
    std::size_t field = 0;
    while (position <= lines[index].size()) {
      const std::size_t comma = lines[index].find(',', position);
      const std::size_t end = comma == std::string::npos ? lines[index].size() : comma;
      if (lines[index].substr(position, end - position) == column) {
        target = field;
        break;
      }
      if (comma == std::string::npos) {
        return false;
      }
      position = comma + 1;
      ++field;
    }
    break;
  }
  if (header_index == lines.size()) {
    return false;
  }

  std::ofstream output(path, std::ios::trunc);
  for (std::size_t index = 0; index < lines.size(); ++index) {
    if (index <= header_index || lines[index].empty() || lines[index][0] == '#') {
      output << lines[index] << '\n';
      continue;
    }
    std::string rebuilt;
    std::size_t position = 0;
    std::size_t field = 0;
    while (position <= lines[index].size()) {
      const std::size_t comma = lines[index].find(',', position);
      const std::size_t end = comma == std::string::npos ? lines[index].size() : comma;
      rebuilt +=
          field == target ? std::string(value) : lines[index].substr(position, end - position);
      if (comma == std::string::npos) {
        break;
      }
      rebuilt += ',';
      position = comma + 1;
      ++field;
    }
    output << rebuilt << '\n';
  }
  return true;
}

}  // namespace

int main() {
  namespace fs = std::filesystem;
  int failures = 0;

  // The wiring config must default to the deterministic verification setup:
  // no tables, seed 0, one worker (world.h).
  const core::StandardSimulationConfig config;
  failures += Expect(config.tables == nullptr, "config defaults to no tables");
  failures += Expect(config.world_seed == 0, "config defaults to seed 0");
  failures += Expect(config.worker_count == 1, "config defaults to the verification mode");

  // Genesis STUB: an empty world at day 0, deterministic from the seed.
  const test::FakeTableSet tables;
  const core::WorldState world = core::CreateStartWorld(tables, 12345);
  failures += Expect(world.world_seed == 12345, "genesis stores the seed");
  failures += Expect(world.calendar.tick == 0, "genesis starts at tick 0");
  failures +=
      Expect(world.calendar.weekday == core::Weekday::kMonday, "stub campaign starts on Monday");
  failures += Expect(world.calendar.season == core::Season::kWinter, "day 0 is winter");
  failures += Expect(world.epoch == core::Epoch::kOne, "the campaign starts in Epoch I");
  failures += Expect((world.rng.stream & 1U) == 1U, "the world RNG is seeded (odd stream)");

  const core::WorldState same_seed = core::CreateStartWorld(tables, 12345);
  const core::WorldState other_seed = core::CreateStartWorld(tables, 54321);
  failures += Expect(same_seed.rng.state == world.rng.state, "same seed — same world RNG");
  failures +=
      Expect(other_seed.rng.state != world.rng.state, "different seed — different world RNG");

  // A LAYOUT CELL THAT IS NOT A NUMBER SAYS SO, and a blank one does not.
  //
  // Three facts used to leave LayoutNumber as one silent zero: no such
  // column, a blank cell, and text that is not a number. Only the third is a
  // defect of the table — a blank layout row genuinely does not say — and it
  // is the third that was silent. Phase-2 task A6, second half; the same
  // shape as the missing weather table that swapped a whole climate for a
  // stub one without a word on the same day.
  //
  // MEASURED THROUGH THE LOG, because genesis has no way to refuse: it
  // builds a world and returns it. The log file is the only place the
  // warning is observable from, so it is where the check looks.
  {
    const fs::path log_path = fs::temp_directory_path() / "unit_core_world_layout.log";
    fs::remove(log_path);
    const test::FakeTable layout({"key", "kind", "x_m", "y_m", "area_ha"},
                                 {{"field_bad", "arable", "100", "100", "12kg"},
                                  {"field_blank", "arable", "200", "200", ""}});
    // Genesis stays people-only until unit_types, resources and crops are
    // all there, so the smallest set that reaches the layout pass names all
    // four. They may be empty: the layout row is arable and needs none of
    // their contents.
    const test::FakeTable empty({"key"}, {});
    const test::FakeTableSet one_table({{"start_layout", &layout},
                                        {"unit_types", &empty},
                                        {"resources", &empty},
                                        {"crops", &empty}});
    failures += Expect(core::InitLogFile(log_path.string()), "the test can open a log file");
    core::CreateStartWorld(one_table, 7);
    core::ShutdownLogFile();
    std::ifstream log(log_path);
    const std::string text((std::istreambuf_iterator<char>(log)), std::istreambuf_iterator<char>());
    const bool complained = text.find("a layout cell is not a number") != std::string::npos;
    failures += Expect(complained,
                       "a layout cell holding '12kg' is complained about, not read as "
                       "a zero in silence");
    // And exactly once: the blank cell beside it must not be complained
    // about, or the warning becomes noise and stops being read.
    const std::size_t first = text.find("a layout cell is not a number");
    const bool only_once = first == std::string::npos || text.find("a layout cell is not a number",
                                                                   first + 1) == std::string::npos;
    failures += Expect(only_once, "and the blank cell beside it is not — blank is not a defect");
    fs::remove(log_path);
  }

  // Stage-3 genesis: the designed start, deterministic from the seed.
  failures += Expect(world.residents.rows.size() == 80, "genesis seats 80 residents");
  failures += Expect(world.families.rows.size() == 21, "genesis builds 21 yards");
  failures +=
      Expect(same_seed.residents.rows.size() == world.residents.rows.size() &&
                 same_seed.residents.rows[10].birth_day == world.residents.rows[10].birth_day,
             "genesis is reproducible from the seed");
  std::uint32_t children = 0;
  std::uint32_t old_timers = 0;
  bool links_hold = true;
  for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
    const core::ResidentRow& resident = world.residents.rows[row];
    links_hold = links_hold && core::FindRow(world.families, resident.family) != core::kNoRow;
    // Life speedup 4: one biological year is 12 game days.
    const float age_years = static_cast<float>(-resident.birth_day) / 12.0F;
    children += age_years < 16.0F ? 1 : 0;
    old_timers += age_years >= 60.0F ? 1 : 0;
  }
  failures += Expect(links_hold, "every starting resident's family exists");
  failures += Expect(children >= 25 && children <= 35, "about 30 children at the start");
  failures += Expect(old_timers >= 8 && old_timers <= 14, "about 11 old-timers at the start");

  // The stage-1 criterion: the empty world ticks 10 000 steps, and the
  // result is identical with one worker and with many.
  constexpr std::uint32_t kCriterionSteps = 10000;
  core::StandardSimulationConfig config_single;
  config_single.tables = &tables;
  config_single.world_seed = 7;
  config_single.worker_count = 1;
  // THIS TEST IS THE LEGITIMATE STUB CASE AND NOW SAYS SO: its table set is
  // a fake with nothing in it, and the criterion it measures — one worker
  // equals many, bit for bit — is about the engine and not about the
  // balance. Since 2026-09-05 the silence would be a refusal instead
  // (core_tables/stub_tables.h), which is the point: a caller that has not
  // thought about its tables cannot be served another world quietly.
  config_single.stub_tables = core::StubTables::kAllowed;
  const auto single = core::CreateStandardSimulation(config_single);
  failures += Expect(single != nullptr, "a caller that allows the stub tables gets a simulation");
  {
    // And the same config WITHOUT that word is refused — the guard, and the
    // reason this test could segfault when the refusal first appeared.
    core::StandardSimulationConfig unstated = config_single;
    unstated.stub_tables = core::StubTables::kRefused;
    failures += Expect(core::CreateStandardSimulation(unstated) == nullptr,
                       "and one that does not is refused, not served the defaults in silence");
  }
  if (single == nullptr) {
    std::cout << "unit_core_world: FAILED\n";
    return failures;
  }
  for (std::uint32_t step = 0; step < kCriterionSteps; ++step) {
    single->AdvanceStep();
  }
  const core::WorldState& state = single->CompletedState();
  failures += Expect(state.calendar.tick == kCriterionSteps, "10 000 steps advance 10 000 ticks");
  const core::SimDay expected_day = kCriterionSteps / core::kTicksPerDay;
  failures += Expect(state.calendar.day == expected_day, "the day matches the tick count");
  const core::Date expected_date = core::DateFromDay(expected_day);
  failures += Expect(state.calendar.date.year == expected_date.year &&
                         state.calendar.date.month == expected_date.month,
                     "the date matches the calendar arithmetic");
  failures +=
      Expect(state.calendar.weekday == core::WeekdayFromDay(expected_day, core::Weekday::kMonday),
             "the weekday matches the calendar arithmetic");

  core::StandardSimulationConfig config_many = config_single;
  config_many.worker_count = 3;
  const auto many = core::CreateStandardSimulation(config_many);
  for (std::uint32_t step = 0; step < kCriterionSteps; ++step) {
    many->AdvanceStep();
  }
  const core::WorldState& many_state = many->CompletedState();
  failures += Expect(many_state.calendar.tick == state.calendar.tick &&
                         many_state.calendar.day == state.calendar.day &&
                         many_state.rng.state == state.rng.state &&
                         many_state.rng.stream == state.rng.stream &&
                         many_state.world_seed == state.world_seed,
                     "one worker and three workers agree after 10 000 steps");
  failures +=
      Expect(many_state.residents.rows.size() == state.residents.rows.size() &&
                 many_state.residents.next_id_value == state.residents.next_id_value &&
                 many_state.families.rows.size() == state.families.rows.size() &&
                 many_state.families.rows[0].satisfaction == state.families.rows[0].satisfaction,
             "the population and its metrics agree across worker counts");

  // The start's old houses begin PART WORN, and the band is the table's —
  // not a constant in genesis (task A5). Checked by moving the band: if the
  // knobs were dead, every house would come out in 45..60 regardless.
  {
    const fs::path banded = fs::temp_directory_path() / "unit_core_world_wear_band";
    fs::remove_all(banded);
    fs::copy(fs::path(KOLKHOZ_TABLES_DIR), banded, fs::copy_options::recursive);
    // REWRITTEN, not appended: the shipped table already names the band, and
    // a second row with the same key is a duplicate the loader refuses —
    // rightly, and it caught this test doing it.
    {
      std::ifstream source(banded / "construction.csv");
      std::string knobs;
      std::string knob_line;
      while (std::getline(source, knob_line)) {
        if (knob_line.rfind("old_house_wear_", 0) == 0) {
          continue;
        }
        knobs += knob_line + "\n";
      }
      source.close();
      std::ofstream(banded / "construction.csv", std::ios::trunc)
          << knobs << "old_house_wear_min,10\nold_house_wear_max,12\n";
    }
    std::string band_error;
    const auto banded_tables = core::LoadTableSet(banded.string(), &band_error);
    failures += Expect(banded_tables != nullptr, "the re-banded table set loads");
    if (banded_tables != nullptr) {
      const core::WorldState banded_world = core::CreateStartWorld(*banded_tables, 4242);
      const core::UnitTypeId old_house{static_cast<std::uint16_t>(
          banded_tables->FindTable("unit_types")->FindRowByKey("old_house"))};
      std::uint32_t houses = 0;
      bool inside_band = true;
      bool all_equal = true;
      float first = -1.0F;
      for (const core::UnitRow& unit : banded_world.units.rows) {
        if (unit.type.value != old_house.value || unit.level == 0) {
          continue;
        }
        ++houses;
        inside_band = inside_band && unit.wear >= 10.0F && unit.wear <= 12.0F;
        first = first < 0.0F ? unit.wear : first;
        all_equal = all_equal && unit.wear == first;
      }
      failures += Expect(houses > 1, "the start has old houses to wear");
      failures += Expect(inside_band, "and their wear comes from the table's band, not from code");
      failures += Expect(!all_equal,
                         "each is drawn separately: twenty-one roofs must not fall in one night");
    }
    fs::remove_all(banded);
  }

  // The table-value debt (phase-2 task A6), tested where it bites. The
  // loader now refuses inf and nan at the door (tables.h), so what can still
  // reach a reader is a FINITE absurdity — and genesis casts livestock ages
  // to integers. Genesis only reaches the herds with the whole table set
  // present, so the shipped tables are copied and one cell is spoiled.
  const fs::path spoiled = fs::temp_directory_path() / "unit_core_world_tables";
  fs::remove_all(spoiled);
  fs::copy(fs::path(KOLKHOZ_TABLES_DIR), spoiled, fs::copy_options::recursive);
  failures += Expect(SpoilLivestockCell(spoiled / "livestock.csv", "life_game_years_max", "1e30"),
                     "the livestock cell to spoil was found");
  std::string spoil_error;
  const auto spoiled_tables = core::LoadTableSet(spoiled.string(), &spoil_error);
  failures += Expect(spoiled_tables != nullptr, "the spoiled table set still loads");
  if (spoiled_tables != nullptr) {
    const core::WorldState wild = core::CreateStartWorld(*spoiled_tables, 12345);
    failures += Expect(!wild.herds.rows.empty(),
                       "the roster reaches the herds — otherwise the check below is vacuous");
    bool ages_are_sane = true;
    for (const core::HerdRow& herd : wild.herds.rows) {
      const float age_sum = herd.adult_age_game_years_total;
      ages_are_sane = ages_are_sane && age_sum >= 0.0F && age_sum < 1e6F;
    }
    failures += Expect(ages_are_sane, "an absurd livestock cell never reaches the herd ages");
    const core::WorldState wild_again = core::CreateStartWorld(*spoiled_tables, 12345);
    failures += Expect(wild_again.rng.state == wild.rng.state,
                       "and the fallback keeps genesis deterministic");
  }
  fs::remove_all(spoiled);

  if (failures == 0) {
    std::cout << "unit_core_world: all checks passed\n";
  }
  return failures;
}
