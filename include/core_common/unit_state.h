/// @file
/// @brief UnitRow — the per-unit state: type, place, stock.
/// @threading PARALLEL_READONLY
/// Rows live in WorldState::units under the double-buffer discipline. In
/// phase 1 of the project EVERY write to a unit row is sequential: stores
/// move in the production decisions sub-step (slot 3), houses appear in the
/// demography sub-step, and the parallel slots touch no unit at all — slot 4
/// is split by FIELD (land_state.h), slot 5 is the instant-delivery stub.
/// When logistics becomes real and takes units as its unit of parallelism,
/// that is a threading change and this block changes with it. Structural
/// changes (construction, demolition) stay sequential regardless — and in
/// phase 1 construction is deferred entirely: units come from genesis, plus
/// the housing STUB that appends a house per wedding.
///
/// Design sources: unit rules design (one generic Unit, types in a table —
/// never a class per building), production units design §10 (storage), the
/// start canon (start.md §10: what already stands).
///
/// What is deliberately NOT here in phase 1: wear and condition
/// (construction/wear are deferred), staff assignments (stage 5), upgrade
/// modules (project phase 2). Fields for them are added when their systems
/// arrive — appending is the cheap extension.

#ifndef CORE_COMMON_UNIT_STATE_H_
#define CORE_COMMON_UNIT_STATE_H_

#include <cstdint>

#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_common/state_table.h"

namespace core {

/// @brief One unit. Plain data; behavior comes from the type's table row.
struct UnitRow {
  /// Row of tables/unit_types.csv: barn, warehouse, well, house, heap...
  UnitTypeId type;

  Vec2 position;

  /// Unit level, 1-based (unit rules design: levels and upgrades). Phase 1
  /// keeps every unit at its genesis level.
  std::uint8_t level = 1;

  /// The family living here, for house-kind units; invalid otherwise.
  /// One family - one house (families design §1).
  FamilyId household;

  /// What the unit holds, dense by ResourceId: a warehouse's stores, a
  /// stock-yard's feed buffer, a heap's logs or manure. Empty vector =
  /// holds nothing yet (sized on first delivery).
  ResourceAmounts stock;
};

/// @brief The units table type used by WorldState.
using UnitTable = StateTable<UnitId, UnitRow>;

}  // namespace core

#endif  // CORE_COMMON_UNIT_STATE_H_
