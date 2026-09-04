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

  std::cout << std::fixed << std::setprecision(3);
  std::cout << "oat_balance: " << kYears << " years, seed " << seed << ", tonnes\n";
  std::cout << "year  opening  harvest      fed    eaten  spoiled     sown  shipped  no_room"
               "  closing   residual   cycle\n";

  core::Grams opening = HeldEverywhere(world.State(), oat);
  core::Grams sown_last_year = 0;
  std::uint16_t last_closed = 0;
  double worst_residual_tonnes = 0.0;
  bool cycle_ever_above_one = false;
  bool ever_sown = false;

  for (std::uint32_t year = 1; year <= kYears; ++year) {
    run::AdvanceYear(*world);
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
    const core::Grams no_room = core::AmountOf(book.no_room, oat);
    const core::Grams closing = HeldEverywhere(state, oat);

    const core::Grams expected =
        opening + harvest - fed - eaten - spoiled - sown - shipped - no_room;
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
              << Tonnes(shipped) << std::setw(9) << Tonnes(no_room) << std::setw(9)
              << Tonnes(closing) << std::setw(11) << Tonnes(residual) << std::setw(8) << cycle
              << '\n';

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
