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
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../common/fixture_policy.h"
#include "../common/orders_policy.h"
#include "../common/repair_policy.h"
#include "../common/run_harness.h"
#include "../common/yard_policy.h"
#include "core_catalog/definitions.h"
#include "core_common/calendar.h"
#include "core_common/ledger_state.h"
#include "core_common/plot.h"
#include "core_common/world_state.h"
#include "core_construction/construction_system.h"
#include "core_report/ledger_csv.h"

namespace {

constexpr std::uint32_t kYears = 30;

/// The run under measurement. Both default to the canonical configuration —
/// which is what ctest runs, and the only one the gates below bind on — and
/// both can be overridden on the command line: `thirty_years [seed] [years]`.
///
/// It earned the argument the day the weather gained memory. "Does the
/// village survive an unlucky first year" is a question about THREE seeds
/// out of twenty, and answering it by editing a constant and rebuilding is
/// how a measurement stops being repeatable
/// (69-reconciliation.md §13.12).
std::uint32_t g_years = kYears;
bool g_canonical_run = true;

/// `--free-materials`: the building materials never run out. NOT A GAME
/// SETTING and never shipped — a control arm for one question, and the
/// question is what this run has been measuring for thirty years.
///
/// The farm makes no logs. Nothing in the tables does: timber is taken by an
/// ORDER (design, timber §2 — felling trees outside the forest is the farm's
/// own channel), and that order does not exist in the core yet. So the start
/// stock of the manor ruins is all the timber there will ever be: 138 tonnes
/// of it, spent by the sixth year, and flat at seven tonnes for the
/// twenty-four years after that (tests/run/store_build_time). From year six
/// this run measures a village that CANNOT BUILD — and it looked healthy,
/// because the residents went on being counted.
///
/// With the flag the materials are topped up daily in a heap bounded by its
/// own outline (capacity_by_plot), so that free timber cannot take room from
/// the grain and turn one measurement into two.
bool g_free_materials = false;

/// `--tables=<dir>`: read the table set from somewhere other than `tables`.
/// The harness has taken the path as a parameter since the day it was
/// written — "a doctored run passes its own copy instead" — and this is the
/// argument that reaches it. Like --free-materials it takes the run out of
/// the canonical set, so no gate binds on a doctored table set.
std::string g_tables_dir = "tables";

constexpr std::uint64_t kSeed = 1929;
std::uint64_t g_seed = kSeed;

// -- THE OTHER TWO HALVES OF THE PHASE GATE ------------------------------
//
// The hard half of the phase-two criterion is four lines
// (manual/process/70-phase2-plan.md §1). One of them — "no UE headers" — is
// grep, and one — determinism — got its own run today. The remaining two
// were read off a document by whoever remembered to look, which is the same
// state determinism was in this morning: A LINE OF A CRITERION WITH NO
// EXECUTABLE CARRIER IS AN INTENTION (boss, 2026-09-04).
//
// They live here rather than in a run of their own because this is where
// the numbers they judge are produced. A gate checked somewhere other than
// where its quantity appears is the failure this run already carries a scar
// from — the house placed at a door the placing code never came through.

/// The canon's thirtieth year: 1400-1500 residents (CLAUDE.md §9 "growth
/// targets", phase-two plan §1). THE FLOOR IS HARD — nothing in the run
/// excuses a village that failed to grow.
constexpr std::size_t kCanonLow = 1400;

/// The canon's top. Printed always, asserted only once the caveat below is
/// gone.
constexpr std::size_t kCanonTop = 1500;

/// THE CEILING ACTUALLY IN FORCE, and it is wider than the canon's for one
/// written-down reason, not for want of a tighter number.
///
/// Every wedding in this run is handed a house for free
/// (core_residents/housing.h, STUB), which cancels the canon's brake "no
/// free house, no wedding" (life-cycle §12). Boss's caveat, made on the
/// fifth reconciliation pass and repeated on the eleventh: the run's curve
/// is an UPPER ESTIMATE of the real one, the excess over the canon is
/// explained by that stub ENTIRELY, and no other number is to be bent to
/// it (69-reconciliation.md §11).
///
/// So the ceiling is derived, not chosen: the canon top times the widest
/// excess the caveat has ever had to explain — 1608 against 1500, +7.2%, on
/// the pass that stated the caveat. A run above THAT is not the stub any
/// more, and the caveat stops covering it.
///
/// The constant has an owner and an end. When the player builds the houses,
/// the stub goes and this line goes with it, and the check tightens to
/// kCanonTop by deletion rather than by somebody remembering.
///
/// AND THE ONE THING THAT MAY NOT BE DONE WITH IT: IT NEVER RISES BECAUSE A
/// RUN EXCEEDED IT (boss, 2026-09-04). The 7.2% is one observation — how far
/// the stub inflated the curve in that pass, at that seed, on that layout —
/// and not a law. A pass that comes out above 1608 is a FINDING; moving the
/// constant to admit it is the quietest way there is to kill the gate. Each
/// pass would nudge the ceiling by its own excess, each time "explained",
/// and in half a year the band would pass anything while still looking
/// strict. It is the rule "a reference is not rewritten from a run",
/// applied to the tolerance instead of to the number.
///
/// A constant that says how it was derived but not what may not be done
/// with it is half guarded.
///
/// AND SINCE 2026-09-04 IT IS NO LONGER ASSERTED. It is printed, with the
/// measured noise beside it, and nothing fails on it. Boss's decision, and
/// it came out of the measurement below: the ceiling stands 1606 to 1608
/// away from the run, which is TWO residents, which is inside the noise of
/// the instrument that produced both numbers. An assertion whose edge sits
/// inside the noise fails on correct work — the next lawful pass reads 1620
/// for no reason at all, and the run teaches the village to switch it off.
/// The floor keeps its assertion: 1400 stands two hundred clear of the
/// noise, and that reading is unambiguous.
///
/// WHAT THIS COSTS, SAID PLAINLY: the population gate ASSERTS downward only.
/// Re-measured 2026-09-05, on the tree as it stands — cloud swing in, no
/// weather memory, baseline 1561 — births scaled by:
///
///   x1.50  3211   prints as a FINDING, fails nothing
///   x1.15  1859   prints as a FINDING: above the 1608 ceiling by 251, four
///                 times the noise. It is READ again, though still not
///                 asserted
///   x0.85  1210   fails the floor by 190, three noises
///   x0.80  1034   fails the floor by 366, six noises
///
/// EVERY ONE OF THOSE NUMBERS MOVED when the cloud swing went in, and the
/// previous set is kept in 69-reconciliation.md §13.9 as the record of the
/// world it was taken in. x1.15 read 1591 then — fifteen residents, inside
/// the noise, caught by nothing; it reads 251 clear of the ceiling now. The
/// lesson is not about clouds: DATE A MUTATION BY THE STATE OF THE MODEL,
/// not only by the seed.
///
/// A mutation whose effect is smaller than the resolution proves nothing by
/// firing OR by staying silent, which is why the magnitude is chosen above
/// the noise and the noise is measured before the first mutation.
///
/// And the honest statement of what this half of the gate is: it catches a
/// village that failed to grow, and it prints everything else.
constexpr std::size_t kStubCeiling = 1608;

/// THE INSTRUMENT'S OWN RESOLUTION, measured rather than estimated
/// (69-reconciliation.md §13.8, 2026-09-04).
///
/// The thirty-year curve is chaotic in the ordinary sense: a change that
/// touches nothing causally still moves the thirtieth year. Measured by a
/// discriminator — the same policy, the same 320 repairs, the same 481
/// man-days, with ONE demolition order moved from year 5 to year 6: 1606
/// became 1545. Sixty-one residents, from a change that has no channel to
/// the population at all.
///
/// It is not derived from a series of readings — the series is the thing it
/// explains. Three points that line up in order are a coincidence until
/// somebody measures how many points the instrument can line up by itself.
///
/// What it is for: any comparison of two runs by population that is finer
/// than this is reading noise. It is printed beside the ceiling so that the
/// next person who wants to attribute a cause meets it before their series.
constexpr std::size_t kMeasuredNoiseResidents = 60;

/// Fast-forward: a game day in at most two seconds with nothing drawn
/// (phase-two plan §1). NOT a pin on this machine's speed — the measured
/// figure is around 0.05 s/day, so the norm sits a factor of forty away and
/// a busy host cannot trip it. What it catches is the thing the norm is
/// there for: a step that became forty times dearer.
constexpr double kDayBudgetSeconds = 2.0;

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
         ("fields_" + std::to_string(g_seed) + ".csv");
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
         ("ledger_" + std::to_string(g_seed) + ".csv");
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
    // No diagnosis is printed here on purpose: the last one this line
    // carried ("a private yard's animals do not breed") outlived its truth
    // by a stage and sent the next reader the wrong way. The ledger has the
    // columns that tell the story — the herd's hunger deaths, the hay
    // harvest and store, the walk-offs — and that is where to look.
    std::cout << "thirty_years: FINDING — a whole herd is gone. Read the ledger sheet: the "
                 "herd_deaths_* columns say whether it starved or aged out, harvest_hay_kg and "
                 "store_hay_kg whether the fodder held, and walk_offs whether the hands did.\n";
  }
}

/// @brief Every resource any build recipe names, by row of resources.csv.
std::vector<std::uint32_t> MaterialRows(const core::ITableSet& tables) {
  std::vector<std::uint32_t> rows;
  const core::ITable* const costs = tables.FindTable("unit_level_cost");
  const core::ITable* const resources = tables.FindTable("resources");
  if (costs == nullptr || resources == nullptr) {
    return rows;
  }
  const std::uint32_t column = costs->FindColumn("resource");
  if (column == core::kNoTableColumn) {
    return rows;
  }
  for (std::uint32_t row = 0; row < costs->RowCount(); ++row) {
    const std::uint32_t resource = resources->FindRowByKey(costs->CellText(row, column));
    if (resource != core::kNoTableRow && std::ranges::find(rows, resource) == rows.end()) {
      rows.push_back(resource);
    }
  }
  return rows;
}

/// @brief The first heap bounded by its own outline: a store with no ceiling
/// and nothing to overflow. The materials are stacked THERE and not in a
/// granary, so that the control arm does not also take room from the grain.
std::uint32_t OutlineHeapRow(const core::WorldState& world, const core::ITableSet& tables) {
  const core::ITable* const types = tables.FindTable("unit_types");
  const std::uint32_t column =
      types == nullptr ? core::kNoTableColumn : types->FindColumn("capacity_by_plot");
  if (column == core::kNoTableColumn) {
    return core::kNoRow;
  }
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    const core::UnitRow& unit = world.units.rows[row];
    if (unit.level > 0 && unit.type.value < types->RowCount() &&
        types->CellText(unit.type.value, column) == "1") {
      return row;
    }
  }
  return core::kNoRow;
}

}  // namespace

int main(int argc, char** argv) {
  int failures = 0;
  if (argc > 1) {
    g_seed = std::strtoull(argv[1], nullptr, 10);
  }
  if (argc > 2) {
    g_years = static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 10));
  }
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    g_free_materials = g_free_materials || argument == "--free-materials";
    if (argument.starts_with("--tables=")) {
      g_tables_dir = std::string(argument.substr(std::string_view("--tables=").size()));
    }
  }
  // THE GATES BIND ON THE CANONICAL RUN AND NOWHERE ELSE. A band measured
  // for thirty years of one seed says nothing about five years of another,
  // and a check that fires there would be measuring the argument rather than
  // the simulation.
  g_canonical_run =
      g_seed == kSeed && g_years == kYears && !g_free_materials && g_tables_dir == "tables";
  run::Simulation world = run::Start(g_seed, 1, g_tables_dir);
  if (!world) {
    return 1;
  }

  // THE CONTROL ARM IS SET ONCE, BEFORE THE FIRST DAY, and then the run is
  // the run. A daily top-up would have meant copying and resetting the whole
  // world every day, and a control arm that reaches into the world 1440
  // times is not the same experiment as one that changes a starting number.
  if (g_free_materials) {
    const std::vector<std::uint32_t> material_rows = MaterialRows(*world.tables);
    core::WorldState stocked = world.State();
    const std::uint32_t heap = OutlineHeapRow(stocked, *world.tables);
    if (heap == core::kNoRow || material_rows.empty()) {
      std::cout << "FAIL: --free-materials found no outline heap to stack them in\n";
      return 1;
    }
    for (const std::uint32_t resource : material_rows) {
      core::ResourceAmounts& stock = stocked.units.rows[heap].stock;
      if (resource >= stock.size()) {
        stock.resize(resource + 1, 0);
      }
      // Ten thousand tonnes of each. A granary is twelve tonnes of timber,
      // so this is a thousand granaries: the arm cannot run short, which is
      // the whole of what it is for.
      stock[resource] = 10'000'000 * core::kGramsPerKilogram;
    }
    world.simulation->ResetWorld(stocked);
    std::cout << "thirty_years: CONTROL ARM — " << material_rows.size()
              << " build materials stacked without limit in the outline heap at row " << heap
              << ". NOT the canonical run, no gate binds, and nothing of this ships\n";
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
  double harvested_kcal = 0.0;
  // Caloric density by ResourceId, read here rather than taken from the
  // core: a second tally that shares the core's code would be one tally.
  std::vector<double> food_value;
  if (const core::ITable* resources = world.tables->FindTable("resources")) {
    const std::uint32_t column = resources->FindColumn("kcal_per_gram");
    food_value.assign(resources->RowCount(), 0.0);
    if (column != core::kNoTableColumn) {
      for (std::uint32_t row = 0; row < resources->RowCount(); ++row) {
        const std::optional<float> cell = resources->CellReal(row, column);
        food_value[row] = cell && *cell > 0.0F ? static_cast<double>(*cell) : 0.0;
      }
    }
  }
  double eaten_tonnes = 0.0;
  std::uint32_t rows_written = 0;
  std::uint16_t last_year = 0;
  double hungry_head_days = 0.0;
  double lived_head_days = 0.0;
  std::uint32_t years_without_plowing = 0;
  float lowest_fertility = 100.0F;
  // Every man-day the settlement worked, of every kind, so the price of
  // repair can be given as a SHARE and not as a number nobody can size.
  double total_work_days = 0.0;

  std::ofstream field_sheet(FieldSheetPath(), std::ios::binary | std::ios::trunc);
  field_sheet << "year,field,kind,area_ha,crop,manured,fertility,stress_july\n";
  std::vector<FieldSample> sampled;

  // The fast-forward measure. Only the game's own day is on the clock: the
  // step and the chairman's orders. The sheets, the samples and the
  // printing are the instrument's cost, not the game's, and charging them
  // to the norm would make the norm say something else.
  // Averaged over a YEAR and not taken as a single worst day: ctest runs
  // this alongside the rest of the suite, and one contended day would be a
  // spike, not a regression. Forty-eight days of a growing village is a
  // measure that only moves when the step does.
  double costliest_year_day_seconds = 0.0;
  std::uint32_t costliest_year = 0;
  double simulated_seconds = 0.0;

  // The chairman, for the one decision the start cannot do without: build
  // the yard, take it to the stable step, appoint a groom. The core used to
  // do this itself in a stub; a run plays the player now (yard_policy.h).
  run::YardPolicy yard(*world.tables);
  // And somewhere to put the harvest. The canon gives the village no granary
  // — that is the game, and the run says so out loud rather than tabling it
  // away (granary_policy.h).
  run::FixturePolicy fixture(*world.tables);
  run::FixturePolicy::Declare();
  // And the two verbs the runs had never said: a standing work order and a
  // pause (orders_policy.h). A verb the run does not say is not checked by
  // the run, however many unit tests stand behind it (boss, 2026-09-04).
  run::OrdersPolicy orders;
  // And the two verbs that were silent for no reason but silence: repair
  // and demolition (repair_policy.h). Every wear number in the tables was
  // chosen by argument on 2026-09-03 and never played once.
  run::RepairPolicy repairs(*world.tables);
  run::RepairPolicy::Declare();

  std::uint64_t growing_field_days = 0;
  std::uint64_t drying_field_days = 0;
  std::uint64_t soaking_field_days = 0;

  for (std::uint32_t year = 0; year < g_years; ++year) {
    double year_seconds = 0.0;
    for (std::uint32_t day = 0; day < core::kDaysPerYear; ++day) {
      const std::chrono::steady_clock::time_point day_began = std::chrono::steady_clock::now();
      run::AdvanceDays(*world, 1);
      yard.RunDay(*world.simulation);
      fixture.RunDay(*world.simulation);
      orders.RunDay(*world.simulation);
      repairs.RunDay(*world.simulation);
      year_seconds +=
          std::chrono::duration<double>(std::chrono::steady_clock::now() - day_began).count();
      const core::WorldState& mid = world.State();
      // THE WEATHER JUDGEMENT, COUNTED RATHER THAN ADMIRED. A state that is
      // on for a third of every growing day says nothing, and one that never
      // fires in thirty years says nothing either; the threshold behind it
      // (farming.csv weather_state_days) was picked off these three counts
      // and not off taste.
      for (const core::FieldRow& row : mid.fields.rows) {
        if (row.phase != core::FieldPhase::kGrowing || row.kind != core::LandKind::kArable) {
          continue;
        }
        ++growing_field_days;
        if (row.weather_state == core::FieldWeatherState::kDrying) {
          ++drying_field_days;
        } else if (row.weather_state == core::FieldWeatherState::kSoaking) {
          ++soaking_field_days;
        }
      }
      if (mid.calendar.date.month == core::Month::kJuly && mid.calendar.date.day_in_month == 0) {
        sampled.assign(mid.fields.rows.size(), FieldSample{});
        for (std::size_t field = 0; field < mid.fields.rows.size(); ++field) {
          sampled[field].crop = CropKey(*world.tables, mid.fields.rows[field].crop);
          sampled[field].manured = mid.fields.rows[field].manure_applied != 0;
          sampled[field].stress =
              mid.fields.rows[field].drought_stress + mid.fields.rows[field].wet_stress;
        }
      }
    }
    // A day of the year that has just been lived, not of the whole run: the
    // village grows all the way through, so the run's mean would hide the
    // only year that matters — the last and largest.
    const double day_seconds = year_seconds / static_cast<double>(core::kDaysPerYear);
    simulated_seconds += year_seconds;
    if (day_seconds > costliest_year_day_seconds) {
      costliest_year_day_seconds = day_seconds;
      costliest_year = year + 1;
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
    // The wall's third sheet, added up a SECOND time and by another road:
    // the core reads the density inside core_world and writes kcal at the
    // rotation; this reads the same column off the table set and sums the
    // same books. Two tallies of one fact — the trick that has caught more
    // than the checks it was written for.
    for (std::size_t index = 0; index < state.ledger.closed.harvest.size(); ++index) {
      const core::Grams grams = state.ledger.closed.harvest[index];
      const double density = index < food_value.size() ? food_value[index] : 0.0;
      if (grams > 0 && density > 0.0) {
        harvested_kcal += static_cast<double>(grams) * density;
      }
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
    for (const float days : state.ledger.closed.work_days_by_kind) {
      total_work_days += static_cast<double>(days);
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
  failures += run::Expect(rows_written == g_years, "one row per year of the run");
  failures += run::Expect(state.ledger.closed.year == g_years,
                          "the last closed book is the last full year");
  failures += run::Expect(state.families.rows.size() > 20, "and it has households");

  // -- THE PHASE GATE, HALF ONE: the run still converges with the canon ----
  const std::size_t population = state.residents.rows.size();
  std::cout << "gate: " << population << " residents in the thirtieth year — canon " << kCanonLow
            << "-" << kCanonTop << ", floor asserted; stub ceiling " << kStubCeiling
            << " OBSERVED not asserted, measured noise ±" << kMeasuredNoiseResidents << '\n';
  if (population > kCanonTop) {
    // SAID OUT LOUD EVERY TIME, not only when it fails. A run that sits
    // above the canon on a caveat must keep saying which caveat, or the
    // green light gets read as agreement with the canon itself.
    std::cout << "gate: above the canon top by " << (population - kCanonTop)
              << " — the free-housing stub (core_residents/housing.h) is what the caveat "
                 "covers; when the player builds the houses this must come back under "
              << kCanonTop << '\n';
  }
  if (population > kStubCeiling) {
    // A FINDING, and it says so in the word the caveat's owner used. It does
    // not fail the run and it does not move the constant: both of those
    // would end the observation, one loudly and one quietly.
    std::cout << "gate: FINDING — above the stub ceiling " << kStubCeiling << " by "
              << (population - kStubCeiling) << ", which is "
              << (population - kStubCeiling > kMeasuredNoiseResidents ? "MORE" : "less")
              << " than the instrument's own ±" << kMeasuredNoiseResidents
              << "; the caveat covers the excess over the canon, not this\n";
  }
  if (g_canonical_run) {
    failures += run::Expect(population >= kCanonLow,
                            "the village reaches the canon's thirtieth year and not a smaller one");
  } else {
    std::cout << "gate: not the canonical run (seed " << g_seed << ", " << g_years
              << " years) — the canon band is printed and not asserted\n";
  }

  // -- THE PHASE GATE, HALF TWO: fast-forward holds the norm ---------------
  std::cout << "gate: a game day of the costliest year cost " << costliest_year_day_seconds
            << " s (year " << costliest_year << "), the run averaged "
            << simulated_seconds / static_cast<double>(g_years * core::kDaysPerYear)
            << " s/day against a budget of " << kDayBudgetSeconds << " s\n";
  // A STOPPED CLOCK IS UNDER EVERY BUDGET. The measure has to prove it
  // measured before its verdict means anything — the same reason the
  // determinism run counts the residents it agreed on.
  failures +=
      run::Expect(simulated_seconds > 0.0, "the fast-forward measure actually timed something");
  failures += run::Expect(costliest_year_day_seconds <= kDayBudgetSeconds,
                          "and no year of the run cost more per game day than the norm allows");
  // The speed norm binds on every run: it is a property of the machine and
  // of the step, not of the seed or of how many years were asked for.

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
  {
    const auto share = [growing_field_days](std::uint64_t part) {
      return growing_field_days == 0
                 ? 0.0
                 : 100.0 * static_cast<double>(part) / static_cast<double>(growing_field_days);
    };
    std::cout << "weather: of " << growing_field_days << " growing field-days, "
              << drying_field_days << " were called drying (" << share(drying_field_days)
              << "%) and " << soaking_field_days << " soaking (" << share(soaking_field_days)
              << "%)\n";
  }
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

  // THE POSTS (task A7). The yard used to appear out of nothing at the turn
  // of the second year, in a stub that stood in for the player; now the run
  // plays him (yard_policy.h). What is measured is the whole chain — an
  // order built the yard, an order appointed a groom, and the herd day
  // moved the team — because any broken link in it looks exactly like the
  // old failure and is caught by the plough check three lines above only
  // AFTER thirty years of it.
  failures += run::Expect(state.chairman.horses_stabled != 0,
                          "the chairman's yard was built, a groom was appointed, and the team "
                          "came in off the private yards");
  std::uint32_t horses_at_yards = 0;
  for (const core::HerdRow& herd : state.herds.rows) {
    if (herd.household_owned == 0 && herd.household.value != core::kInvalidEntityIdValue) {
      ++horses_at_yards;
    }
  }
  failures +=
      run::Expect(horses_at_yards == 0, "and no kolkhoz herd is billeted on a household any more");
  // Nobody need still HOLD the post at the end: the run's chairman stops
  // watching the day the team comes in, and thirty years is four grooms'
  // lifetimes. A post whose holder dies simply empties — measured here on
  // purpose, because the first draft of this check asserted the opposite and
  // was wrong about the model rather than about the run.
  fixture.Report(state);
  failures += orders.Report();
  failures += repairs.Report(state);
  std::cout << "repair: that is " << (repairs.LaborDays() / total_work_days * 100.0) << "% of the "
            << total_work_days << " man-days the settlement worked in thirty years\n";
  // A LIMIT THAT BINDS MUST SAY SO. A run that quietly starves a village
  // against a ceiling is an argument, not a measurement (boss, 2026-09-03).
  core::Grams lost_to_room = 0;
  for (const core::Grams lost : state.ledger.closed.lost_no_room) {
    lost_to_room += lost;
  }
  if (lost_to_room > 0) {
    std::cout << "thirty_years: STORAGE STILL BINDS — the last year lost "
              << static_cast<double>(lost_to_room) / 1.0e6 << " t for want of room\n";
  }
  // NO TWO PLOTS OVERLAP — read off the WORLD, not off the door the rule
  // used to be guarded at. The wedding stub appended its house rows
  // straight into the table and never went past kTooClose, so the village
  // came out as stacks of houses at one coordinate (boss, 2026-09-04).
  // A property of the state is checked over the state.
  core::Definitions definitions;
  std::string catalog_error;
  // kRefused, like the run itself: this reads the catalogue of the SAME full
  // table set the village was built on, so a missing table here would mean
  // the run had already been assembled on one.
  core::LoadDefinitions(*world.tables, core::StubTables::kRefused, definitions, catalog_error);
  const core::PlotRules plot_rules = definitions.Plots();
  const std::span<const float> plot_radii = definitions.units.plot_radius_m;
  std::uint32_t overlapping = 0;
  for (std::uint32_t row = 0; row < state.units.rows.size(); ++row) {
    const core::UnitRow& unit = state.units.rows[row];
    const float radius = unit.type.value < plot_radii.size() ? plot_radii[unit.type.value] : 0.0F;
    if (!(radius > 0.0F)) {
      continue;
    }
    if (core::PlotOverlaps(
            state.units, plot_rules, unit.position, radius, state.units.row_ids[row])) {
      ++overlapping;
    }
  }
  // AND HOW FAR THE VILLAGE HAD TO SPREAD TO HOLD THEM. Plots that may not
  // overlap take room, and room is the walk to the fields — the cost of the
  // fix, named rather than left to be discovered (boss, 2026-09-04).
  core::Vec2 centre{.x = 0.0F, .y = 0.0F};
  std::uint32_t housed = 0;
  for (const core::UnitRow& unit : state.units.rows) {
    if (unit.household.value == core::kInvalidEntityIdValue) {
      continue;
    }
    centre.x += unit.position.x;
    centre.y += unit.position.y;
    ++housed;
  }
  float spread = 0.0F;
  if (housed != 0) {
    centre.x /= static_cast<float>(housed);
    centre.y /= static_cast<float>(housed);
    for (const core::UnitRow& unit : state.units.rows) {
      if (unit.household.value == core::kInvalidEntityIdValue) {
        continue;
      }
      const float dx = unit.position.x - centre.x;
      const float dy = unit.position.y - centre.y;
      spread = std::max(spread, std::sqrt((dx * dx) + (dy * dy)));
    }
  }
  std::cout << "thirty_years: " << state.units.rows.size() << " units stand, " << overlapping
            << " of them on somebody else's plot; " << housed << " lived-in houses reach " << spread
            << " m from the village centre\n";
  failures += run::Expect(overlapping == 0, "no two plots overlap after thirty years of weddings");

  std::uint32_t posts_held = 0;
  for (const core::ResidentRow& resident : state.residents.rows) {
    posts_held += resident.post.profession.value != core::kInvalidDefIdValue ? 1 : 0;
  }
  std::cout << "thirty_years: " << posts_held << " posts still held at the end\n";

  // WEAR OVER THIRTY YEARS (task A5). The criterion is not a number but a
  // shape: what is worked wears, what merely stands waits its turn, and
  // nothing quietly disappears except the start's old houses, which the
  // canon lets fall. Measured here rather than eyeballed, because "the wear
  // looks plausible" is exactly the sort of claim that stops being true
  // without anybody noticing.
  std::uint32_t worn_units = 0;
  std::uint32_t ruins = 0;
  std::uint32_t wearless = 0;
  float highest_wear = 0.0F;
  double wear_sum = 0.0;
  for (const core::UnitRow& unit : state.units.rows) {
    if (unit.level == 0) {
      continue;
    }
    if (unit.wear > 0.0F) {
      ++worn_units;
      wear_sum += static_cast<double>(unit.wear);
      highest_wear = unit.wear > highest_wear ? unit.wear : highest_wear;
      ruins += unit.wear >= 100.0F ? 1 : 0;
    } else {
      ++wearless;
    }
  }
  const double mean_wear = worn_units == 0 ? 0.0 : wear_sum / static_cast<double>(worn_units);
  std::cout << "thirty_years: " << worn_units << " units carry wear (mean " << mean_wear
            << "%, worst " << highest_wear << "%, " << ruins << " at the ruin mark), " << wearless
            << " have none to carry\n";

  failures += run::Expect(worn_units > 0,
                          "thirty years of standing and working leave a mark on the buildings");
  failures += run::Expect(wearless > 0,
                          "and the heaps and stacks carry none: no building, nothing to wear");
  failures += run::Expect(highest_wear <= 100.0F, "wear never passes the ruin mark");
  failures += run::Expect(mean_wear > 1.0,
                          "the village is not brand new after three decades without a repair");

  // The two halves of the food year must both be real. A run where nothing
  // is harvested, or nothing is eaten, would still satisfy every count above
  // — and both have happened during this phase's development.
  failures += run::Expect(harvested_tonnes > 100.0, "the fields produced over the run");

  // -- THE OFFICE WALL: one row per closed year, and the same numbers ------
  //
  // The chronicle is written at the rotation from the closed book; this run
  // sums the same books itself, year by year, as it goes. Two tallies of one
  // fact again — the trick that turned out stronger than the acceptance it
  // was written for (the event journal, 0.17.18) — and it catches the two
  // ways a series like this dies: a row that never gets appended, and a row
  // appended twice.
  const core::Chronicle& wall = state.ledger.chronicle;
  failures += run::Expect(wall.size() == g_years, "the wall carries one row per closed year");
  double wall_kcal = 0.0;
  for (const core::ChronicleYear& year : wall) {
    wall_kcal += static_cast<double>(year.harvest_kcal);
  }
  // Within a kilocalorie a year: the core truncates each year to a whole
  // kcal, this run does not.
  failures += run::Expect(std::abs(wall_kcal - harvested_kcal) < static_cast<double>(g_years),
                          "and the same food off the arable the run added up itself");
  if (!wall.empty()) {
    failures += run::Expect(
        wall.back().year == static_cast<std::uint16_t>(g_years) && wall.front().year == 1,
        "the years on the wall run from the first to the last, in order");
    // The residents on the last row are read at the rotation, which is the
    // last tick of the run — the same population the gate above judged.
    failures += run::Expect(wall.back().residents == population,
                            "and the last row's people are the people the gate counted");
    std::cout << "wall: " << wall.size() << " years — year 1 " << wall.front().residents
              << " residents, fertility " << wall.front().fertility << ", harvest "
              << static_cast<double>(wall.front().harvest_kcal) / 1.0e9 << " Gkcal; year "
              << wall.back().year << " " << wall.back().residents << " residents, fertility "
              << wall.back().fertility << ", harvest "
              << static_cast<double>(wall.back().harvest_kcal) / 1.0e9 << " Gkcal\n";
  }
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
