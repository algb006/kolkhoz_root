// Simulation run: task A2's own criterion (phase-2 plan §А2) — "a run builds
// a unit over a few game days, the materials leave the store, the man-days
// are counted, and the thirty-year run still converges".
//
// The last clause belongs to tests/run/thirty_years; the first three are
// this run's, and it is the only place where construction is exercised the
// way a session will use it: through the ORDER BOOK, not by calling the
// subsystem. The chairman marks a site, then says "start" — two orders, two
// steps — and after that the farm builds it with nobody watching.
//
// Why a granary. It is the cheapest thing the start can actually pay for:
// 120 real man-days at eight builders, and every material of its recipe is
// in the start stock (straw in the hay stack, logs and boards in the piles,
// stone and brick in the manor ruins, clay in the clay pile). A run that
// ordered something unaffordable would measure the store, not the building.
//
// Why the first days of the year. Construction is last in the assignment's
// order of kinds (labor model §4), so in the sowing or harvest window the
// brigade is whoever the fields did not take. In the winter days the run
// starts in, the hands are free and the site gets its crew — which is the
// case the criterion is about: the building takes days, not seasons.

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#include "../common/run_harness.h"
#include "core_common/calendar.h"
#include "core_common/order_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace {

/// What the run orders built, and where. The spot is 280 m north of the
/// house row and clear of every plot the layout places — the overlap rule
/// (unit rules §9) is a refusal, and a refused order would make this run
/// measure nothing.
constexpr std::string_view kBuiltType = "granary";

constexpr core::Vec2 kSite{.x = 8600.0F, .y = 9700.0F};

/// Days the run gives the farm before it calls the building a failure. The
/// norm is 120 real man-days — 17 game man-days — and the class caps the
/// crew at eight, so a fed winter village needs three or four days. Twelve
/// leaves room for a day of assignment going elsewhere without turning a
/// slow build into a red run.
constexpr std::uint32_t kPatienceDays = 12;

/// @brief Row index of a table key, or kNoTableRow.
std::uint32_t RowOf(const core::ITableSet& tables, std::string_view table, std::string_view key) {
  const core::ITable* found = tables.FindTable(table);
  return found == nullptr ? core::kNoTableRow : found->FindRowByKey(key);
}

/// @brief Everything the units hold of one resource, in grams.
core::Grams HeldEverywhere(const core::WorldState& world, std::uint32_t resource_row) {
  core::Grams total = 0;
  for (const core::UnitRow& unit : world.units.rows) {
    if (resource_row < unit.stock.size()) {
      total += unit.stock[resource_row];
    }
  }
  return total;
}

/// @brief The site the run ordered: the only unit of its type at level 0,
/// or the built one once it is finished. kNoRow while nothing was marked.
std::uint32_t FindOrdered(const core::WorldState& world, core::UnitTypeId type) {
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    if (world.units.rows[row].type.value == type.value) {
      return row;
    }
  }
  return core::kNoRow;
}

/// @brief Did the step just run emit this event kind?
bool SawEvent(const core::WorldState& world, core::EventKind kind) {
  return std::ranges::any_of(world.step_events,
                             [kind](const core::SimEvent& event) { return event.kind == kind; });
}

/// @brief Advances one step and reports whether the outbox carried `kind`.
/// The outbox is emptied by the NEXT step, so an event has to be read in
/// the step that produced it — which is why this is a function and not a
/// loop with a check after it.
bool StepAndWatch(core::ISimulation& simulation, core::EventKind kind) {
  simulation.AdvanceStep();
  return SawEvent(simulation.CompletedState(), kind);
}

}  // namespace

int main() {
  int failures = 0;
  const run::Simulation simulation = run::Start(1929);
  if (!simulation) {
    return 1;
  }
  const std::uint32_t type_row = RowOf(*simulation.tables, "unit_types", kBuiltType);
  const std::uint32_t log_row = RowOf(*simulation.tables, "resources", "log");
  const std::uint32_t board_row = RowOf(*simulation.tables, "resources", "board");
  if (type_row == core::kNoTableRow || log_row == core::kNoTableRow ||
      board_row == core::kNoTableRow) {
    std::cout << "FAIL: the shipped tables have no granary, log or board — nothing to build\n";
    return 1;
  }
  const core::UnitTypeId built_type{static_cast<std::uint16_t>(type_row)};
  const auto units_before = static_cast<std::uint32_t>(simulation.State().units.rows.size());
  const core::Grams logs_before = HeldEverywhere(simulation.State(), log_row);
  const core::Grams boards_before = HeldEverywhere(simulation.State(), board_row);

  // -- the chairman marks a site: pegs and string, nothing spent -------------
  core::OrderRow mark;
  mark.kind = core::OrderKind::kBuildUnit;
  mark.unit_type = built_type;
  mark.position = kSite;
  simulation->StageOrders(std::span<const core::OrderRow>(&mark, 1), {});
  const bool marked_done = StepAndWatch(*simulation.simulation, core::EventKind::kOrderDone);
  failures += run::Expect(marked_done, "the order to mark a site is settled in its own step");

  const core::WorldState& after_mark = simulation.State();
  failures += run::Expect(after_mark.units.rows.size() == units_before + 1,
                          "and the site is a unit row, not a table of its own");
  const std::uint32_t site_row = FindOrdered(after_mark, built_type);
  if (site_row == core::kNoRow) {
    std::cout << "FAIL: nothing was marked — the rest of the run has no subject\n";
    return 1;
  }
  const core::UnitId site = after_mark.units.row_ids[site_row];
  failures += run::Expect(after_mark.units.rows[site_row].level == 0,
                          "a marked site stands at level 0: not built is not a flag");
  failures += run::Expect(
      after_mark.units.rows[site_row].construction.phase == core::ConstructionPhase::kMarked,
      "and its phase says so");
  failures += run::Expect(HeldEverywhere(after_mark, log_row) == logs_before,
                          "marking spends nothing: the pegs are free");

  // -- and says start: materials come, the brigade works --------------------
  core::OrderRow start;
  start.kind = core::OrderKind::kStartBuild;
  start.unit = site;
  simulation->StageOrders(std::span<const core::OrderRow>(&start, 1), {});
  simulation->AdvanceStep();

  bool built = false;
  bool saw_built_event = false;
  float days_taken = 0.0F;
  const core::Tick started_tick = simulation.State().calendar.tick;
  for (std::uint32_t tick = 0; tick < kPatienceDays * core::kTicksPerDay && !built; ++tick) {
    saw_built_event =
        StepAndWatch(*simulation.simulation, core::EventKind::kUnitBuilt) || saw_built_event;
    const std::uint32_t row = FindRow(simulation.State().units, site);
    built = row != core::kNoRow && simulation.State().units.rows[row].level >= 1;
    days_taken = static_cast<float>(simulation.State().calendar.tick - started_tick) /
                 static_cast<float>(core::kTicksPerDay);
  }

  const core::WorldState& state = simulation.State();
  const core::Grams logs_after = HeldEverywhere(state, log_row);
  const core::Grams boards_after = HeldEverywhere(state, board_row);
  const float builder_days =
      state.ledger.current
          .work_days_by_kind[static_cast<std::size_t>(core::WorkKind::kConstruction)];
  std::cout << "construction_year: the granary took " << days_taken << " game days, "
            << builder_days << " game man-days of building; the stores lost "
            << static_cast<double>(logs_before - logs_after) / 1000.0 << " kg of logs and "
            << static_cast<double>(boards_before - boards_after) / 1000.0 << " kg of boards\n";

  failures += run::Expect(built, "the farm builds what it was told to, within a dozen days");
  failures += run::Expect(saw_built_event, "and says so in the outbox");
  failures += run::Expect(logs_after < logs_before && boards_after < boards_before,
                          "the recipe is paid out of the stores, not conjured");
  failures += run::Expect(builder_days > 0.0F, "and the builders' man-days are on the books");

  const std::uint32_t final_row = FindRow(state.units, site);
  if (final_row != core::kNoRow) {
    const core::UnitRow& unit = state.units.rows[final_row];
    failures += run::Expect(unit.construction.phase == core::ConstructionPhase::kNone,
                            "a finished unit is not a site any more");
    failures +=
        run::Expect(unit.construction.labor_days_remaining == 0.0F, "and owes no more labour");
  }

  if (failures == 0) {
    std::cout << "construction_year: all checks passed\n";
  }
  return failures;
}
