// Unit test of core_world: the wiring config contract and the O0 world
// genesis STUB. CreateStandardSimulation coverage arrives with task O2.

#include <algorithm>
#include <array>
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
#include "core_construction/construction_system.h"
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

/// @brief Rewrites ONE key's value in a key/value CSV, leaving the rest of
/// the file alone. Returns false when the key is not there, so a renamed row
/// fails the test instead of silently changing nothing.
bool SetKeyValue(const std::filesystem::path& path, std::string_view key, std::string_view value) {
  std::ifstream input(path);
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(input, line)) {
    lines.push_back(line);
  }
  input.close();
  const std::string prefix = std::string(key) + ",";
  bool found = false;
  for (std::string& row : lines) {
    if (row.rfind(prefix, 0) != 0) {
      continue;
    }
    const std::size_t after_key = prefix.size();
    const std::size_t next_comma = row.find(',', after_key);
    const std::string tail =
        next_comma == std::string::npos ? std::string() : row.substr(next_comma);
    row = prefix + std::string(value) + tail;
    found = true;
  }
  if (found) {
    std::ofstream out(path, std::ios::trunc);
    for (const std::string& row : lines) {
      out << row << '\n';
    }
  }
  return found;
}

/// @brief Replaces every value of `column` in a CSV with `value`, keeping the
/// file otherwise as it is. Named for the column and not for the table it
/// was written against: it spoils a cell of any of them, and the livestock
/// name it carried until 2026-09-06 read as a restriction that was never
/// there. Returns false when the column is not there, so a
/// renamed column fails the test instead of silently emptying it.
bool SpoilColumn(const std::filesystem::path& path,
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
  const core::WorldState world =
      core::CreateStartWorld(tables, core::StubTables::kAllowed, nullptr, 12345, nullptr);
  failures += Expect(world.world_seed == 12345, "genesis stores the seed");
  failures += Expect(world.calendar.tick == 0, "genesis starts at tick 0");
  failures +=
      Expect(world.calendar.weekday == core::Weekday::kMonday, "stub campaign starts on Monday");
  failures += Expect(world.calendar.season == core::Season::kWinter, "day 0 is winter");
  failures += Expect(world.epoch == core::Epoch::kOne, "the campaign starts in Epoch I");
  failures += Expect((world.rng.stream & 1U) == 1U, "the world RNG is seeded (odd stream)");

  const core::WorldState same_seed =
      core::CreateStartWorld(tables, core::StubTables::kAllowed, nullptr, 12345, nullptr);
  const core::WorldState other_seed =
      core::CreateStartWorld(tables, core::StubTables::kAllowed, nullptr, 54321, nullptr);
  failures += Expect(same_seed.rng.state == world.rng.state, "same seed — same world RNG");
  failures +=
      Expect(other_seed.rng.state != world.rng.state, "different seed — different world RNG");

  // THE START LAYOUT IS PARSED BEFORE THE WORLD IS BUILT, and the refusal
  // names the row and the column.
  //
  // What stood here until 2026-09-06 measured the WARNING genesis wrote to
  // the log, and said why: "genesis has no way to refuse — it builds a world
  // and returns it". That reason is gone. The parser refuses, CreateStartWorld
  // carries the sentence out, and the check reads the sentence instead of
  // grepping a file.
  //
  // The fixture below also caught the old test in the act: its rows called
  // themselves kind 'arable', a word the shipped table has never used, and
  // the old reader placed them as fields without a murmur. A test that can
  // invent a kind is a test standing on a reader that accepts any.
  {
    const std::vector<std::string> header = {
        "key", "kind", "unit_type", "x_m", "y_m", "area_ha", "is_derelict", "meadow_kind"};
    const std::vector<std::string> good_field = {
        "field_a", "field", "", "100", "200", "7.5", "0", ""};
    // The same header with the two condition columns of the first morning.
    const std::vector<std::string> condition_header = {"key",
                                                       "kind",
                                                       "unit_type",
                                                       "x_m",
                                                       "y_m",
                                                       "area_ha",
                                                       "is_derelict",
                                                       "meadow_kind",
                                                       "start_wear_pct",
                                                       "start_dead"};
    const test::FakeTable empty({"key"}, {});

    // Each case brings its OWN subject, differing from the good row in the
    // one cell its rule is about: a fixture several rules reject cannot say
    // which one did (boss, 2026-09-04).
    struct Case {
      const char* label;
      std::vector<std::string> columns;
      std::vector<std::vector<std::string>> rows;
      const char* names_row;     ///< Substring the message must carry, "" for none.
      const char* names_column;  ///< Column the message must name.
    };

    const std::vector<Case> refused = {
        // A MISSING COLUMN, and deliberately this one. A header without
        // 'kind' or 'key' is refused even with the required-column check
        // taken out — the empty text a missing column reads as fails the
        // per-row rule next door — so a guard built on those two would pass
        // while measuring nothing. A missing 'x_m' has no such second door:
        // an absent column is an absent cell, an absent cell keeps the
        // caller's value, and the whole village is founded at the origin in
        // silence. Found by mutation, 2026-09-06.
        {"a header without 'x_m' is not a layout at all",
         {"key", "kind", "y_m", "area_ha"},
         {{"field_a", "field", "200", "7.5"}},
         "",
         "x_m"},
        {"a kind the layout does not have is refused, not read as a field",
         header,
         {{"field_a", "arable", "", "100", "200", "7.5", "0", ""}},
         "field_a",
         "kind"},
        {"'12kg' in the area is refused, not laid out as nought hectares",
         header,
         {{"field_a", "field", "", "100", "200", "12kg", "0", ""}},
         "field_a",
         "area_ha"},
        {"a negative coordinate is refused: the table's own header says never negative",
         header,
         {{"field_a", "field", "", "-100", "200", "7.5", "0", ""}},
         "field_a",
         "x_m"},
        {"two rows may not share a key: the stock table finds a unit by it",
         header,
         {good_field, {"field_a", "field", "", "300", "400", "3", "0", ""}},
         "field_a",
         "key"},
        {"a unit row must name its type",
         header,
         {{"barn", "unit", "", "100", "200", "", "0", ""}},
         "barn",
         "unit_type"},
        {"a meadow must say which kind it is: the yield differs by two thirds",
         header,
         {{"meadow_a", "meadow", "", "100", "200", "20", "0", ""}},
         "meadow_a",
         "meadow_kind"},
        // THE CONDITION COLUMNS DESCRIBE A BUILDING. A wear on a field is a
        // cell nothing reads, and the first morning's scene would come out
        // exactly as if it had been blank — the silence these two columns
        // were added to end.
        {"a field may not be worn: wear is a property of a building",
         condition_header,
         {{"field_a", "field", "", "100", "200", "7.5", "0", "", "30", "0"}},
         "field_a",
         "start_wear_pct"},
        {"a meadow may not start dead",
         condition_header,
         {{"meadow_a", "meadow", "", "100", "200", "20", "0", "upland", "-1", "1"}},
         "meadow_a",
         "start_dead"},
        // MINUS ONE IS THE ONLY NEGATIVE THE COLUMN ADMITS. Were the rule
        // written as "negative means as built", a mistyped -10 would land
        // as the same "nothing said" and the wrecked mill would come out of
        // the table looking new.
        {"a wear of -10 is a typo, not a second way of saying 'as built'",
         condition_header,
         {{"barn", "unit", "barn", "100", "200", "", "0", "", "-10", "0"}},
         "barn",
         "start_wear_pct"},
    };
    for (const Case& item : refused) {
      const test::FakeTable layout(item.columns, item.rows);
      const test::FakeTableSet set({{"start_layout", &layout},
                                    {"unit_types", &empty},
                                    {"resources", &empty},
                                    {"crops", &empty}});
      std::string error;
      const core::WorldState refused_world =
          core::CreateStartWorld(set, core::StubTables::kAllowed, nullptr, 7, &error);
      failures += Expect(!error.empty(), item.label);
      failures += Expect(error.find(item.names_column) != std::string::npos,
                         "and the refusal names the column it choked on");
      failures += Expect(*item.names_row == '\0' || error.find(item.names_row) != std::string::npos,
                         "and the row, by the key a person can find in the CSV");
      // The refusal is not decoration: nothing of the scene was placed.
      failures += Expect(refused_world.fields.rows.empty() && refused_world.units.rows.empty(),
                         "and a refused layout founds no scene at all");
    }

    // A MISSING COLUMN IS NOT A DEFECTIVE ROW. 'unit_type' and 'meadow_kind'
    // are required by the rows that need them and by no others, so their
    // refusal has to say which of the two happened — the parser was itself
    // reporting an absent column as a bad row, which is the diagnosis it
    // exists to stop (UB-006 of the cycle, in code written that morning).
    {
      const std::vector<std::string> no_type = {"key", "kind", "x_m", "y_m", "area_ha"};
      const test::FakeTable layout(no_type, {{"barn", "unit", "100", "200", ""}});
      const test::FakeTableSet set({{"start_layout", &layout},
                                    {"unit_types", &empty},
                                    {"resources", &empty},
                                    {"crops", &empty}});
      std::string error;
      core::CreateStartWorld(set, core::StubTables::kAllowed, nullptr, 7, &error);
      failures += Expect(error.find("no such column") != std::string::npos,
                         "a unit row with no unit_type COLUMN is told the column is missing");
      failures +=
          Expect(error.find("unit_type") != std::string::npos, "and the missing column is named");
    }

    // A BLANK CELL IS NOT A DEFECT, and this is the half that keeps the
    // refusals above from being a machine that refuses everything: a unit
    // row names no area, an unsown year names no crop.
    {
      const test::FakeTable layout(
          header,
          {good_field,
           {"field_blank", "field", "", "300", "400", "", "0", ""},
           {"meadow_a", "meadow", "", "500", "600", "20", "0", "floodplain"}});
      const test::FakeTableSet set({{"start_layout", &layout},
                                    {"unit_types", &empty},
                                    {"resources", &empty},
                                    {"crops", &empty}});
      std::string error;
      const core::WorldState placed =
          core::CreateStartWorld(set, core::StubTables::kAllowed, nullptr, 7, &error);
      failures += Expect(error.empty(), "a layout whose blanks are blanks is accepted");
      failures += Expect(placed.fields.rows.size() == 3,
                         "and every row of it is placed — two fields and a meadow");
      bool floodplain = false;
      for (const core::FieldRow& field : placed.fields.rows) {
        floodplain = floodplain || field.kind == core::LandKind::kFloodplainMeadow;
      }
      failures += Expect(floodplain, "and the floodplain meadow keeps its kind through the parse");
    }
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
      const core::WorldState banded_world = core::CreateStartWorld(
          *banded_tables, core::StubTables::kAllowed, nullptr, 4242, nullptr);
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

  // THE INHERITED YARD COMES OUT OF THE TABLE AND NOT OUT OF PROSE. Until
  // the column existed the canon's worn church and half-ruined build yard
  // lived only in the design text, and the core founded them as new — which
  // is precisely what the prologue's first morning is a picture of.
  //
  // Checked against the SHIPPED tables, by key, and the numbers are read
  // back from the CSV rather than written here twice: a second copy of a
  // balance figure in a test is the drift this project keeps finding.
  {
    std::string shipped_error;
    const auto shipped = core::LoadTableSet(KOLKHOZ_TABLES_DIR, &shipped_error);
    failures += Expect(shipped != nullptr, "the shipped table set loads");
    if (shipped != nullptr) {
      const core::ITable* const layout = shipped->FindTable("start_layout");
      const core::ITable* const types = shipped->FindTable("unit_types");
      const core::WorldState morning =
          core::CreateStartWorld(*shipped, core::StubTables::kRefused, nullptr, 4242, nullptr);
      const std::uint32_t wear_col = layout->FindColumn("start_wear_pct");
      failures += Expect(wear_col != core::kNoTableColumn,
                         "the shipped layout carries the first morning's wear");
      std::uint32_t worn = 0;
      bool every_one_arrived = true;
      for (std::uint32_t row = 0; row < layout->RowCount(); ++row) {
        const std::string_view kind = layout->CellText(row, layout->FindColumn("kind"));
        const std::string_view text = layout->CellText(row, wear_col);
        if (kind != "unit" || text.empty() || text == "-1") {
          continue;
        }
        ++worn;
        const float expected = std::stof(std::string(text));
        const core::UnitTypeId type{static_cast<std::uint16_t>(
            types->FindRowByKey(layout->CellText(row, layout->FindColumn("unit_type"))))};
        bool found = false;
        for (const core::UnitRow& unit : morning.units.rows) {
          found = found || (unit.type.value == type.value && unit.wear == expected);
        }
        every_one_arrived = every_one_arrived && found;
      }
      // THE COUNT IS ASSERTED TOO. Without it the loop above passes on an
      // empty table — a check that cannot fail is the shape this project
      // has caught in itself more than once.
      failures += Expect(worn >= 4, "the shipped start names several inherited buildings");
      failures += Expect(every_one_arrived,
                         "and each of them stands at the wear its row gives it, not at nought");

      // AND THE MILL IS DEAD, which no wear can say: the scale ends at "a
      // ruin that still works". Asserted by key rather than by counting
      // dead units, so that the day a second one is added this check still
      // names the one it is about.
      const std::uint32_t dead_col = layout->FindColumn("start_dead");
      failures += Expect(dead_col != core::kNoTableColumn,
                         "the shipped layout says which buildings start dead");
      std::uint32_t dead_rows = 0;
      bool every_dead_one_arrived = true;
      for (std::uint32_t row = 0; row < layout->RowCount(); ++row) {
        if (layout->CellText(row, dead_col) != "1") {
          continue;
        }
        ++dead_rows;
        const core::UnitTypeId type{static_cast<std::uint16_t>(
            types->FindRowByKey(layout->CellText(row, layout->FindColumn("unit_type"))))};
        bool found = false;
        for (const core::UnitRow& unit : morning.units.rows) {
          found = found || (unit.type.value == type.value && unit.dead == 1);
        }
        every_dead_one_arrived = every_dead_one_arrived && found;
      }
      failures += Expect(dead_rows >= 1, "the first morning has something standing dead in it");
      failures += Expect(every_dead_one_arrived,
                         "and it comes out of genesis dead rather than merely worn");
    }
  }

  // The table-value debt (phase-2 task A6), tested where it bites. The
  // loader now refuses inf and nan at the door (tables.h), so what can still
  // reach a reader is a FINITE absurdity — and genesis casts livestock ages
  // to integers. Genesis only reaches the herds with the whole table set
  // present, so the shipped tables are copied and one cell is spoiled.
  const fs::path spoiled = fs::temp_directory_path() / "unit_core_world_tables";
  fs::remove_all(spoiled);
  fs::copy(fs::path(KOLKHOZ_TABLES_DIR), spoiled, fs::copy_options::recursive);
  // NEGATIVE, not huge, and the difference is the whole point of this pass.
  // A 1e30 was refused even by genesis's own private ceiling of 1e9; MINUS
  // FIVE sailed straight through it, because that ceiling was symmetric and
  // was never a band at all — it was one number standing in for the ranges
  // of seven different columns. Since 2026-09-06 every cell genesis reads
  // declares what it may hold, out of core_catalog, and a lifetime is
  // non-negative.
  failures += Expect(SpoilColumn(spoiled / "livestock.csv", "life_game_years_max", "-5"),
                     "the livestock cell to spoil was found");
  std::string spoil_error;
  const auto spoiled_tables = core::LoadTableSet(spoiled.string(), &spoil_error);
  failures += Expect(spoiled_tables != nullptr, "the spoiled table set still loads");
  if (spoiled_tables != nullptr) {
    const core::WorldState wild = core::CreateStartWorld(
        *spoiled_tables, core::StubTables::kAllowed, nullptr, 12345, nullptr);
    failures += Expect(!wild.herds.rows.empty(),
                       "the roster reaches the herds — otherwise the check below is vacuous");
    bool ages_are_sane = true;
    for (const core::HerdRow& herd : wild.herds.rows) {
      const float age_sum = herd.adult_age_game_years_total;
      ages_are_sane = ages_are_sane && age_sum >= 0.0F && age_sum < 1e6F;
    }
    failures += Expect(ages_are_sane, "an absurd livestock cell never reaches the herd ages");
    // The control: the herds must actually be aged from that column, or the
    // check above is satisfied by a world that never read it.
    bool some_age_is_set = false;
    for (const core::HerdRow& herd : wild.herds.rows) {
      some_age_is_set = some_age_is_set || herd.adult_age_game_years_total > 0.0F;
    }
    failures += Expect(some_age_is_set,
                       "and the herds are aged at all — otherwise the check above is vacuous");
    const core::WorldState wild_again = core::CreateStartWorld(
        *spoiled_tables, core::StubTables::kAllowed, nullptr, 12345, nullptr);
    failures += Expect(wild_again.rng.state == wild.rng.state,
                       "and the fallback keeps genesis deterministic");
  }
  fs::remove_all(spoiled);

  // AND THE ASSEMBLY ACTS ON THE REFUSAL — which is a different claim from
  // "the parser refuses", and the one that was missing. Every check above
  // reads CreateStartWorld's error; not one of them would have gone red if
  // CreateStandardSimulation had read that error and built the world anyway.
  // That is the class this whole day has been about: a check that runs and
  // whose result is not used. Found by mutation, 2026-09-06.
  {
    const fs::path bad_scene = fs::temp_directory_path() / "unit_core_world_bad_layout";
    fs::remove_all(bad_scene);
    fs::copy(fs::path(KOLKHOZ_TABLES_DIR), bad_scene, fs::copy_options::recursive);

    // The control first: the SHIPPED tables must assemble, or the refusal
    // below proves nothing — a set that never assembles is refused for
    // whatever reason one likes.
    std::string clean_error;
    const auto clean_tables = core::LoadTableSet(bad_scene.string(), &clean_error);
    failures += Expect(clean_tables != nullptr, "the shipped table set loads");
    if (clean_tables != nullptr) {
      core::StandardSimulationConfig shipped;
      shipped.tables = clean_tables.get();
      shipped.world_seed = 3;
      failures += Expect(core::CreateStandardSimulation(shipped) != nullptr,
                         "and the shipped scene assembles — the control for the refusal below");
    }

    failures += Expect(SpoilColumn(bad_scene / "start_layout.csv", "x_m", "-1"),
                       "the layout column to spoil was found");
    std::string bad_error;
    const auto bad_tables = core::LoadTableSet(bad_scene.string(), &bad_error);
    failures += Expect(bad_tables != nullptr, "a layout of negative metres still LOADS");
    if (bad_tables != nullptr) {
      core::StandardSimulationConfig broken;
      broken.tables = bad_tables.get();
      broken.world_seed = 3;
      failures += Expect(core::CreateStandardSimulation(broken) == nullptr,
                         "and the assembly refuses it instead of founding the village outside "
                         "the world");
    }
    fs::remove_all(bad_scene);
  }

  // THE FIGURE OF THE VILLAGE (core_common/body.h). Eighty people who were
  // all exactly one height until 2026-09-06.
  {
    const fs::path figures = fs::temp_directory_path() / "unit_core_world_figures";
    fs::remove_all(figures);
    fs::copy(fs::path(KOLKHOZ_TABLES_DIR), figures, fs::copy_options::recursive);
    std::string figure_error;
    const auto figure_tables = core::LoadTableSet(figures.string(), &figure_error);
    failures += Expect(figure_tables != nullptr, "the tables for the figure guard load");
    if (figure_tables != nullptr) {
      const core::WorldState village =
          core::CreateStartWorld(*figure_tables, core::StubTables::kAllowed, nullptr, 777, nullptr);
      const core::WorldState same =
          core::CreateStartWorld(*figure_tables, core::StubTables::kAllowed, nullptr, 777, nullptr);
      const core::WorldState other =
          core::CreateStartWorld(*figure_tables, core::StubTables::kAllowed, nullptr, 778, nullptr);

      bool all_alike = true;
      bool inside_the_clamp = true;
      bool same_seed_agrees = true;
      bool some_seed_differs = false;
      const float first = village.residents.rows[0].height_deviation;
      // 2.5 sigma of 3.7 % is 9.25 %; nothing may stand outside it, and the
      // check is written with a hair of slack for the float arithmetic
      // rather than against an exact equality.
      constexpr float kBand = 0.0926F;
      for (std::size_t row = 0; row < village.residents.rows.size(); ++row) {
        const core::ResidentRow& person = village.residents.rows[row];
        all_alike = all_alike && person.height_deviation == first;
        inside_the_clamp = inside_the_clamp && person.height_deviation >= -kBand &&
                           person.height_deviation <= kBand;
        same_seed_agrees = same_seed_agrees &&
                           same.residents.rows[row].height_deviation == person.height_deviation;
        some_seed_differs = some_seed_differs ||
                            other.residents.rows[row].height_deviation != person.height_deviation;
      }
      failures +=
          Expect(!village.residents.rows.empty(), "the figure guard has a village to look at");
      failures += Expect(!all_alike,
                         "the starting village is not eighty people of one height — which is the "
                         "whole reason the field exists");
      // THE CUT'S OWN GUARD IS IN core_common, on two hundred thousand draws.
      // This one is about the VILLAGE, and it is worth having for that: at
      // eighty people the band is never reached, so with the cut removed this
      // line stays green — it says the shipped world is sane, not that the
      // rule works. The distinction is written down because the first version
      // of this check thought it was testing the rule.
      failures += Expect(inside_the_clamp,
                         "and the shipped village stands inside the band the knobs describe");
      failures += Expect(same_seed_agrees,
                         "the same seed gives the same village its same figures — the draw is a "
                         "function of the world, not of the order it was built in");
      failures += Expect(some_seed_differs, "and another seed gives another village");

      // THE FIGURE COSTS THE WORLD'S RNG NOTHING, and this is the assertion
      // the balance runs taught. The first version drew from the stream, and
      // four draws per person rerolled everything downstream: truancy stopped
      // happening at all and the year's labour fell under its reference band.
      // A counter hash keyed by the person consumes nothing, so the state of
      // the stream after genesis must not depend on the figure knobs at all.
      const fs::path widened = fs::temp_directory_path() / "unit_core_world_figures_wide";
      fs::remove_all(widened);
      fs::copy(fs::path(KOLKHOZ_TABLES_DIR), widened, fs::copy_options::recursive);
      // ONE ROW AND NOT THE WHOLE COLUMN. The first draft widened every
      // value in the file, which put `body_height_clamp_sigma` outside its
      // own declared range — so the read was REFUSED, the documented
      // defaults came back, and those are the shipped numbers: the guard
      // compared the village against itself. A blunt instrument gave a
      // green that meant nothing.
      failures +=
          Expect(SetKeyValue(widened / "world_params.csv", "body_height_sigma_frac", "0.02"),
                 "the sigma row to narrow was found");
      std::string wide_error;
      const auto wide_tables = core::LoadTableSet(widened.string(), &wide_error);
      if (wide_tables != nullptr) {
        const core::WorldState wider =
            core::CreateStartWorld(*wide_tables, core::StubTables::kAllowed, nullptr, 777, nullptr);
        failures += Expect(wider.rng.state == village.rng.state,
                           "changing the figure knobs does not move the world's RNG by one step: "
                           "a new fact must not move the facts that were already there");
        bool figures_did_change = false;
        for (std::size_t row = 0; row < wider.residents.rows.size(); ++row) {
          figures_did_change =
              figures_did_change || wider.residents.rows[row].height_deviation !=
                                        village.residents.rows[row].height_deviation;
        }
        failures += Expect(figures_did_change,
                           "while the figures themselves DID change — otherwise the check above "
                           "passes on knobs nobody read");
      }
      fs::remove_all(widened);
    }
    fs::remove_all(figures);
  }

  // THE START STOCK IS MEASURED THROUGH THE CONSTRUCTION DOOR, and this is
  // the check that genesis ASKS it rather than carrying a ladder of its own
  // (boss, 2026-09-06: "an instrument that re-derives a quantity does not
  // check the one it was given").
  //
  // Read from two sides on purpose. The same overfilled table is founded
  // twice: once with the door, where the excess is cut and booked to
  // lost_no_room, and once with nullptr, where there is no capacity to be
  // full against and the stock stands as written. One side alone would pass
  // just as well if genesis measured against something else entirely.
  {
    const fs::path overfilled = fs::temp_directory_path() / "unit_core_world_overfill";
    fs::remove_all(overfilled);
    fs::copy(fs::path(KOLKHOZ_TABLES_DIR), overfilled, fs::copy_options::recursive);
    // The church store holds 60 t on its only rung; the canon puts 54 t in
    // it. 135 t of potatoes instead of 35 puts it 94 t over, and nothing but
    // the ladder says so.
    {
      std::ifstream source(overfilled / "start_stock.csv");
      std::string rows;
      std::string stock_line;
      bool found = false;
      while (std::getline(source, stock_line)) {
        if (stock_line.rfind("church_store,potato,", 0) == 0) {
          rows += "church_store,potato,135,1000\n";
          found = true;
          continue;
        }
        rows += stock_line + "\n";
      }
      source.close();
      failures += Expect(found, "the row to overfill the church store was found");
      std::ofstream(overfilled / "start_stock.csv", std::ios::trunc) << rows;
    }
    std::string overfill_error;
    const auto overfilled_tables = core::LoadTableSet(overfilled.string(), &overfill_error);
    failures += Expect(overfilled_tables != nullptr, "the overfilled table set loads");
    if (overfilled_tables != nullptr) {
      const auto capacities =
          core::CreateConstructionSystem(*overfilled_tables, core::StubTables::kRefused);
      failures += Expect(capacities != nullptr, "and it builds a construction subsystem");
      const core::WorldState measured = core::CreateStartWorld(
          *overfilled_tables, core::StubTables::kAllowed, capacities.get(), 999, nullptr);
      const core::WorldState unmeasured = core::CreateStartWorld(
          *overfilled_tables, core::StubTables::kAllowed, nullptr, 999, nullptr);
      core::Grams cut = 0;
      for (const core::Grams lost : measured.ledger.current.lost_no_room) {
        cut += lost;
      }
      core::Grams cut_without = 0;
      for (const core::Grams lost : unmeasured.ledger.current.lost_no_room) {
        cut_without += lost;
      }
      failures += Expect(cut > 0, "with the door, the excess over the ladder's 60 t is cut");
      failures += Expect(cut_without == 0,
                         "without it nothing is cut — so the cut above came from the ladder and "
                         "not from a number genesis kept for itself");
    }
    fs::remove_all(overfilled);
  }

  // EVERY TABLE OF THE SHIPPED SET, TAKEN OUT ONE AT A TIME. The order this
  // answers (boss, 2026-09-07) came from `host`, who removed alarms.csv to
  // test an instrument of his own and found that the run did not refuse —
  // it converged, printed plausible numbers and said nothing.
  //
  // WHY THE LIST HERE IS OF TABLES THE CORE DOES NOT READ, and not of the
  // ones it does. A list of the required would be a third home for a rule
  // that already has two (the module lists and the readers themselves), and
  // it would age towards agreeing with them. This one ages the other way: a
  // NEW file in tables/ is red until somebody says which side it is on, and
  // a table that stops being required is red as well. Neither can happen in
  // silence, which is the whole complaint.
  {
    // Read by the host, the layer, or nobody yet — the core never asks for
    // them, so their absence cannot change a single number it computes.
    const std::array<std::string_view, 10> not_read_by_the_core = {"alarms",
                                                                   "difficulty",
                                                                   "diseases",
                                                                   "disease_severity",
                                                                   "event_sites",
                                                                   "farm_health_bands",
                                                                   "forest_biome_mix",
                                                                   "forest_biomes",
                                                                   "forest_forage",
                                                                   "tree_species"};
    const std::array<std::string_view, 2> also_not_read = {"resident_activities",
                                                           "resident_activity_details"};
    const fs::path doctored = fs::temp_directory_path() / "unit_core_world_missing_table";
    for (const fs::directory_entry& file : fs::directory_iterator(fs::path(KOLKHOZ_TABLES_DIR))) {
      if (file.path().extension() != ".csv") {
        continue;
      }
      const std::string name = file.path().stem().string();
      const bool ignored =
          std::ranges::find(not_read_by_the_core, name) != not_read_by_the_core.end() ||
          std::ranges::find(also_not_read, name) != also_not_read.end();
      fs::remove_all(doctored);
      fs::copy(fs::path(KOLKHOZ_TABLES_DIR), doctored, fs::copy_options::recursive);
      fs::remove(doctored / file.path().filename());
      const auto set = core::LoadTableSet(doctored.string(), nullptr);
      if (Expect(set != nullptr, "a set with one file taken out still loads as tables") != 0) {
        continue;
      }
      core::StandardSimulationConfig probe_config;
      probe_config.tables = set.get();
      probe_config.world_seed = 1929;
      probe_config.worker_count = 1;
      // The refusal is the subject, so this is the strict word — the one a
      // game, a run and the ledger tool all use.
      probe_config.stub_tables = core::StubTables::kRefused;
      const bool assembled = core::CreateStandardSimulation(probe_config) != nullptr;
      const std::string label =
          ignored ? "a table the core never reads does not stop the assembly (" + name +
                        ") — if it is read now, it belongs on the other side of that list"
                  : "the assembly refuses a set missing a table the core reads (" + name + ")";
      failures += Expect(ignored == assembled, label.c_str());
    }
    fs::remove_all(doctored);
  }

  if (failures == 0) {
    std::cout << "unit_core_world: all checks passed\n";
  }
  return failures;
}
