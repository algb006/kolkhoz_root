/// @file
/// @brief Whether a unit marks the yard ground under its plot — the one rule
/// the graphics layer's yard land mask reads, at load and on the events.
/// @threading PARALLEL_READONLY
/// A pure read of one unit row. Called by consumers between steps; it writes
/// nothing.
///
/// Design: Decorations (manual/design/world/map/decorations.md), «Отсчёт — от
/// начала стройки» and «Снесли юнит — исчезают»; #1203 (the human, 22 October
/// 2026: «Делайте в прологе. В игре не забудьте»; boss, the epoch1-resume
/// thread [4]). The ground turns from the START of the works, not the pegs:
/// a marked plot is string, nothing more. It returns when the row is gone —
/// demolished or collapsed, no exceptions.
///
/// THE MASK IS NOT SAVED: it is this rule over the saved units table, so a
/// consumer rebuilds it on load and then follows kUnitWorksStarted (stamp),
/// kUnitDemolished and kUnitCollapsed (unstamp). A saved mask would be a
/// second home that could disagree with the units it describes.
///
/// A unit MOVE, when one is built, owes the mask an unstamp at the old place
/// and a stamp at the new (the same design row). The core has no move order
/// yet.

#ifndef CORE_COMMON_YARD_GROUND_RULES_H_
#define CORE_COMMON_YARD_GROUND_RULES_H_

#include "core_common/unit_state.h"

namespace core {

/// @brief Whether `unit` marks the yard ground under its plot: every phase
///        from the start of the works on — delivering, building, standing,
///        repairing, insulating, being taken down — and not a marked plot.
/// @return false only for ConstructionPhase::kMarked.
[[nodiscard]] inline bool UnitStampsYardGround(const UnitRow& unit) {
  return unit.construction.phase != ConstructionPhase::kMarked;
}

}  // namespace core

#endif  // CORE_COMMON_YARD_GROUND_RULES_H_
