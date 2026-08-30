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

/// How the village itself came through the year (stage 6). A diagnostic, not
/// a criterion — the criterion is the food_year run of task O4 — but the
/// numbers must never be a mystery when a later change moves them. This is
/// the only run that loads the real tables/, so it is the only one in which
/// the food roster exists at all.
void PrintVillageCondition(const core::WorldState& state) {
  float satiety_total = 0.0F;
  std::uint32_t hungry = 0;
  for (const core::ResidentRow& resident : state.residents.rows) {
    satiety_total += resident.satiety;
    hungry += resident.satiety < 40.0F ? 1U : 0U;
  }
  const auto people = static_cast<float>(state.residents.rows.size());
  double pantry_tonnes = 0.0;
  for (const core::FamilyRow& family : state.families.rows) {
    for (const core::Grams amount : family.pantry) {
      pantry_tonnes += static_cast<double>(amount) / 1.0e6;
    }
  }
  std::uint32_t cattle = 0;
  std::uint32_t billeted = 0;
  std::uint32_t starving = 0;
  for (const core::HerdRow& herd : state.herds.rows) {
    cattle += herd.newborn_count + herd.juvenile_count + herd.adult_count;
    billeted += herd.billeted_count;
    starving += herd.unfed_days > 0.0F ? 1U : 0U;
  }
  std::cout << "harvest_first_year: " << cattle << " head in " << state.herds.rows.size()
            << " herds, " << billeted << " billeted, " << starving << " herds hungry today\n";
  std::cout << "harvest_first_year: mean satiety "
            << (people > 0.0F ? satiety_total / people : 0.0F) << ", " << hungry << " of "
            << state.residents.rows.size() << " under 40, pantries hold " << pantry_tonnes
            << " t\n";
}

/// The determinism criterion on a world that actually exercises the parallel
/// phases: the same year with three workers must land bit for bit on the
/// same world (RACE-003 — the empty-world check could not see these phases
/// at all, since a world without fields or families dispatches nothing).
///
/// Stage 6 widened the surface: the needs and metrics phases now write FAMILY
/// rows from workers — the pantry, the variety mask, the plot hours, the
/// members' satiety — so those get their own comparison rather than riding
/// on the fields'.
int CompareWorkerCounts(const core::WorldState& one, const core::WorldState& many) {
  int failures = 0;
  failures += Expect(many.fields.rows.size() == one.fields.rows.size(),
                     "worker count does not change the field table");
  bool fields_identical = many.fields.rows.size() == one.fields.rows.size();
  for (std::uint32_t row = 0; row < many.fields.rows.size() && fields_identical; ++row) {
    const core::FieldRow& one_worker = one.fields.rows[row];
    const core::FieldRow& three_workers = many.fields.rows[row];
    fields_identical = one_worker.fertility == three_workers.fertility &&
                       one_worker.weather_stress == three_workers.weather_stress &&
                       one_worker.phase == three_workers.phase &&
                       one_worker.crop.value == three_workers.crop.value &&
                       one_worker.repeat_years == three_workers.repeat_years;
  }
  failures += Expect(fields_identical, "1 worker and 3 workers agree on every field, bit for bit");
  bool stores_identical = many.units.rows.size() == one.units.rows.size();
  for (std::uint32_t row = 0; row < many.units.rows.size() && stores_identical; ++row) {
    stores_identical = many.units.rows[row].stock == one.units.rows[row].stock;
  }
  failures += Expect(stores_identical, "1 worker and 3 workers agree on every store");
  failures += Expect(
      many.residents.rows.size() == one.residents.rows.size() && many.rng.state == one.rng.state,
      "the people and the RNG agree across worker counts");
  bool households_identical = many.families.rows.size() == one.families.rows.size();
  for (std::uint32_t row = 0; row < many.families.rows.size() && households_identical; ++row) {
    const core::FamilyRow& one_worker = one.families.rows[row];
    const core::FamilyRow& three_workers = many.families.rows[row];
    households_identical = one_worker.pantry == three_workers.pantry &&
                           one_worker.food_variety_mask == three_workers.food_variety_mask &&
                           one_worker.household_hours == three_workers.household_hours &&
                           one_worker.plot_ratio_sum == three_workers.plot_ratio_sum &&
                           one_worker.component_satiety == three_workers.component_satiety;
  }
  failures += Expect(households_identical,
                     "1 worker and 3 workers agree on every pantry, mask, hour and component");
  bool satiety_identical = many.residents.rows.size() == one.residents.rows.size();
  for (std::uint32_t row = 0; row < many.residents.rows.size() && satiety_identical; ++row) {
    satiety_identical = many.residents.rows[row].satiety == one.residents.rows[row].satiety &&
                        many.residents.rows[row].health == one.residents.rows[row].health;
  }
  failures += Expect(satiety_identical, "and on every person's satiety and health");
  return failures;
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
  // Six sown, two fallow, ten meadows: the grass is not scarce, the
  // hands and the mowing window are (terrain design §1).
  failures += Expect(start.fields.rows.size() == 18, "genesis lays out the arable and the meadows");
  failures += Expect(start.units.rows.size() >= 29, "genesis places the start units");
  // 39 cows, 16 billeted horses, and every yard's own goats and hens.
  failures += Expect(start.herds.rows.size() == 60, "genesis places the kolkhoz and yard herds");

  const std::uint32_t oat = resources->FindRowByKey("oat");
  const std::uint32_t barley = resources->FindRowByKey("barley");
  const std::uint32_t wheat = resources->FindRowByKey("wheat");
  const std::uint32_t hay = resources->FindRowByKey("hay");
  const std::uint32_t manure = resources->FindRowByKey("manure");

  auto stock_of = [](const core::WorldState& world, std::uint32_t resource_row) {
    core::Grams total = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.stock.size() > resource_row) {
        total += unit.stock[resource_row];
      }
    }
    return total;
  };

  // One full year, watching the stores rise and fall. Since stage 6 the
  // herds eat for real and the district takes its share, so the year's END
  // is no longer a measure of the HARVEST — the peak is. That peak is what
  // the sim_v6 anchor is about, and it is sampled here rather than inferred.
  core::Grams grain_peak = 0;
  core::Grams hay_peak = 0;
  for (std::uint32_t tick = 0; tick < core::kTicksPerYear; ++tick) {
    simulation->AdvanceStep();
    const core::WorldState& day = simulation->CompletedState();
    if (core::HourFromTick(day.calendar.tick) != 0) {
      continue;
    }
    const core::Grams grain = stock_of(day, oat) + stock_of(day, barley) + stock_of(day, wheat);
    grain_peak = grain > grain_peak ? grain : grain_peak;
    const core::Grams cut = stock_of(day, hay);
    hay_peak = cut > hay_peak ? cut : hay_peak;
  }
  const core::WorldState& state = simulation->CompletedState();

  const double grain_tonnes = static_cast<double>(grain_peak) / 1.0e6;
  const double hay_tonnes = static_cast<double>(hay_peak) / 1.0e6;
  const double manure_tonnes = static_cast<double>(stock_of(state, manure)) / 1.0e6;
  const double grain_left =
      static_cast<double>(stock_of(state, oat) + stock_of(state, barley) + stock_of(state, wheat)) /
      1.0e6;
  const double hay_left = static_cast<double>(stock_of(state, hay)) / 1.0e6;

  std::cout << "harvest_first_year: grain peaked at " << grain_tonnes << " t and ended at "
            << grain_left << " t, hay " << hay_tonnes << " -> " << hay_left << " t, manure in heap "
            << manure_tonnes << " t\n";

  PrintVillageCondition(state);

  // The reference anchor: ~48 t minus what was eaten as seed for the sown
  // 43.4 ha (7.8 t came from the start stores, so the net gain is what
  // matters: harvest itself lands whole). Weather stress can shave up to
  // the cap (30%); fertility deltas land after harvest.
  failures += Expect(grain_tonnes > 33.0 && grain_tonnes < 53.0,
                     "first-year grain matches the sim_v6 anchor (~48 t, weather may shave)");

  // The meadows delivered: 200 ha at about 2 t/ha, which is what it takes to
  // winter the herd (terrain design §1 — grass is 15% of the map, so the
  // fodder base is bounded by hands and by the mowing window, never by land).
  failures +=
      Expect(hay_tonnes > 300.0 && hay_tonnes < 430.0, "the meadows delivered the herd's winter");

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
  failures += CompareWorkerCounts(state, parallel->CompletedState());

  if (failures == 0) {
    std::cout << "harvest_first_year: all checks passed\n";
  }
  return failures;
}
