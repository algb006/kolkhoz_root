// Simulation run: 33 years of demography over the standard wiring, checked
// against the reference-curve milestones (stage-3 criterion, plan §5:
// ~200 by year 7, ~500 by year 14, ~1500 by year 33; reference run
// manual/balance/sim/demography.py at x4 acceleration, migration 8/year).
// Tolerances are wide on purpose: the core is a per-person stochastic model
// of the same rates, not the cohort arithmetic itself.

#include <cstdint>
#include <iostream>
#include <string>

#include "core_common/calendar.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
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

}  // namespace

int main() {
  int failures = 0;

  // The shipped tables are the reference configuration for this run.
  std::string error;
  const auto tables = core::LoadTableSet("tables", &error);
  if (tables == nullptr) {
    // The run is started from the repository root by ctest; a stray cwd is
    // a usage problem, not a simulation failure.
    std::cout << "FAIL: tables/ did not load (" << error << ") — run from the repo root\n";
    return 1;
  }

  core::StandardSimulationConfig config;
  config.tables = tables.get();
  config.world_seed = 1931;
  config.worker_count = 1;
  const auto simulation = core::CreateStandardSimulation(config);
  if (simulation == nullptr) {
    std::cout << "FAIL: the simulation did not assemble\n";
    return 1;
  }

  const std::uint32_t start_population =
      static_cast<std::uint32_t>(simulation->CompletedState().residents.rows.size());
  failures += Expect(start_population == 80, "genesis seats 80 residents");
  failures +=
      Expect(simulation->CompletedState().families.rows.size() == 21, "genesis builds 21 yards");

  constexpr std::uint32_t kYears = 33;
  std::uint32_t population_year7 = 0;
  std::uint32_t population_year14 = 0;
  core::Epoch epoch_year14 = core::Epoch::kOne;
  for (std::uint32_t year = 1; year <= kYears; ++year) {
    for (std::uint32_t tick = 0; tick < core::kTicksPerYear; ++tick) {
      simulation->AdvanceStep();
    }
    const core::WorldState& state = simulation->CompletedState();
    const auto population = static_cast<std::uint32_t>(state.residents.rows.size());
    if (year == 7) {
      population_year7 = population;
    }
    if (year == 14) {
      population_year14 = population;
      epoch_year14 = state.epoch;
    }
    std::cout << "year " << year << ": " << population << " residents, "
              << state.families.rows.size() << " families, epoch " << static_cast<int>(state.epoch)
              << '\n';
  }

  const core::WorldState& final_state = simulation->CompletedState();
  const auto final_population = static_cast<std::uint32_t>(final_state.residents.rows.size());

  // Reference milestones: 199 (year 7), ~500 (year 14), ~1500 (year 33).
  failures +=
      Expect(population_year7 >= 150 && population_year7 <= 260, "about 200 residents by year 7");
  failures += Expect(population_year14 >= 380 && population_year14 <= 650,
                     "about 500 residents by year 14");
  failures += Expect(final_population >= 1150 && final_population <= 1950,
                     "about 1500 residents by year 33");
  failures += Expect(epoch_year14 >= core::Epoch::kTwo, "Epoch II has come by year 14");
  failures += Expect(final_state.epoch == core::Epoch::kThree, "Epoch III has come by year 33");

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
  failures += Expect(families_hold, "every resident's family exists");
  failures += Expect(spouses_hold, "spouse links are symmetric");

  // The sex balance held (the self-correcting draw): no lasting skew.
  std::uint32_t men = 0;
  for (const core::ResidentRow& resident : final_state.residents.rows) {
    men += resident.sex == core::Sex::kMale ? 1 : 0;
  }
  const float male_share = static_cast<float>(men) / static_cast<float>(final_population);
  failures += Expect(male_share > 0.42F && male_share < 0.58F, "the sex balance held");

  std::cout << "population_curve: year 7 = " << population_year7
            << ", year 14 = " << population_year14 << ", year 33 = " << final_population
            << ", male share " << male_share << '\n';
  if (failures == 0) {
    std::cout << "population_curve: all checks passed\n";
  }
  return failures;
}
