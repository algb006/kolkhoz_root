/// @file
/// @brief Whether a module's parent stands sound — the one rule the building,
/// the accountant and the production of a module all ask.
/// @threading PARALLEL_READONLY
/// A pure read of a world. Called from the sequential decisions slot and
/// between steps; it writes nothing.
///
/// Design: unit rules §11, "Модули" and "Модуль требует исправного юнита";
/// boss's decisions of 2026-09-13 (parcels 198, 201). A module is built on its
/// parent's plot, and it neither builds nor works while its parent does not
/// stand sound.
///
/// ONE HOME FOR THE RULE, in core_common, because three modules ask it and none
/// of them owns the others: construction refuses to mark or start a module,
/// labor sends nobody to a module's work, production turns out nothing.

#ifndef CORE_COMMON_MODULE_RULES_H_
#define CORE_COMMON_MODULE_RULES_H_

#include <cstdint>

#include "core_common/ids.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"

namespace core {

/// @brief Whether `parent` stands sound enough to carry a module: built (level
///        at least 1), not dead, not paused.
///
/// @note STUB — WEAR IS NOT ASKED. The design says "износ ниже порога" and names
///       no number, and no other rule of the core has one either (an upgrade
///       repairs by itself). The knob is named — `module_parent_wear_max`, in
///       construction.csv — and boss holds the number (parcel 201). Both yards
///       wear not at all since that day, so for stores and the sawmill the stub
///       changes nothing.
inline bool StandsSoundAsParent(const UnitRow& parent) {
  return parent.level > 0 && parent.dead == 0 && parent.paused == 0;
}

/// @brief Whether a unit's parent lets it build and work. True for a
///        free-standing unit; for a module, whether its parent row still
///        exists and stands sound.
inline bool ModuleParentSound(const WorldState& world, const UnitRow& unit) {
  if (unit.parent.value == kInvalidEntityIdValue) {
    return true;
  }
  const std::uint32_t row = FindRow(world.units, unit.parent);
  return row != kNoRow && StandsSoundAsParent(world.units.rows[row]);
}

/// @brief How many modules stand on `parent` — marked, being built or built —
///        and would come down with it (0.37.114: kDemolishUnit on a parent
///        takes its modules; the order's confirmation names this number).
///        Modules already being taken down are not counted: they are going.
inline std::uint32_t ModulesOf(const WorldState& world, UnitId parent) {
  std::uint32_t modules = 0;
  if (parent.value == kInvalidEntityIdValue) {
    return modules;
  }
  for (const UnitRow& unit : world.units.rows) {
    modules += unit.parent.value == parent.value &&
                       unit.construction.phase != ConstructionPhase::kDemolishing
                   ? 1U
                   : 0U;
  }
  return modules;
}

}  // namespace core

#endif  // CORE_COMMON_MODULE_RULES_H_
