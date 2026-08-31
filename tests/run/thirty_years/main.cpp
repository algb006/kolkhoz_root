// Simulation run: thirty years on the shipped tables, writing the yearly
// ledger sheet the reconciliation is done on (stage 7, tasks F2/O2;
// manual/68-run-ledger.md).
//
// THIS RUN'S OUTPUT IS ITS POINT. The assertions below are a floor — the
// village exists, the books balance, nothing has gone structurally mad —
// and they are deliberately loose: this is not a criterion run like
// food_year or labor_year, it is the instrument the criterion of the whole
// phase (task F3) is measured with. A tight band here would only mean
// asserting today's numbers before anyone has checked they are right.
//
// The sheet lands in claude/analysis/, which is outside git: it is a
// measurement, not a source, and it changes with every table edit.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "../common/run_harness.h"
#include "core_common/calendar.h"
#include "core_common/ledger_state.h"
#include "core_common/world_state.h"
#include "core_report/ledger_csv.h"

namespace {

constexpr std::uint32_t kYears = 30;

constexpr std::uint64_t kSeed = 1929;

/// Where the sheet goes. Relative to the repository root, which is this
/// test's working directory (its CMakeLists sets it).
/// The second sheet: one row per arable field per year — fertility at the
/// year's turn, the crop and whether manure went in. The ledger keeps only
/// the settlement's mean, and the fourth reconciliation pass asked a
/// question the mean cannot answer: does a field that GOT the manure grow
/// its fertility, as the canon promises for a sound rotation, while one that
/// did not merely holds? Fields are few, so the sheet stays small.
std::filesystem::path FieldSheetPath() {
  return std::filesystem::path("claude") / "analysis" /
         ("fields_" + std::to_string(kSeed) + ".csv");
}

std::string CropKey(const core::ITableSet& tables, core::CropId crop) {
  const core::ITable* crops = tables.FindTable("crops");
  if (crops == nullptr || crop.value == core::kInvalidDefIdValue) {
    return "-";
  }
  return std::string(crops->CellText(crop.value, crops->FindColumn("key")));
}

/// Written at the TURN of the year, when the crop of the ending year is off
/// and the manure flag has already been consumed by the harvest. So the
/// crop and the manure are remembered from midsummer (`sampled`), when both
/// are still on the row; the fertility is the turn's, after the harvest
/// settled it.
struct FieldSample {
  std::string crop;
  bool manured = false;
  float stress = 0.0F;  ///< Weather stress accumulated by midsummer.
};

std::filesystem::path SheetPath() {
  return std::filesystem::path("claude") / "analysis" /
         ("ledger_" + std::to_string(kSeed) + ".csv");
}

/// A year's row, printed for the terminal: enough to see the shape of the
/// run without opening the sheet, and no more.
void PrintYear(const core::WorldState& state) {
  const core::YearLedger& book = state.ledger.closed;
  const float mean = book.satiety_days > 0
                         ? book.satiety_day_mean_sum / static_cast<float>(book.satiety_days)
                         : 0.0F;
  std::cout << "year " << book.year << ": " << state.residents.rows.size() << " residents, satiety "
            << mean << " (leanest day " << book.satiety_day_mean_min << "), " << book.births
            << " born, " << book.deaths << " died, " << book.departures << " left, herd "
            << book.herd_births << "/+" << " -" << (book.herd_deaths_age + book.herd_deaths_hunger)
            << ", LE " << state.vitals.life_expectancy_years << '\n';
}

/// What the herd looks like at the end, kolkhoz and yard apart. NOT an
/// assertion: the floor below deliberately does not judge the balance. It
/// is printed because the first thing this instrument found was a herd
/// collapse nothing else in the suite could see, and a number that has to
/// be dug out of a 1500-column sheet is a number nobody will look at.
void PrintHerdFinding(const core::WorldState& state) {
  std::uint32_t kolkhoz = 0;
  std::uint32_t yard = 0;
  for (const core::HerdRow& herd : state.herds.rows) {
    const std::uint32_t heads =
        static_cast<std::uint32_t>(herd.newborn_count) + herd.juvenile_count + herd.adult_count;
    (herd.household_owned != 0 ? yard : kolkhoz) += heads;
  }
  std::cout << "thirty_years: at the end — " << kolkhoz << " head of kolkhoz livestock, " << yard
            << " in the yards\n";
  if (yard == 0 || kolkhoz == 0) {
    std::cout << "thirty_years: FINDING — a whole herd is gone. A private yard's animals do not "
                 "breed in the core yet, so once the last of them ages out the yard is empty for "
                 "good — and with it the milk that is a third of the village's table.\n";
  }
}

}  // namespace

int main() {
  int failures = 0;
  run::Simulation world = run::Start(kSeed);
  if (!world) {
    return 1;
  }

  std::filesystem::create_directories(SheetPath().parent_path());
  std::ofstream sheet(SheetPath(), std::ios::binary | std::ios::trunc);
  if (!sheet) {
    std::cout << "FAIL: cannot write " << SheetPath().string() << '\n';
    return 1;
  }
  sheet << core::LedgerCsvHeader(*world.tables);

  // What the years add up to, for the balance checks below. Kept as doubles
  // because thirty years of grams overflow a float's mantissa long before
  // they overflow the counter.
  double harvested_tonnes = 0.0;
  double eaten_tonnes = 0.0;
  std::uint32_t rows_written = 0;
  std::uint16_t last_year = 0;
  double hungry_head_days = 0.0;
  double lived_head_days = 0.0;
  std::uint32_t years_without_plowing = 0;
  float lowest_fertility = 100.0F;

  std::ofstream field_sheet(FieldSheetPath(), std::ios::binary | std::ios::trunc);
  field_sheet << "year,field,kind,area_ha,crop,manured,fertility,stress_july\n";
  std::vector<FieldSample> sampled;

  for (std::uint32_t year = 0; year < kYears; ++year) {
    for (std::uint32_t day = 0; day < core::kDaysPerYear; ++day) {
      run::AdvanceDays(*world, 1);
      const core::WorldState& mid = world.State();
      if (mid.calendar.date.month == core::Month::kJuly && mid.calendar.date.day_in_month == 0) {
        sampled.assign(mid.fields.rows.size(), FieldSample{});
        for (std::size_t field = 0; field < mid.fields.rows.size(); ++field) {
          sampled[field].crop = CropKey(*world.tables, mid.fields.rows[field].crop);
          sampled[field].manured = mid.fields.rows[field].manure_applied != 0;
          sampled[field].stress = mid.fields.rows[field].weather_stress;
        }
      }
    }
    const core::WorldState& state = world.State();
    for (std::size_t field = 0; field < state.fields.rows.size(); ++field) {
      const core::FieldRow& row = state.fields.rows[field];
      if (row.kind == core::LandKind::kMeadow || row.kind == core::LandKind::kFloodplainMeadow) {
        continue;
      }
      const FieldSample sample = field < sampled.size() ? sampled[field] : FieldSample{};
      field_sheet << (year + 1) << ',' << field << ','
                  << (row.kind == core::LandKind::kDerelict ? "derelict" : "arable") << ','
                  << row.area_ga << ',' << sample.crop << ',' << (sample.manured ? 1 : 0) << ','
                  << row.fertility << ',' << sample.stress << '\n';
    }
    // The book closes on the first tick of the new year, so a whole year of
    // ticks always leaves exactly one new closed book to take.
    if (state.ledger.closed.year == last_year) {
      continue;  // the first year has not turned yet
    }
    last_year = state.ledger.closed.year;
    sheet << core::LedgerCsvRow(state, *world.tables);
    ++rows_written;
    PrintYear(state);
    for (const core::Grams grams : state.ledger.closed.harvest) {
      harvested_tonnes += static_cast<double>(grams) / 1.0e6;
    }
    for (const core::Grams grams : state.ledger.closed.eaten) {
      eaten_tonnes += static_cast<double>(grams) / 1.0e6;
    }
    hungry_head_days += static_cast<double>(state.ledger.closed.herd_hungry_head_days);
    for (const core::HerdRow& herd : state.herds.rows) {
      lived_head_days +=
          static_cast<double>(herd.newborn_count + herd.juvenile_count + herd.adult_count) *
          core::kDaysPerYear;
    }
    const float plowed =
        state.ledger.closed.work_days_by_kind[static_cast<std::size_t>(core::WorkKind::kPlowing)];
    years_without_plowing += plowed > 0.0F ? 0U : 1U;
    for (const core::FieldRow& field : state.fields.rows) {
      if (field.kind == core::LandKind::kArable) {
        lowest_fertility = field.fertility < lowest_fertility ? field.fertility : lowest_fertility;
      }
    }
  }
  sheet.close();

  const core::WorldState& state = world.State();
  std::cout << "thirty_years: " << rows_written << " years written to " << SheetPath().string()
            << "; " << harvested_tonnes << " t harvested, " << eaten_tonnes << " t eaten over the "
            << "run\n";

  PrintHerdFinding(state);

  // -- the floor -----------------------------------------------------------
  // The book of year N closes on the first tick of year N+1, which is the
  // last tick of the Nth year of ticks: thirty years of ticks therefore
  // close exactly thirty books, the last of them year 30.
  failures += run::Expect(rows_written == kYears, "one row per year of the run");
  failures +=
      run::Expect(state.ledger.closed.year == kYears, "the last closed book is the last full year");
  failures +=
      run::Expect(state.residents.rows.size() > 100, "the settlement is alive after thirty years");
  failures += run::Expect(state.families.rows.size() > 20, "and it has households");

  // -- what task O2b made structural, and what the first pass had none of ---
  // These are not balance bands. They are the three ways the farm used to
  // stop being a farm, and every one of them was a rule of the canon the
  // core was not keeping (manual/balance/69-reconciliation.md).
  //
  // The plough is the load-bearing one. Ploughing is horse work; the team
  // ages out by the sixth year unless a stable stands, and phase 1 has no
  // construction — so from year seven the old core sowed nothing, ever
  // again, and three hundred adults stood idle beside six fields frozen in
  // the ploughing phase.
  failures += run::Expect(years_without_plowing == 0, "the farm ploughs in every year of the run");
  std::cout << "thirty_years: the leanest arable field ended at " << lowest_fertility
            << " fertility\n";
  failures += run::Expect(lowest_fertility >= 15.0F,
                          "and no field is worked into desert: the repeat penalty has a ceiling "
                          "and fertility has a floor");
  std::uint32_t kolkhoz_heads = 0;
  for (const core::HerdRow& herd : state.herds.rows) {
    if (herd.household_owned == 0) {
      kolkhoz_heads +=
          static_cast<std::uint32_t>(herd.newborn_count) + herd.juvenile_count + herd.adult_count;
    }
  }
  failures +=
      run::Expect(kolkhoz_heads > 10, "the kolkhoz herds are still standing after thirty years");

  // The two halves of the food year must both be real. A run where nothing
  // is harvested, or nothing is eaten, would still satisfy every count above
  // — and both have happened during this phase's development.
  failures += run::Expect(harvested_tonnes > 100.0, "the fields produced over the run");
  failures += run::Expect(eaten_tonnes > 100.0, "and the village ate");

  // The herd is the part of the balance that goes wrong quietly: a barn can
  // starve for years at half milk without a single count moving.
  // NOT "did anything anywhere starve this year". That reading was written
  // when the settlement had four herds; it now has one for every yard in the
  // village, and a single household that let its plot go and mowed too
  // little hay would trip it — which is the design working, not failing
  // (household design §1: the plot's hours are what the yard's animals live
  // on). What must not happen is CHRONIC hunger, so the measure is a rate:
  // the head-days of hunger against the head-days the settlement's animals
  // lived at all.
  const double hungry_share = lived_head_days > 0.0 ? hungry_head_days / lived_head_days : 0.0;
  std::cout << "thirty_years: " << (hungry_share * 100.0) << "% of the animals' head-days were "
            << "hungry ones\n";
  failures += run::Expect(hungry_share < 0.05, "the herds are not chronically underfed");

  // The ledger is state like any other, so a book that never closed, or a
  // year counted twice, shows up as a satiety day count that is not a year.
  const core::YearLedger& book = state.ledger.closed;
  failures += run::Expect(book.satiety_days == core::kDaysPerYear,
                          "a closed book holds exactly one year of days");

  if (failures == 0) {
    std::cout << "thirty_years: all checks passed\n";
  }
  return failures;
}
