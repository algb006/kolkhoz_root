// Simulation run: where the oats goes.
//
// Not a hypothesis — a BALANCE. The seed rule ("sow what the seed covers")
// was built, measured and reverted on 2026-09-05 because the sown area of
// oats walked down to zero and stayed there while every other crop dipped
// and recovered. Boss's question after that is not "why" but "where does it
// part": one crop, one year, every line that moves a gram of it, and a sum
// that must close.
//
//   opening stock + harvested - eaten - fed - spoiled - sown - delivered
//     = closing stock
//
// IF IT DOES NOT CLOSE, THAT IS THE ANSWER, and it is worth more than any
// single line: a gram that leaves by a road nobody named is exactly what a
// balance is for.
//
// The stock is counted wherever a gram of oats can sit: the stores, the
// families' pantries, and the field buffers of a harvest nobody has carted
// yet. A balance that forgets one of the three closes by accident.
//
// It runs on the tree AS SHIPPED — the seed rule is out, so the whole field
// is sown whatever the store holds. What is measured here is therefore the
// unforced flow: where the oats goes when nothing is limiting the sowing.

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "../common/run_harness.h"
#include "core_common/calendar.h"
#include "core_common/quantities.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace {

constexpr std::uint32_t kYears = 15;
constexpr std::uint64_t kDefaultSeed = 1929;

/// @brief Grams of `resource` held anywhere a gram can be held.
core::Grams HeldEverywhere(const core::WorldState& world, core::ResourceId resource) {
  core::Grams total = 0;
  for (const core::UnitRow& unit : world.units.rows) {
    total += core::AmountOf(unit.stock, resource);
  }
  for (const core::FamilyRow& family : world.families.rows) {
    total += core::AmountOf(family.pantry, resource);
  }
  // The field's own buffer: reaped and not yet carried (task A4). It is in
  // the harvest column already, so leaving it out would make the year look
  // short by exactly what the carts have not fetched.
  for (const core::FieldRow& field : world.fields.rows) {
    if (field.reaped_resource.value == resource.value) {
      total += field.reaped_grams;
    }
  }
  return total;
}

double Tonnes(core::Grams grams) {
  return static_cast<double>(grams) / 1.0e6;
}

/// @brief What the seed fund WOULD demand for one crop today, recomputed
/// here from the tables rather than read out of the core.
///
/// A second tally by another road, which is the only kind worth having: the
/// core walks the same fields in family_exchange.cpp (IssueReserve) by the
/// same rule — a field that is IDLE and carries this crop in its coming slot
/// reserves its sowing norm — and a disagreement between the two would be
/// the finding.
core::Grams SeedDemand(const core::WorldState& world,
                       core::CropId crop,
                       float sowing_norm_kg_per_ha) {
  core::Grams demand = 0;
  for (const core::FieldRow& field : world.fields.rows) {
    // The core's rule since 2026-09-05: a field still owes its sowing until
    // the crop is in the ground, so growing and reaping are the only two
    // phases that end the demand. Mirrored here rather than shared, which is
    // the point of a second tally.
    const bool already_sown =
        field.phase == core::FieldPhase::kGrowing || field.phase == core::FieldPhase::kHarvest;
    if (already_sown || field.rotation_year0.value != crop.value) {
      continue;
    }
    demand += core::GramsFromKilograms(sowing_norm_kg_per_ha * field.area_ga);
  }
  return demand;
}

/// @brief What the STORES hold — the quantity the fund is subtracted from
/// before anything is handed out (FreeStock). Pantries are not stores: what
/// a family already holds is past the fund.
core::Grams VillageStores(const core::WorldState& world, core::ResourceId resource) {
  core::Grams total = 0;
  for (const core::UnitRow& unit : world.units.rows) {
    total += core::AmountOf(unit.stock, resource);
  }
  return total;
}

}  // namespace

int main(int argc, char** argv) {
  const std::uint64_t seed = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : kDefaultSeed;
  int failures = 0;
  run::Simulation world = run::Start(seed);
  if (!world) {
    return 1;
  }

  const core::ITable* resources = world.tables->FindTable("resources");
  if (resources == nullptr) {
    std::cout << "FAIL: no resources table\n";
    return 1;
  }
  const std::uint32_t oat_row = resources->FindRowByKey("oat");
  if (oat_row == core::kNoTableRow) {
    std::cout << "FAIL: the tables have no oat\n";
    return 1;
  }
  const core::ResourceId oat{static_cast<std::uint16_t>(oat_row)};

  const core::ITable* crops = world.tables->FindTable("crops");
  const std::uint32_t oat_crop_row =
      crops == nullptr ? core::kNoTableRow : crops->FindRowByKey("oat");
  if (oat_crop_row == core::kNoTableRow) {
    std::cout << "FAIL: the tables have no oat crop\n";
    return 1;
  }
  const core::CropId oat_crop{static_cast<std::uint16_t>(oat_crop_row)};
  const std::optional<float> norm =
      crops->CellReal(oat_crop_row, crops->FindColumn("sowing_norm_kg_per_ha"));
  const float sowing_norm = norm ? *norm : 0.0F;

  std::cout << std::fixed << std::setprecision(3);
  std::cout << "oat_balance: " << kYears << " years, seed " << seed << ", tonnes\n";
  std::cout << "year  opening  harvest      fed    eaten  spoiled     sown  shipped  lost_no_room"
               "  closing   residual   cycle   demand     held  days\n";

  core::Grams opening = HeldEverywhere(world.State(), oat);
  core::Grams sown_last_year = 0;
  std::uint16_t last_closed = 0;
  double worst_residual_tonnes = 0.0;
  bool cycle_ever_above_one = false;
  bool ever_sown = false;

  for (std::uint32_t year = 1; year <= kYears; ++year) {
    // Day by day, because the fund is recomputed on every hand-out and a
    // yearly snapshot would photograph one arbitrary morning of it.
    core::Grams demand_peak = 0;
    core::Grams held_peak = 0;
    std::uint32_t demanding_days = 0;
    // WHEN in the year the fund stands, not only how long. The demand lives
    // while the field is IDLE; the moment ploughing opens, the field leaves
    // that phase and the fund stops protecting the grain — weeks before the
    // sowing actually takes it.
    std::uint32_t first_demand_day = core::kDaysPerYear;
    std::uint32_t last_demand_day = 0;
    for (std::uint32_t day = 0; day < core::kDaysPerYear; ++day) {
      run::AdvanceDays(*world, 1);
      const core::WorldState& today = world.State();
      const core::Grams demand = SeedDemand(today, oat_crop, sowing_norm);
      if (demand > 0) {
        ++demanding_days;
        first_demand_day = std::min(first_demand_day, day);
        last_demand_day = std::max(last_demand_day, day);
      }
      demand_peak = std::max(demand_peak, demand);
      // What the fund actually FREEZES is the smaller of what it asks for
      // and what the stores hold: a demand of nine tonnes against an empty
      // store holds nothing at all.
      held_peak = std::max(held_peak, std::min(demand, VillageStores(today, oat)));
    }
    const core::WorldState& state = world.State();
    if (state.ledger.closed.year == last_closed) {
      continue;  // the books have not turned; nothing to balance
    }
    last_closed = state.ledger.closed.year;
    const core::YearLedger& book = state.ledger.closed;

    const core::Grams harvest = core::AmountOf(book.harvest, oat);
    const core::Grams fed = core::AmountOf(book.feed, oat);
    const core::Grams eaten = core::AmountOf(book.eaten, oat);
    const core::Grams spoiled = core::AmountOf(book.spoiled, oat);
    const core::Grams sown = core::AmountOf(book.seed, oat);
    const core::Grams shipped = core::AmountOf(book.delivered, oat);
    // "Did not fit" — and every one of its five call sites is a LOSS: a
    // field's buffer written off when snow takes the crop or when next
    // year's produce needs the space, hay with nowhere to go, straw, and
    // the stock of a store that fell down. The name says "no room" and the
    // meaning is "gone", which is why a balance that trusted the name came
    // up short.
    const core::Grams lost_no_room = core::AmountOf(book.lost_no_room, oat);
    const core::Grams closing = HeldEverywhere(state, oat);

    const core::Grams expected =
        opening + harvest - fed - eaten - spoiled - sown - shipped - lost_no_room;
    const core::Grams residual = closing - expected;
    // A kilogram of one crop over a year of a whole settlement is the width
    // of the rounding, not of a leak.
    worst_residual_tonnes = std::max(worst_residual_tonnes, std::abs(Tonnes(residual)));

    // THE NUMBER THE QUESTION IS ABOUT: what one gram sown last year gave
    // back this year. Above one and the stock rebuilds itself; below one and
    // the staircase needs no other explanation.
    const double cycle = sown_last_year > 0
                             ? static_cast<double>(harvest) / static_cast<double>(sown_last_year)
                             : 0.0;
    cycle_ever_above_one = cycle_ever_above_one || cycle > 1.0;
    ever_sown = ever_sown || sown > 0;

    std::cout << std::setw(4) << book.year << std::setw(9) << Tonnes(opening) << std::setw(9)
              << Tonnes(harvest) << std::setw(9) << Tonnes(fed) << std::setw(9) << Tonnes(eaten)
              << std::setw(9) << Tonnes(spoiled) << std::setw(9) << Tonnes(sown) << std::setw(9)
              << Tonnes(shipped) << std::setw(9) << Tonnes(lost_no_room) << std::setw(9)
              << Tonnes(closing) << std::setw(11) << Tonnes(residual) << std::setw(8) << cycle
              << std::setw(9) << Tonnes(demand_peak) << std::setw(9) << Tonnes(held_peak)
              << std::setw(6) << demanding_days;
    if (demanding_days > 0) {
      std::cout << "  (days " << first_demand_day << "-" << last_demand_day << ")";
    }
    std::cout << '\n';

    opening = closing;
    sown_last_year = sown;
  }

  // -- THE BALANCE ITSELF, as a check ------------------------------------
  //
  // A gram that leaves by a road nobody named would show here and nowhere
  // else: every other measure of this run is a sum over a road that IS
  // named. One kilogram of tolerance is the rounding of grams into tonnes
  // for printing, not slack.
  failures += run::Expect(worst_residual_tonnes < 0.001,
                          "the oats balance closes: nothing leaves by an unnamed road");
  // And the measure has to prove it measured: a world that grew no oats at
  // all balances perfectly at zero. EVER sown, not sown last year — by the
  // last year of this run nothing is sown at all, and that is the finding
  // rather than a fault of the instrument.
  failures += run::Expect(ever_sown, "the run actually sowed oats to balance");
  // THE ANSWER TO THE QUESTION THIS RUN WAS BUILT FOR, kept as a check so
  // that it cannot quietly stop being true: a kilogram sown gives back more
  // than a kilogram, so the staircase down is not the crop failing to
  // reproduce. Measured at 4.29 on the canonical seed.
  failures +=
      run::Expect(cycle_ever_above_one, "a kilogram of oats sown gives back more than a kilogram");
  std::cout << (failures == 0 ? "oat_balance: the books close\n" : "oat_balance: FAILURES ABOVE\n");
  return failures;
}
