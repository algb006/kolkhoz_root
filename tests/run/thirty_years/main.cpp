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
    std::cout << "thirty_years: FINDING — a whole herd is gone. Private herds do not breed in "
                 "phase 1 by design, so a yard that loses its animals never gets them back.\n";
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
  std::uint32_t years_with_a_death_from_hunger = 0;

  for (std::uint32_t year = 0; year < kYears; ++year) {
    run::AdvanceYear(*world);
    const core::WorldState& state = world.State();
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
    years_with_a_death_from_hunger += state.ledger.closed.herd_deaths_hunger > 0 ? 1U : 0U;
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

  // The two halves of the food year must both be real. A run where nothing
  // is harvested, or nothing is eaten, would still satisfy every count above
  // — and both have happened during this phase's development.
  failures += run::Expect(harvested_tonnes > 100.0, "the fields produced over the run");
  failures += run::Expect(eaten_tonnes > 100.0, "and the village ate");

  // The herd is the part of the balance that goes wrong quietly: a barn can
  // starve for years at half milk without a single count moving.
  failures += run::Expect(years_with_a_death_from_hunger < kYears / 2,
                          "the herd is not starving year in, year out");

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
