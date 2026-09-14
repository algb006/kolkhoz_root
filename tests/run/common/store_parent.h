/// @file
/// @brief The parent yard a store stands on, for the runs that put a store up
/// by hand (unit rules §11; boss, parcels 198 and 222).
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY. By unit_types.csv every store is a module of a yard — the granary of
/// the food yard, the goods store of the utility yard — and the core refuses
/// to mark a module with no sound parent under it. Until 2026-09-14 the runs
/// blanked that parent in a prosthesis and stood stores alone; store
/// modularity is switched on now, so a run that marks a store first stands its
/// yard up. A yard's first level is a plot: pegs and string, no materials and
/// no man-days, so the yard stands on the step after its start.

#ifndef TESTS_RUN_COMMON_STORE_PARENT_H_
#define TESTS_RUN_COMMON_STORE_PARENT_H_

#include <cstdint>
#include <span>
#include <string_view>

#include "core_common/calendar.h"
#include "core_common/order_state.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace run {

/// @brief The parent type unit_types.csv names for `module_key`; invalid when
///        the type has none or is not there.
inline core::UnitTypeId ParentTypeOf(const core::ITableSet& tables, std::string_view module_key) {
  const core::ITable* const types = tables.FindTable("unit_types");
  if (types == nullptr) {
    return core::UnitTypeId{};
  }
  const std::uint32_t row = types->FindRowByKey(module_key);
  if (row == core::kNoTableRow) {
    return core::UnitTypeId{};
  }
  const std::uint32_t parent =
      types->FindRowByKey(types->CellText(row, types->FindColumn("parent")));
  return parent == core::kNoTableRow ? core::UnitTypeId{}
                                     : core::UnitTypeId{static_cast<std::uint16_t>(parent)};
}

/// @brief Marks the parent yard of `module_key` centred on `site`, starts it,
///        and steps until it stands.
/// @return True when a yard of that type stands centred on `site` afterwards
///         (or the module has no parent at all); false when it did not within
///         two days. The steps it took are the run's to account for.
inline bool StandUpParentYard(const core::ITableSet& tables,
                              core::ISimulation& simulation,
                              std::string_view module_key,
                              core::Vec2 site) {
  const core::UnitTypeId parent = ParentTypeOf(tables, module_key);
  if (parent.value == core::kInvalidDefIdValue) {
    return true;
  }
  // THIS yard, the one centred on the site: a yard the run's chairman put up
  // elsewhere does not carry a store marked here.
  const auto standing = [&]() {
    for (const core::UnitRow& unit : simulation.CompletedState().units.rows) {
      const float dx = unit.position.x - site.x;
      const float dy = unit.position.y - site.y;
      if (unit.type.value == parent.value && unit.level > 0 && (dx * dx) + (dy * dy) < 1.0F) {
        return true;
      }
    }
    return false;
  };
  core::OrderRow mark;
  mark.kind = core::OrderKind::kBuildUnit;
  mark.unit_type = parent;
  mark.position = site;
  simulation.StageOrders(std::span<const core::OrderRow>(&mark, 1), {});
  for (std::uint32_t tick = 0; tick < 2U * core::kTicksPerDay && !standing(); ++tick) {
    const core::WorldState& world = simulation.CompletedState();
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      const float dx = unit.position.x - site.x;
      const float dy = unit.position.y - site.y;
      if (unit.type.value == parent.value && (dx * dx) + (dy * dy) < 1.0F &&
          unit.construction.phase == core::ConstructionPhase::kMarked) {
        core::OrderRow start;
        start.kind = core::OrderKind::kStartBuild;
        start.unit = world.units.row_ids[row];
        simulation.StageOrders(std::span<const core::OrderRow>(&start, 1), {});
        break;
      }
    }
    simulation.AdvanceStep();
  }
  return standing();
}

}  // namespace run

#endif  // TESTS_RUN_COMMON_STORE_PARENT_H_
