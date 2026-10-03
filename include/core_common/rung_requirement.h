/// @file
/// @brief Whether a rung that waits for another unit may open: the unit of the
///        required type stands (unit_levels.csv `requires_unit`; boss, the
///        logistics thread [22]-[24], 4 October 2026). One question, asked by
///        the construction's orders (an upgrade, a new mark) and by the era's
///        readiness (a rung the world cannot open is not required of it).
/// @threading PARALLEL_READONLY
/// A pure read of the world.

#ifndef CORE_COMMON_RUNG_REQUIREMENT_H_
#define CORE_COMMON_RUNG_REQUIREMENT_H_

#include <algorithm>

#include "core_common/ids.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"

namespace core {

/// @brief True when nothing is required (an invalid type), or a unit of
///        `required` stands: built to level 1 or more and not dead. A site at
///        level 0 does not count — the office that is still pegs and string
///        holds no chairman.
inline bool RungRequirementMet(const WorldState& world, UnitTypeId required) {
  if (required.value == kInvalidDefIdValue) {
    return true;
  }
  return std::ranges::any_of(world.units.rows, [required](const UnitRow& unit) {
    return unit.type.value == required.value && unit.level >= 1 && unit.dead == 0;
  });
}

}  // namespace core

#endif  // CORE_COMMON_RUNG_REQUIREMENT_H_
