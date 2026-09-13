// Simulation run: DO THE NUMBERS OF TIMBER DESIGN §8a HOLD? Criteria 2..4.
//
// Boss's order of 2026-09-13 (parcel 194), on 0.19.0: nine seeds, thirty
// years, "отчёт числами; где критерий нарушен — назови ручку, а не правь её".
//
//   2  One winter does not close the year's need — how many logs one winter
//      of the start's people can fell, against the logs the building takes
//      in a year.
//   3  Felling outside the forest pays better than waiting for old trunks,
//      and old trunks are not pointless — the share of logs from groves and
//      belts against old forest, year by year.
//   4  The groves do not vanish in five years — the stock of groves and belts
//      left at years 5 and 10.
//
// NO GATE. It measures criteria whose numbers are boss's to judge; a gate
// would be a threshold nobody named. The village is steered exactly as in
// plan_shortfall: the yard, the building chairman, the felling chairman, the
// repairs and the obvious chairman on the land.
//
// Usage: timber_years [seed] (default 1929).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "../common/felling_policy.h"
#include "../common/fixture_policy.h"
#include "../common/repair_policy.h"
#include "../common/run_harness.h"
#include "../common/sowing_policy.h"
#include "../common/yard_policy.h"
#include "core_catalog/timber_catalog.h"
#include "core_common/calendar.h"
#include "core_common/labor_state.h"
#include "core_common/timber_state.h"
#include "core_common/world_state.h"

namespace {

constexpr std::uint32_t kYears = 30;

/// The obvious chairman's agronomy, as in plan_trial and plan_shortfall.
constexpr std::int32_t kRipenDays = 13;
constexpr std::uint32_t kSeasonLastDay = 42;

/// Adult age for "a hand" (life.csv adult_age_years), mirrored as the other
/// runs mirror it.
constexpr float kAdultAgeYears = 16.0F;

double Logs(core::Grams grams, core::Grams log_grams) {
  return log_grams > 0 ? static_cast<double>(grams) / static_cast<double>(log_grams) : 0.0;
}

/// Everything the village holds of logs: every unit's stock (sites included)
/// and every load lying on a stand.
core::Grams LogsHeld(const core::WorldState& world, core::ResourceId log) {
  core::Grams held = 0;
  for (const core::UnitRow& unit : world.units.rows) {
    held += log.value < unit.stock.size() ? unit.stock[log.value] : 0;
  }
  for (const core::TimberStandRow& stand : world.stands.rows) {
    held += stand.load_grams;
  }
  return held;
}

double GroveStock(const core::WorldState& world) {
  double stock = 0.0;
  for (const core::TimberStandRow& stand : world.stands.rows) {
    stock +=
        stand.kind == core::TimberStandKind::kForestOld ? 0.0 : static_cast<double>(stand.stock_m3);
  }
  return stock;
}

struct YearTally {
  core::Grams logs_from_groves = 0;  ///< logs laid down on groves and belts
  core::Grams logs_from_old = 0;     ///< logs laid down on old forest
  core::Grams logs_used = 0;         ///< logs that left the village's hands
  float felling_man_days = 0.0F;
};

}  // namespace

int main(int argc, char** argv) {
  const std::uint64_t seed = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1929;
  run::Simulation started = run::Start(seed);
  if (!started) {
    return 1;
  }
  core::TimberCatalog catalog;
  std::string error;
  if (!core::ParseTimberCatalog(*started.tables, catalog, error)) {
    std::cout << "FAIL: timber catalogue: " << error << "\n";
    return 1;
  }
  float life_speedup = 4.0F;
  if (const core::ITable* const life = started.tables->FindTable("life")) {
    life_speedup = life->CellReal(life->FindRowByKey("life_speedup"), life->FindColumn("value"))
                       .value_or(4.0F);
  }
  run::YardPolicy yard(*started.tables);
  run::FixturePolicy fixture(*started.tables);
  run::FellingPolicy felling(*started.tables);
  run::RepairPolicy repairs(*started.tables);
  run::SowingPolicy chairman(kRipenDays, kSeasonLastDay, false, started.tables.get());
  run::FellingPolicy::Declare("timber_years");

  // CRITERION 2, THE BOUND: one winter of the start's people. A felled cubic
  // metre costs timber_felling_days_per_m3 game man-days; the crew is capped
  // by whole tools in the stores; the winter is December to February, less
  // the rest days. It is an upper bound — a winter day is short and the
  // accountant sends people elsewhere too — and it is printed as one.
  {
    const core::WorldState& world = started.State();
    std::uint32_t adults = 0;
    for (const core::ResidentRow& resident : world.residents.rows) {
      adults += core::BiologicalAgeYears(life_speedup, resident.birth_day, world.calendar.day) >=
                        kAdultAgeYears
                    ? 1U
                    : 0U;
    }
    core::Grams tools = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      tools += catalog.tool_resource.value < unit.stock.size()
                   ? unit.stock[catalog.tool_resource.value]
                   : 0;
    }
    const std::uint32_t crew = std::min(adults, core::FellingCrewCap(catalog, tools));
    constexpr double kWinterDays = 3.0 * core::kDaysPerMonth;
    constexpr double kWorkShare = 6.0 / 7.0;  // one rest day a week
    const double man_days = static_cast<double>(crew) * kWinterDays * kWorkShare;
    const double m3 = catalog.felling_days_per_m3 > 0.0F
                          ? man_days / static_cast<double>(catalog.felling_days_per_m3)
                          : 0.0;
    constexpr double kGroveShare = 0.25;  // part: every grove and belt of the shipped bake
    const double logs =
        catalog.log_m3 > 0.0F ? m3 * kGroveShare / static_cast<double>(catalog.log_m3) : 0.0;
    std::cout << "timber_years: seed " << seed << ": ONE WINTER, upper bound — " << adults
              << " adults, crew capped at " << crew << " by tools, " << std::lround(man_days)
              << " man-days fell " << std::lround(m3) << " m3 = " << std::lround(logs)
              << " logs of a grove\n";
  }

  std::vector<YearTally> years(kYears);
  std::vector<core::TimberStandRow> yesterday = started.State().stands.rows;
  core::Grams held_at_year_start = LogsHeld(started.State(), catalog.log_resource);
  double grove_start = GroveStock(started.State());
  std::array<double, 3> grove_at = {0.0, 0.0, 0.0};  // years 5, 10, 30

  for (std::uint32_t year = 0; year < kYears; ++year) {
    YearTally& tally = years[year];
    for (std::uint32_t day = 0; day < core::kDaysPerYear; ++day) {
      run::AdvanceDays(*started, 1);
      yard.RunDay(*started.simulation);
      fixture.RunDay(*started.simulation);
      felling.RunDay(*started.simulation);
      repairs.RunDay(*started.simulation);
      chairman.RunDay(*started.simulation);
      const core::WorldState& world = started.State();
      // LOGS LAID DOWN are what a stand's load grew by; the carting only ever
      // takes a load down, so a rise is a felling and nothing else.
      for (std::size_t row = 0; row < world.stands.rows.size() && row < yesterday.size(); ++row) {
        const core::TimberStandRow& now = world.stands.rows[row];
        const core::TimberStandRow& before = yesterday[row];
        if (now.load_grams > before.load_grams) {
          const core::Grams laid = now.load_grams - before.load_grams;
          (now.kind == core::TimberStandKind::kForestOld ? tally.logs_from_old
                                                         : tally.logs_from_groves) += laid;
        }
      }
      yesterday = world.stands.rows;
      if (world.calendar.day % core::kDaysPerYear == core::kDaysPerYear - 1) {
        const core::Grams held = LogsHeld(world, catalog.log_resource);
        tally.logs_used = held_at_year_start + tally.logs_from_groves + tally.logs_from_old - held;
        held_at_year_start = held;
        const float felling_days =
            world.ledger.current
                .work_days_by_kind[static_cast<std::size_t>(core::WorkKind::kFelling)];
        tally.felling_man_days = felling_days;
      }
    }
    if (year + 1 == 5) {
      grove_at[0] = GroveStock(started.State());
    }
    if (year + 1 == 10) {
      grove_at[1] = GroveStock(started.State());
    }
  }
  grove_at[2] = GroveStock(started.State());

  double used_total = 0.0;
  double groves_total = 0.0;
  double old_total = 0.0;
  double max_used = 0.0;
  double max_felled = 0.0;
  for (std::uint32_t year = 0; year < kYears; ++year) {
    const YearTally& tally = years[year];
    const double from_groves = Logs(tally.logs_from_groves, catalog.log_grams);
    const double from_old = Logs(tally.logs_from_old, catalog.log_grams);
    const double used = Logs(tally.logs_used, catalog.log_grams);
    used_total += used;
    groves_total += from_groves;
    old_total += from_old;
    max_used = std::max(max_used, used);
    max_felled = std::max(max_felled, from_groves + from_old);
    std::cout << "timber_years:   year " << (year + 1) << " — logs felled: groves and belts "
              << std::lround(from_groves) << ", old forest " << std::lround(from_old)
              << "; logs used " << std::lround(used) << "; felling man-days "
              << tally.felling_man_days << "\n";
  }
  const double felled_total = groves_total + old_total;
  std::cout << "timber_years: seed " << seed << ": THIRTY YEARS — logs used "
            << std::lround(used_total) << " (worst year " << std::lround(max_used) << "), felled "
            << std::lround(felled_total) << " (most in a year " << std::lround(max_felled)
            << "); from old forest "
            << (felled_total > 0.0 ? std::lround(100.0 * old_total / felled_total) : 0) << " %\n";
  std::cout << "timber_years: seed " << seed << ": GROVES AND BELTS — " << std::lround(grove_start)
            << " m3 at the start, " << std::lround(grove_at[0]) << " at year 5, "
            << std::lround(grove_at[1]) << " at year 10, " << std::lround(grove_at[2])
            << " at year 30\n";
  felling.Report("timber_years", started.State());
  return 0;
}
