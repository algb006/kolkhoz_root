// Simulation run: the first farming year over the standard wiring. The
// stage-4 criterion (plan §6): the first-year grain harvest matches the
// reference run — sim_v6 year 1: 43.4 ha of grain at soil factor 1.3 gives
// ~48 t. Weather stress can only lower it (never below the cap), so the
// band is asymmetric around the anchor.

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
  std::string error;
  const auto tables = core::LoadTableSet("tables", &error);
  if (tables == nullptr) {
    std::cout << "FAIL: tables/ did not load (" << error << ") — run from the repo root\n";
    return 1;
  }
  const core::ITable* resources = tables->FindTable("resources");
  if (resources == nullptr) {
    std::cout << "FAIL: no resources table\n";
    return 1;
  }

  core::StandardSimulationConfig config;
  config.tables = tables.get();
  config.world_seed = 1930;
  config.worker_count = 1;
  const auto simulation = core::CreateStandardSimulation(config);
  if (simulation == nullptr) {
    std::cout << "FAIL: the simulation did not assemble\n";
    return 1;
  }

  const core::WorldState& start = simulation->CompletedState();
  failures += Expect(start.fields.rows.size() == 12, "genesis lays out 12 fields");
  failures += Expect(start.units.rows.size() >= 29, "genesis places the start units");
  failures += Expect(start.herds.rows.size() == 17, "39 cows plus 16 horse yards");

  // One full year.
  for (std::uint32_t tick = 0; tick < core::kTicksPerYear; ++tick) {
    simulation->AdvanceStep();
  }
  const core::WorldState& state = simulation->CompletedState();

  // Sum the grain across all storage.
  auto stock_of = [&state](std::uint32_t resource_row) {
    core::Grams total = 0;
    for (const core::UnitRow& unit : state.units.rows) {
      if (unit.stock.size() > resource_row) {
        total += unit.stock[resource_row];
      }
    }
    return total;
  };
  const std::uint32_t oat = resources->FindRowByKey("oat");
  const std::uint32_t barley = resources->FindRowByKey("barley");
  const std::uint32_t wheat = resources->FindRowByKey("wheat");
  const std::uint32_t hay = resources->FindRowByKey("hay");
  const std::uint32_t manure = resources->FindRowByKey("manure");
  const double grain_tonnes =
      static_cast<double>(stock_of(oat) + stock_of(barley) + stock_of(wheat)) / 1.0e6;
  const double hay_tonnes = static_cast<double>(stock_of(hay)) / 1.0e6;
  const double manure_tonnes = static_cast<double>(stock_of(manure)) / 1.0e6;

  std::cout << "harvest_first_year: grain " << grain_tonnes << " t, hay " << hay_tonnes
            << " t, manure in heap " << manure_tonnes << " t\n";

  // The reference anchor: ~48 t minus what was eaten as seed for the sown
  // 43.4 ha (7.8 t came from the start stores, so the net gain is what
  // matters: harvest itself lands whole). Weather stress can shave up to
  // the cap (30%); fertility deltas land after harvest.
  failures += Expect(grain_tonnes > 33.0 && grain_tonnes < 53.0,
                     "first-year grain matches the sim_v6 anchor (~48 t, weather may shave)");

  // Meadows delivered hay (80 ha x 2 t/ha, minus winter feeding).
  failures += Expect(hay_tonnes > 120.0 && hay_tonnes < 200.0,
                     "meadows delivered on the order of 160 t of hay");

  // The manure loop runs: cows fill the heap (~351 t/year at full herd,
  // minus the spring doses plowed into the sown fields).
  failures += Expect(manure_tonnes > 150.0, "the cow manure flow filled the heap");

  // Fields cycled: the sown six are idle again (harvested), fertility moved.
  std::uint32_t idle_fields = 0;
  for (const core::FieldRow& field : state.fields.rows) {
    idle_fields += field.phase == core::FieldPhase::kIdle ? 1 : 0;
  }
  failures += Expect(idle_fields >= 6, "the annual fields returned to idle after harvest");

  // The determinism criterion on a world that actually exercises the
  // parallel field phase: the same year with three workers must land bit
  // for bit on the same world (RACE-003 — the empty-world check could not
  // see this phase at all, since a world without fields dispatches nothing).
  core::StandardSimulationConfig parallel_config = config;
  parallel_config.worker_count = 3;
  const auto parallel = core::CreateStandardSimulation(parallel_config);
  if (parallel == nullptr) {
    std::cout << "FAIL: the parallel simulation did not assemble\n";
    return failures + 1;
  }
  for (std::uint32_t tick = 0; tick < core::kTicksPerYear; ++tick) {
    parallel->AdvanceStep();
  }
  const core::WorldState& many = parallel->CompletedState();
  failures += Expect(many.fields.rows.size() == state.fields.rows.size(),
                     "worker count does not change the field table");
  bool fields_identical = many.fields.rows.size() == state.fields.rows.size();
  for (std::uint32_t row = 0; row < many.fields.rows.size() && fields_identical; ++row) {
    const core::FieldRow& one_worker = state.fields.rows[row];
    const core::FieldRow& three_workers = many.fields.rows[row];
    fields_identical = one_worker.fertility == three_workers.fertility &&
                       one_worker.weather_stress == three_workers.weather_stress &&
                       one_worker.phase == three_workers.phase &&
                       one_worker.crop.value == three_workers.crop.value &&
                       one_worker.repeat_years == three_workers.repeat_years;
  }
  failures += Expect(fields_identical, "1 worker and 3 workers agree on every field, bit for bit");
  bool stores_identical = many.units.rows.size() == state.units.rows.size();
  for (std::uint32_t row = 0; row < many.units.rows.size() && stores_identical; ++row) {
    stores_identical = many.units.rows[row].stock == state.units.rows[row].stock;
  }
  failures += Expect(stores_identical, "1 worker and 3 workers agree on every store");
  failures += Expect(many.residents.rows.size() == state.residents.rows.size() &&
                         many.rng.state == state.rng.state,
                     "the people and the RNG agree across worker counts");

  if (failures == 0) {
    std::cout << "harvest_first_year: all checks passed\n";
  }
  return failures;
}
