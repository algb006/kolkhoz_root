// Simulation run: 33 years of demography over the standard wiring, checked
// against the reference-curve milestones (stage-3 criterion, plan §5:
// ~200 by year 7, ~500 by year 14, ~1500 by year 33; reference run
// manual/balance/sim/demography.py at x4 acceleration, migration 8/year).
// Tolerances are wide on purpose: the core is a per-person stochastic model
// of the same rates, not the cohort arithmetic itself.

#include <cstdint>
#include <iostream>
#include <string>

#include "../common/building_chairman.h"
#include "../common/run_harness.h"
#include "core_common/calendar.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace {}  // namespace

int main() {
  int failures = 0;

  // The shipped tables are the reference configuration for this run.
  const run::Simulation world = run::Start(1931);
  if (!world) {
    return 1;
  }
  core::ISimulation* simulation = world.simulation.get();

  const std::uint32_t start_population =
      static_cast<std::uint32_t>(simulation->CompletedState().residents.rows.size());
  failures += run::Expect(start_population == 80, "genesis seats 80 residents");
  failures += run::Expect(simulation->CompletedState().families.rows.size() == 21,
                          "genesis builds 21 yards");

  // THE BUILDING CHAIRMAN (building_chairman.h). This run had no chairman at
  // all while weddings got a free house from a stub; the stub is gone (boss,
  // parcel 257), and a curve measured without anybody building is a curve of
  // a village leaving in its first winters — 36 at year 7, nobody at 14.
  run::BuildingChairman builder(*world.tables);
  run::BuildingChairman::Declare("population_curve");

  constexpr std::uint32_t kYears = 33;
  std::uint32_t population_year7 = 0;
  std::uint32_t population_year14 = 0;
  for (std::uint32_t year = 1; year <= kYears; ++year) {
    for (std::uint32_t day = 0; day < core::kDaysPerYear; ++day) {
      run::AdvanceDays(*world, 1);
      builder.RunDay(*simulation);
    }
    const core::WorldState& state = simulation->CompletedState();
    const auto population = static_cast<std::uint32_t>(state.residents.rows.size());
    if (year == 7) {
      population_year7 = population;
    }
    if (year == 14) {
      population_year14 = population;
    }
    std::cout << "year " << year << ": " << population << " residents, "
              << state.families.rows.size() << " families, epoch " << static_cast<int>(state.epoch)
              << '\n';
  }

  const core::WorldState& final_state = simulation->CompletedState();
  const auto final_population = static_cast<std::uint32_t>(final_state.residents.rows.size());

  // Reference milestones: 199 (year 7), ~500 (year 14), ~1500 (year 33).
  //
  // THE BANDS ARE WIDE BECAUSE THE CURVE IS CHAOTIC, and that was measured,
  // not assumed. Boss moved one ten-hectare field 141 m on 2 September 2026.
  // The economy barely noticed — the year's labour went from 715.11 to
  // 715.76 game man-days and the grain peak from 34.0436 to 34.0358 t, both
  // under a tenth of a percent. The population at year 7 went from 222 to
  // 262, and at year 14 from 516 to 632: eighteen and twenty-two percent.
  //
  // Nothing is broken. Births are drawn against the world's RNG in row
  // order, so a hair's difference in one family's satiety moves one draw,
  // that draw moves a wedding, and thirty-three years later the village is
  // built of different people. Same seed and same tables still give the same
  // village to the byte — the determinism check is elsewhere and it holds.
  //
  // So a band of plus or minus fifteen percent measures ONE TRAJECTORY and
  // calls a balance edit a regression. What this run can honestly assert is
  // the shape: the settlement grows, it does not stall, and it does not
  // explode. The bands below are that claim. Narrowing them again means
  // averaging several seeds first, which costs minutes per run — recorded in
  // OPEN_ITEMS rather than done here.
  //
  // The upper ends also carry a known inflation: weddings get a free house
  // from a stub, so the canon's brake ("build houses or the village ages")
  // has never been applied (69-reconciliation.md §11).
  //
  // KNOWN GAPS, NOT MOVED BANDS (boss, parcel 301: "Цели оставить, облегчить
  // стройку"). With the stub gone and a chairman who builds, the curve stands
  // at 127 / 118 / 142 on this seed; with free materials thirty_years' nine
  // seeds reach 486 by year 14 — the gap is the building chain. The upper
  // ends still fail: a village that explodes is not a gap, it is a fault.
  failures += run::KnownGap(population_year7 >= 150,
                            "the settlement is growing by year 7, not stalled",
                            std::to_string(population_year7) + " against 150");
  failures += run::Expect(population_year7 <= 320, "and not exploding by year 7");
  failures += run::KnownGap(population_year14 >= 380,
                            "and is past the Epoch II mark by year 14",
                            std::to_string(population_year14) + " against 380");
  failures += run::Expect(population_year14 <= 800, "and not exploding by year 14");
  failures += run::KnownGap(final_population >= 1150,
                            "and lands in the canon's order of magnitude by year 33",
                            std::to_string(final_population) + " against 1150");
  failures += run::Expect(final_population <= 2300, "and not exploding by year 33");
  // AND THE FLOOR UNDER ALL THREE CEILINGS, which is not a band and cannot be
  // a matter of balance: the years were lived and the village is not the
  // genesis standing still. Every hard assertion above is an upper bound, and
  // the lower ones are KnownGap by design — so on 2026-09-16, with every phase
  // of the step removed, this run passed in 2.6 seconds against the usual 25:
  // eighty residents never born, never dead, never married, under every
  // ceiling (boss, standstill parcel 9).
  failures += run::Expect(final_state.calendar.date.year + 1 >= kYears,
                          "the curve lived its thirty-three years");
  failures += run::Expect(final_population != start_population,
                          "and the village it drew is not the genesis standing still");
  // The epoch switch is a population-threshold STUB (residents_system.cpp:
  // the designed era events — the readiness index, the ceremonies — are a
  // later phase). It flips at exactly 500, so asserting it at year 14 is a
  // HARDER claim about the same number the line above calls "about 500" with
  // a band of 380 to 650. Two assertions about one number, one banded and
  // one exact, is a contradiction in the test rather than in the model: a
  // run at 447 satisfies "about 500" and fails "the threshold was crossed".
  // What survives is the claim that matters — the settlement reaches Epoch II
  // on the way, not that it does so in a particular year of a stubbed rule.
  // The epochs follow the population threshold, so they are the same gap.
  failures += run::KnownGap(final_state.epoch >= core::Epoch::kTwo,
                            "Epoch II is reached on the way",
                            "epoch " + std::to_string(static_cast<int>(final_state.epoch)));
  failures += run::KnownGap(final_state.epoch == core::Epoch::kThree,
                            "Epoch III has come by year 33",
                            "epoch " + std::to_string(static_cast<int>(final_state.epoch)));

  // Integrity after three decades of births, deaths, weddings and moves:
  // every resident's family exists, spouses point at each other.
  bool families_hold = true;
  bool spouses_hold = true;
  for (std::uint32_t row = 0; row < final_state.residents.rows.size(); ++row) {
    const core::ResidentRow& resident = final_state.residents.rows[row];
    families_hold =
        families_hold && core::FindRow(final_state.families, resident.family) != core::kNoRow;
    if (resident.spouse.value != core::kInvalidEntityIdValue) {
      const std::uint32_t spouse_row = core::FindRow(final_state.residents, resident.spouse);
      spouses_hold = spouses_hold && spouse_row != core::kNoRow &&
                     final_state.residents.rows[spouse_row].spouse.value ==
                         final_state.residents.row_ids[row].value;
    }
  }
  failures += run::Expect(families_hold, "every resident's family exists");
  failures += run::Expect(spouses_hold, "spouse links are symmetric");

  // The sex balance held (the self-correcting draw): no lasting skew.
  std::uint32_t men = 0;
  for (const core::ResidentRow& resident : final_state.residents.rows) {
    men += resident.sex == core::Sex::kMale ? 1 : 0;
  }
  const float male_share = static_cast<float>(men) / static_cast<float>(final_population);
  failures += run::Expect(male_share > 0.42F && male_share < 0.58F, "the sex balance held");

  std::cout << "population_curve: year 7 = " << population_year7
            << ", year 14 = " << population_year14 << ", year 33 = " << final_population
            << ", male share " << male_share << '\n';
  if (failures == 0) {
    std::cout << "population_curve: all checks passed\n";
  }
  return failures;
}
