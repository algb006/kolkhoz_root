/// @file
/// @brief What this spring's flood took (#1894; look's «flood-loss» remains mark,
/// ue's to stand): the ricks, stacks and barns whose hay and straw the flood
/// zeroed on a floodplain cell, and the floodplain meadows whose reaped heap it
/// took - read off UnitRow::flood_lost_year and FieldRow::flood_lost_year (#1744,
/// #1375; save 211). No flood day: «this spring» (boss, #1894).
/// @threading PARALLEL_READONLY
/// Plain data, built between steps by a pure read (ISession::FloodLossesThisYear).

#ifndef CORE_COMMON_FLOOD_LOSSES_VIEW_H_
#define CORE_COMMON_FLOOD_LOSSES_VIEW_H_

#include <cstdint>
#include <vector>

#include "core_common/geometry.h"
#include "core_common/ids.h"

namespace core {

/// @brief One loss: a unit's store or a meadow's heap.
struct FloodLoss {
  /// The unit's position, or the meadow's centre.
  Vec2 place;
  /// Exactly one of the two is valid: the unit (its type says the kind) or
  /// the meadow field.
  UnitId unit;
  UnitTypeId unit_type;
  FieldId field;
  /// The meadow's area, hectares; 0 for a unit.
  float area_ga = 0.0F;
};

/// @brief This calendar year's losses to the spring flood.
struct FloodLossesView {
  /// False from a bare engine.
  bool measured = false;
  /// The calendar year (1-based) the rows are of.
  std::uint16_t year = 0;
  std::vector<FloodLoss> losses;
};

}  // namespace core

#endif  // CORE_COMMON_FLOOD_LOSSES_VIEW_H_
