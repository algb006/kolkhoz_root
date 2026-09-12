// Unit test of core_report: the header and the row agree, the numbers read
// back as what was put in, and a missing table drops its group from both.
//
// The tables are synthesized into a temporary directory: this test is about
// the sheet, not about the shipped balance, and a fixed four-key resource
// list makes every column countable by hand.

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "core_common/ledger_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_report/ledger_csv.h"
#include "core_tables/tables.h"

namespace {

int Expect(bool condition, const char* label) {
  if (condition) {
    return 0;
  }
  std::cout << "FAIL: " << label << '\n';
  return 1;
}

void WriteTableFile(const std::filesystem::path& path,
                    const std::vector<std::string>& keys,
                    const char* extra_column) {
  std::ofstream out(path);
  out << "key," << extra_column << '\n';
  for (const std::string& key : keys) {
    out << key << ",1\n";
  }
}

/// Splits one CSV line on commas. The dialect needs no quoting here: keys
/// are snake_case and values are numbers (manual/61-balance-tables.md §2).
std::vector<std::string> Split(const std::string& line) {
  std::vector<std::string> cells;
  std::string cell;
  for (const char letter : line) {
    if (letter == ',') {
      cells.push_back(cell);
      cell.clear();
      continue;
    }
    if (letter != '\n') {
      cell += letter;
    }
  }
  cells.push_back(cell);
  return cells;
}

std::size_t IndexOf(const std::vector<std::string>& header, const std::string& name) {
  for (std::size_t index = 0; index < header.size(); ++index) {
    if (header[index] == name) {
      return index;
    }
  }
  return header.size();
}

}  // namespace

int main() {
  int failures = 0;
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "kolkhoz_unit_core_report";
  const std::filesystem::path full = root / "full";
  const std::filesystem::path no_livestock = root / "no_livestock";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(full);
  std::filesystem::create_directories(no_livestock);

  const std::vector<std::string> resources = {"oat", "potato", "milk", "hay"};
  WriteTableFile(full / "resources.csv", resources, "edible");
  WriteTableFile(full / "livestock.csv", {"cow", "goat"}, "feed_units_per_real_day");
  WriteTableFile(no_livestock / "resources.csv", resources, "edible");

  std::string error;
  const auto tables = core::LoadTableSet(full.string(), &error);
  const auto lean_tables = core::LoadTableSet(no_livestock.string(), &error);
  if (tables == nullptr || lean_tables == nullptr) {
    std::cout << "FAIL: the synthetic table sets did not load (" << error << ")\n";
    return 1;
  }

  core::WorldState state;
  state.epoch = core::Epoch::kTwo;
  state.vitals.life_expectancy_years = 61.5F;
  core::FieldRow field;
  field.rotation_year0 = core::CropId{0};  // a chain: this land is worked
  field.area_ga = 10.0F;
  field.fertility = 60.0F;
  core::AppendRow(state.fields, field);
  field.area_ga = 30.0F;
  field.fertility = 80.0F;
  core::AppendRow(state.fields, field);  // area-weighted mean is 75, not 70
  // AND GROUND NOBODY HAS GIVEN A ROTATION IS NOT IN IT EITHER, which was a
  // land KIND until 2026-09-12: unworked land carried LandKind::kDerelict and
  // fell out of this walk unremarked. When the kind went, ninety-three of the
  // start canon's hundred and sixty-three hectares — frozen at sixty-five —
  // would have entered the mean the balance calculations are read off, and
  // nothing here would have said a word. Forty hectares at ten would drag 75
  // to 40.6.
  core::FieldRow unworked;
  unworked.area_ga = 40.0F;
  unworked.fertility = 10.0F;
  unworked.overgrown = 1;
  core::AppendRow(state.fields, unworked);
  // A meadow has no fertility to average in: 200 ha of grass at the neutral
  // default would drag the mean of the ARABLE to something nobody sowed.
  core::FieldRow meadow;
  meadow.kind = core::LandKind::kMeadow;
  meadow.area_ga = 200.0F;
  meadow.fertility = 50.0F;
  core::AppendRow(state.fields, meadow);

  core::UnitRow store;
  store.stock = core::ResourceAmounts{1'000'000, 0, 0, 5'000'000};
  core::AppendRow(state.units, store);
  core::FamilyRow family;
  family.pantry = core::ResourceAmounts{250'000, 700'000};
  core::AppendRow(state.families, family);
  core::HerdRow cows;
  cows.kind = core::LivestockKindId{0};
  cows.adult_count = 39;
  cows.adult_male_count = 2;
  cows.billeted_count = 4;
  core::AppendRow(state.herds, cows);
  core::HerdRow yard_goats;
  yard_goats.kind = core::LivestockKindId{1};
  yard_goats.household_owned = 1;
  yard_goats.adult_count = 2;
  yard_goats.juvenile_count = 1;
  core::AppendRow(state.herds, yard_goats);

  core::YearLedger& book = state.ledger.closed;
  book.year = 7;
  book.births = 12;
  book.deaths = 5;
  book.satiety_day_mean_sum = 48.0F * 70.5F;
  book.satiety_days = 48;
  book.satiety_day_mean_min = 31.25F;
  book.hungry_at_once_max = 9;
  book.area_harvested_ha = 43.5F;
  book.trudodni_accrued = 71'924;  // 719.24 trudodni, stored in hundredths
  book.work_days_by_kind[static_cast<std::size_t>(core::WorkKind::kHarvest)] = 374.5F;
  core::AddLedgerAmount(book.harvest, core::ResourceId{0}, 43'000'000);
  core::AddLedgerAmount(book.eaten, core::ResourceId{1}, 9'500'500);

  const std::vector<std::string> header = Split(core::LedgerCsvHeader(*tables));
  const std::vector<std::string> row = Split(core::LedgerCsvRow(state, *tables));
  failures += Expect(header.size() == row.size(), "the header and the row have the same width");
  failures += Expect(header.size() > std::size_t{4} * 14U, "the per-resource blocks are all there");

  const auto cell = [&](const char* name) -> std::string {
    const std::size_t index = IndexOf(header, name);
    return index < row.size() ? row[index] : std::string("<no such column>");
  };

  failures += Expect(cell("year") == "7", "the year is the closed book's");
  failures += Expect(cell("population") == "0" && cell("families") == "1",
                     "the state columns describe the world, not the book");
  failures += Expect(cell("epoch") == "2", "the epoch prints as its number");
  failures += Expect(cell("fertility_mean") == "75",
                     "fertility is weighted by area; meadows are not in it, and neither is "
                     "ground nobody has given a rotation — 40 ha at ten would drag 75 to 40.6");
  failures += Expect(cell("births") == "12" && cell("deaths") == "5", "the people flows");
  failures += Expect(cell("satiety_mean") == "70.5", "the yearly mean is the sum over the days");
  failures += Expect(cell("satiety_day_min") == "31.25" && cell("hungry_at_once_max") == "9",
                     "and the extremes come through as they stand");
  failures += Expect(cell("trudodni_accrued") == "719.24",
                     "trudodni are reported whole, not in hundredths");
  failures += Expect(cell("work_harvest_days") == "374.5", "labor is named by work kind");

  // Masses: grams in, kilograms with three decimals out, exactly.
  failures += Expect(cell("harvest_oat_kg") == "43000.000", "a mass prints as exact kilograms");
  failures += Expect(cell("eaten_potato_kg") == "9500.500", "down to the gram");
  failures += Expect(cell("harvest_potato_kg") == "0.000",
                     "a resource that never moved still has its column");
  failures += Expect(cell("store_hay_kg") == "5000.000" && cell("pantry_potato_kg") == "700.000",
                     "the stores and the pantries are summed over the village");

  // Herds: the kolkhoz rungs apart from what stands in the yards.
  failures += Expect(cell("herd_cow_adult") == "39" && cell("herd_cow_male") == "2" &&
                         cell("herd_cow_billeted") == "4",
                     "a kolkhoz herd fills its rung columns");
  failures += Expect(cell("yard_goat_head") == "3" && cell("herd_goat_adult") == "0",
                     "a yard herd is counted apart, all rungs together");

  // A missing table drops its whole group — from BOTH functions, or the
  // sheet would silently shift by a column.
  const std::vector<std::string> lean_header = Split(core::LedgerCsvHeader(*lean_tables));
  const std::vector<std::string> lean_row = Split(core::LedgerCsvRow(state, *lean_tables));
  failures += Expect(lean_header.size() == lean_row.size(),
                     "a table-less sheet still lines its header up with its row");
  failures += Expect(lean_header.size() == header.size() - (std::size_t{2} * 6U),
                     "and it is exactly the livestock group narrower");
  failures += Expect(IndexOf(lean_header, "herd_cow_adult") == lean_header.size(),
                     "the dropped group leaves no column behind");

  std::filesystem::remove_all(root);
  if (failures == 0) {
    std::cout << "unit_core_report: all checks passed (" << header.size() << " columns)\n";
  }
  return failures;
}
