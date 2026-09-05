// The seam of a work assignment (work_seam.h). Moved out of core_labor on
// 2026-09-05 when the resident's activity needed the same seven cases to
// tell "nobody gave him work" from "he was given work and there is nothing
// to work with".

#include "core_common/work_seam.h"

#include <cstdint>

#include "core_common/state_table_ops.h"

namespace core {

const float* WorkSeamOf(const WorldState& world, const WorkAssignment& work) {
  if (work.kind == WorkKind::kHerdCare) {
    const std::uint32_t row = FindRow(world.herds, work.herd);
    return row == kNoRow ? nullptr : &world.herds.rows[row].care_days_remaining;
  }
  if (work.kind == WorkKind::kConstruction) {
    const std::uint32_t row = FindRow(world.units, work.unit);
    // A site that finished or was demolished since the morning: the crew
    // simply has nothing to drain, exactly as with a field production has
    // moved on.
    return row == kNoRow ? nullptr : &world.units.rows[row].construction.labor_days_remaining;
  }
  const std::uint32_t row = FindRow(world.fields, work.field);
  if (row == kNoRow) {
    return nullptr;
  }
  if (work.kind == WorkKind::kHauling) {
    // Hauling drains ITS OWN seam. It used to share the field's work seam,
    // on the strength of a comment claiming the two could never overlap —
    // and the canon's own rotation overlapped them in the same step. What
    // ends the haul is the load being gone, not a phase changing under it.
    return world.fields.rows[row].reaped_grams > 0 ? &world.fields.rows[row].haul_days_remaining
                                                   : nullptr;
  }
  if (KindOfPhase(world.fields.rows[row].phase) != work.kind) {
    return nullptr;  // production has moved the field on since the morning
  }
  return &world.fields.rows[row].work_days_remaining;
}

bool WorkPlaceOf(const WorldState& world, const WorkAssignment& work, Vec2& place) {
  if (work.kind == WorkKind::kHerdCare) {
    const std::uint32_t herd_row = FindRow(world.herds, work.herd);
    if (herd_row == kNoRow) {
      return false;
    }
    const std::uint32_t unit_row = FindRow(world.units, world.herds.rows[herd_row].unit);
    if (unit_row == kNoRow) {
      return false;
    }
    place = world.units.rows[unit_row].position;
    return true;
  }
  if (work.kind == WorkKind::kConstruction) {
    const std::uint32_t row = FindRow(world.units, work.unit);
    if (row == kNoRow) {
      return false;
    }
    place = world.units.rows[row].position;
    return true;
  }
  const std::uint32_t row = FindRow(world.fields, work.field);
  if (row == kNoRow) {
    return false;
  }
  place = world.fields.rows[row].center;
  return true;
}

bool HomePositionOf(const WorldState& world, FamilyId family, Vec2& home) {
  const std::uint32_t family_row = FindRow(world.families, family);
  if (family_row == kNoRow) {
    return false;
  }
  const std::uint32_t house_row = FindRow(world.units, world.families.rows[family_row].house);
  if (house_row == kNoRow) {
    return false;
  }
  home = world.units.rows[house_row].position;
  return true;
}

float* WorkSeamOf(WorldState& world, const WorkAssignment& work) {
  // One body, two constnesses: the const overload does the reasoning and
  // this one only gives the answer back writable. Two bodies would be two
  // rosters of work kinds, and the day a kind is added one of them would
  // still be right.
  return const_cast<float*>(WorkSeamOf(static_cast<const WorldState&>(world), work));
}

}  // namespace core
