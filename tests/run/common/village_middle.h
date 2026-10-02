/// @file
/// @brief The village's middle, as every policy of the run's chairman
/// reckons it: one function, one answer.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY ONE HOME. Six policies each carried their own copy — the stores, the
/// horse yard, the sawmill, the felling, the planting and the pits took the
/// mean of EVERY unit standing; the houses, the school, the office and the
/// social objects the mean of the houses people live in. The two agreed while
/// every unit of the start stood in the village. The five old wells of
/// 0.37.110 stand 0.6 to 3 km out, and the first six moved with them: the
/// food yard went 270 m south on all nine villages of the canon, and Epoch II
/// opened in years 13..24 where it had opened in 13..18 (population_curve).
/// A unit kilometres out is on the map; it is not the village.

#ifndef TESTS_RUN_COMMON_VILLAGE_MIDDLE_H_
#define TESTS_RUN_COMMON_VILLAGE_MIDDLE_H_

#include <cstdint>

#include "core_common/geometry.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"

namespace run {

/// @brief The mean position of the houses people live in (a unit some family
/// calls its house).
/// @return That mean; in a world with no lived house the mean of every unit,
///         as the six copies reckoned it until 0.37.111; in a world with no
///         unit at all (150, 150) — the hand-built worlds' corner.
inline core::Vec2 VillageMiddle(const core::WorldState& world) {
  for (const bool lived_only : {true, false}) {
    core::Vec2 sum{.x = 0.0F, .y = 0.0F};
    std::uint32_t seen = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      if (lived_only && unit.household.value == core::kInvalidEntityIdValue) {
        continue;
      }
      sum.x += unit.position.x;
      sum.y += unit.position.y;
      ++seen;
    }
    if (seen != 0) {
      return core::Vec2{.x = sum.x / static_cast<float>(seen),
                        .y = sum.y / static_cast<float>(seen)};
    }
  }
  return core::Vec2{.x = 150.0F, .y = 150.0F};
}

}  // namespace run

#endif  // TESTS_RUN_COMMON_VILLAGE_MIDDLE_H_
