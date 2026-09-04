// Simulation run: the first farming year over the standard wiring. The
// stage-4 criterion (plan §6): the first-year grain harvest matches the
// reference run — sim_v6 year 1: 43.4 ha of grain at soil factor 1.3 gives
// ~48 t. Weather stress can only lower it (never below the cap), so the
// band is asymmetric around the anchor.

#include <cstdint>
#include <iostream>
#include <string>

#include "../common/run_harness.h"
#include "core_common/calendar.h"
#include "core_common/ledger_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace {

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
  failures += run::Expect(many.fields.rows.size() == one.fields.rows.size(),
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
  failures +=
      run::Expect(fields_identical, "1 worker and 3 workers agree on every field, bit for bit");
  bool stores_identical = many.units.rows.size() == one.units.rows.size();
  for (std::uint32_t row = 0; row < many.units.rows.size() && stores_identical; ++row) {
    stores_identical = many.units.rows[row].stock == one.units.rows[row].stock;
  }
  failures += run::Expect(stores_identical, "1 worker and 3 workers agree on every store");
  failures += run::Expect(
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
  failures += run::Expect(households_identical,
                          "1 worker and 3 workers agree on every pantry, mask, hour and component");
  bool satiety_identical = many.residents.rows.size() == one.residents.rows.size();
  for (std::uint32_t row = 0; row < many.residents.rows.size() && satiety_identical; ++row) {
    satiety_identical = many.residents.rows[row].satiety == one.residents.rows[row].satiety &&
                        many.residents.rows[row].health == one.residents.rows[row].health;
  }
  failures += run::Expect(satiety_identical, "and on every person's satiety and health");
  // The ledger (stage 7) is state like any other and must agree too. It is
  // the strictest of these comparisons in one respect: two of its columns
  // are FOLDED by the events slot out of what the parallel phases left in
  // the pantries, so a ledger that differs across worker counts would mean
  // the parallel phases themselves disagreed — even if the pantries above
  // happened to land equal.
  const core::YearLedger& one_book = one.ledger.current;
  const core::YearLedger& many_book = many.ledger.current;
  failures += run::Expect(
      one_book.births == many_book.births && one_book.deaths == many_book.deaths &&
          one_book.harvest == many_book.harvest && one_book.eaten == many_book.eaten &&
          one_book.plot_harvest == many_book.plot_harvest && one_book.feed == many_book.feed &&
          one_book.work_days_by_kind == many_book.work_days_by_kind &&
          one_book.trudodni_accrued == many_book.trudodni_accrued &&
          one_book.satiety_day_mean_sum == many_book.satiety_day_mean_sum,
      "1 worker and 3 workers write the same ledger");
  return failures;
}

}  // namespace

int main() {
  int failures = 0;
  const run::Simulation world = run::Start(1930);
  if (!world) {
    return 1;
  }
  core::ISimulation* simulation = world.simulation.get();
  const core::ITable* resources = world.tables->FindTable("resources");
  if (resources == nullptr) {
    std::cout << "FAIL: no resources table\n";
    return 1;
  }

  const core::WorldState& start = simulation->CompletedState();
  // Six sown fields, a fallow one, two derelict, ten meadows — the start
  // canon's suggested three-year rotation on 70 raised hectares of the 160
  // (start canon §8), and grass that is not scarce; the hands and the
  // mowing window are (terrain design §1) — and the abandoned 3 ha reserve
  // field, which genesis lays as a derelict field too (it is not in the
  // 160). Twenty rows; the count used to say nineteen and describe twenty.
  failures +=
      run::Expect(start.fields.rows.size() == 20, "genesis lays out the arable and the meadows");
  failures += run::Expect(start.units.rows.size() >= 29, "genesis places the start units");
  // 39 cows, 16 billeted horses, and every yard's own goats and hens.
  failures +=
      run::Expect(start.herds.rows.size() == 60, "genesis places the kolkhoz and yard herds");

  const std::uint32_t rye = resources->FindRowByKey("rye");
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
    // Everything the settlement HAS of the three spring grains, which since
    // task A3 is no longer "what is in the stores": a store has a ceiling
    // now, and what did not fit is lying on the field it was reaped from
    // (FieldRow::reaped_grams). Counting only the stores would measure the
    // church, not the harvest — the same sum as before the ceiling, taken
    // where the grain actually is.
    core::Grams grain = stock_of(day, oat) + stock_of(day, barley) + stock_of(day, wheat);
    for (const core::FieldRow& waiting_field : day.fields.rows) {
      const std::uint32_t what = waiting_field.reaped_resource.value;
      if (what == oat || what == barley || what == wheat) {
        grain += waiting_field.reaped_grams;
      }
    }
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

  // WHAT THE FIELDS GAVE, from the year's book — and since task A3 that is
  // no longer the same number as what the stores hold. The stores now have a
  // CEILING (manual/72-storage-and-alarms.md §2), and the start has exactly
  // one of them: the church, sixty tonnes, shared by grain, potato and
  // vegetables alike. So the peak in store measures the church, and the
  // anchor — 43.4 ha of grain at soil factor 1.3, sim_v6 year 1 — has to be
  // read where the harvest is recorded whole — and over the FOUR bread
  // grains the anchor's 43.4 hectares are sown with, rye included. The old
  // reading summed three of them out of the stores, where the start set's
  // own eleven tonnes sat beside the new crop; that sum is a different
  // quantity, and with a ceiling under it, it measures the church.
  const auto ledger_grams = [](const core::ResourceAmounts& column, std::uint32_t id) {
    return id < column.size() ? column[id] : core::Grams{0};
  };
  // The year has turned by now, so the book that holds this year's flows is
  // `closed`; `current` is the new one, already a day old. Summing both is
  // the honest read and survives either side of the turn.
  const auto reaped_of = [&](std::uint32_t id) {
    return ledger_grams(state.ledger.closed.harvest, id) +
           ledger_grams(state.ledger.current.harvest, id);
  };
  const double reaped_tonnes =
      static_cast<double>(reaped_of(rye) + reaped_of(oat) + reaped_of(barley) + reaped_of(wheat)) /
      1.0e6;
  core::Grams waiting = 0;
  for (const core::FieldRow& field : state.fields.rows) {
    waiting += field.reaped_grams;
  }
  core::Grams lost_for_want_of_room = 0;
  for (const core::Grams amount : state.ledger.closed.no_room) {
    lost_for_want_of_room += amount > 0 ? amount : 0;
  }
  for (const core::Grams amount : state.ledger.current.no_room) {
    lost_for_want_of_room += amount > 0 ? amount : 0;
  }
  std::cout << "harvest_first_year: the fields gave " << reaped_tonnes
            << " t of grain; the ceiling left " << static_cast<double>(waiting) / 1.0e6
            << " t waiting on the fields and " << static_cast<double>(lost_for_want_of_room) / 1.0e6
            << " t with nowhere to go at all\n";

  failures += run::Expect(grain_tonnes > 33.0 && grain_tonnes < 53.0,
                          "first-year grain matches the sim_v6 anchor (~48 t, weather may shave)");

  // And what the FIELDS gave, which is a different number and always was:
  // the peak above includes the start set's own eleven tonnes sitting in the
  // church. Printed rather than asserted — the anchor was calibrated on the
  // peak, and inventing a band for a quantity nobody measured against sim_v6
  // would be fitting the check to the code.
  std::cout << "harvest_first_year: of that peak, " << reaped_tonnes
            << " t is this year's own reaping\n";

  // And the finding the ceiling itself is: the start cannot store its own
  // first harvest. That is not a regression to be tuned away — the canon
  // says the food store "is not in the start set and has to be built"
  // (production units §10), and this is the first year the core says so in
  // numbers instead of quietly holding four hundred tonnes in a church.
  failures += run::Expect(waiting > 0 || lost_for_want_of_room > 0,
                          "the first harvest does not fit the church, and the run says so");

  // The meadows deliver what HANDS AND THE WINDOW allow, which is what the
  // canon says in so many words: the fodder base is "limited not by land but
  // by hands at the haymaking and by the cutting season" (start conditions
  // §1). 200 ha at 1.5 t/ha (boss answer Q6, 2026-08-31 — farming.csv,
  // meadow_yield_kg_per_ha) is 300 t if every hectare is cut in time.
  //
  // The band moved DOWN from 250-320 and the reason is worth the paragraph.
  // It was measured when a harnessed job took no horse from the day's pool
  // at all — a real contract violation that task A4 fixed. But the first fix
  // went too far the other way and made a horse REQUIRED: for one measured
  // run a mower without an animal simply did not go, the carts took all
  // sixteen horses, and the cut fell to 156 t. A scythe needs no horse. Only
  // ploughing and harrowing truly cannot be done without one, and that is
  // now the rule (assignment.cpp): a horse is taken if one is free and makes
  // the work faster, and its absence stops nothing but the plough.
  failures += run::Expect(hay_tonnes > 150.0 && hay_tonnes < 250.0,
                          "the meadows deliver what the hands and the window allow");

  // The manure loop runs: the cows fill the heap (~351 t a year at full
  // herd) and the heap is dealt out to the fields. The HEAP is the wrong
  // thing to look at here: it is dealt out at the year's turn, poorest field
  // first, partial doses and all (production_system.cpp, PlanManure), so at
  // this moment it is nearly empty by design. The year's two FLOWS are what
  // say the loop is closed, and the ledger keeps both.
  const core::YearLedger& book = state.ledger.closed;
  const double made_tonnes = manure < book.herd_produce.size()
                                 ? static_cast<double>(book.herd_produce[manure]) / 1.0e6
                                 : 0.0;
  const double plowed_tonnes = static_cast<double>(book.manure_plowed_in) / 1.0e6;
  std::cout << "harvest_first_year: the herd made " << made_tonnes << " t of manure, "
            << plowed_tonnes << " t went in with the plough, " << book.area_manured_ha
            << " ha manured\n";
  failures += run::Expect(made_tonnes > 150.0, "the cow manure flow filled the heap");
  failures += run::Expect(plowed_tonnes > 150.0, "and the heap went into the fields");

  // Fields cycled: the sown six are idle again (harvested), fertility moved.
  std::uint32_t idle_fields = 0;
  for (const core::FieldRow& field : state.fields.rows) {
    idle_fields += field.phase == core::FieldPhase::kIdle ? 1 : 0;
  }
  failures += run::Expect(idle_fields >= 6, "the annual fields returned to idle after harvest");

  // The determinism criterion on a world that actually exercises the
  // parallel field phase: the same year with three workers must land bit
  // for bit on the same world (RACE-003 — the empty-world check could not
  // see this phase at all, since a world without fields dispatches nothing).
  const run::Simulation parallel = run::Start(1930, 3);
  if (!parallel) {
    return failures + 1;
  }
  run::AdvanceYear(*parallel);
  failures += CompareWorkerCounts(state, parallel.State());

  if (failures == 0) {
    std::cout << "harvest_first_year: all checks passed\n";
  }
  return failures;
}
