/// @file
/// @brief UnitRow — the per-unit state: type, place, stock, and the site it
/// is while it is being built, raised a level or taken down.
/// @threading PARALLEL_READONLY
/// Rows live in WorldState::units under the double-buffer discipline. EVERY
/// write to a unit row is sequential: stores move in the production
/// decisions sub-step (slot 3), houses appear in the demography sub-step,
/// the construction sub-step of the same slot moves the `construction`
/// block and the level, and the parallel slots touch no unit at all — slot
/// 4 is split by FIELD (land_state.h), slot 5 is the instant-delivery stub.
/// When logistics becomes real and takes units as its unit of parallelism,
/// that is a threading change and this block changes with it. Structural
/// changes — a row appended for a marked site, a row removed after
/// demolition — stay sequential regardless (buffer-law rule 6).
///
/// Design sources: unit rules design (one generic Unit, types in a table —
/// never a class per building; §9 the plot, §11 levels, §14 demolition),
/// construction design (§6 marking and the manual start, §7 the phases, §8
/// labour days and progress, §12 demolition), production units design §10
/// (storage), the start canon (start.md §10: what already stands).
///
/// A SITE IS A UNIT ROW, not a table of its own (project phase 2, task A2;
/// manual/71-construction.md §2). The boundary contract promised that
/// construction progress arrives "as fields of the same rows" (70-boundary
/// §3), an upgrade is a site ON a unit that already exists and keeps its
/// id, and the site's materials are delivered into the same `stock` any
/// unit has — so the logistics that will replace the instant stub (task A4)
/// needs no second kind of destination. A unit that is not yet built is a
/// row at LEVEL 0: the one rule every consumer needs.
///
/// What is deliberately NOT here yet: wear and condition (task A5), staff
/// assignments, upgrade modules. Fields for them are added when their
/// systems arrive — appending is the cheap extension.

#ifndef CORE_COMMON_UNIT_STATE_H_
#define CORE_COMMON_UNIT_STATE_H_

#include <cstdint>

#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_common/state_table.h"

namespace core {

/// @brief Where a unit stands as a construction site. The order of the
/// values is the order of the phases (construction design §7); kNone is
/// "not a site", which is what every unit is most of its life.
enum class ConstructionPhase : std::uint8_t {
  kNone = 0,  ///< Built and standing (or a start unit). Nothing in progress.

  /// Marked out with pegs and string (construction design §6): the plot is
  /// taken, nothing has been spent, nothing happens until kStartBuild. May
  /// lie for years; removed at once and for free by kDemolishUnit.
  kMarked,

  /// Started; materials are being brought to the site — into `stock`.
  /// Site preparation (phase 0 of the design: clearing, levelling) is
  /// folded in here as a STUB of zero cost: the core has no trees and no
  /// relief to clear.
  kDelivering,

  /// The full recipe is on site; labour is being invested. This is the
  /// phase the labor sub-step drains (construction.labor_days_remaining).
  kBuilding,

  /// Being taken down (construction design §12): stock already moved out,
  /// labour being invested in the dismantling; the row is removed when it
  /// reaches zero. The unit is at level 0 from the moment this begins.
  kDemolishing,
};

/// @brief The site block of a unit: what is being built here and how far
/// it has come. Plain data; all zeros when the unit is simply standing.
///
/// THE LABOUR SEAM. Like a field's work_days_remaining (land_state.h) and a
/// herd's care_days_remaining, `labor_days_remaining` is the contract
/// between two modules that never call each other: construction sets it
/// when a site enters kBuilding or kDemolishing, the labor sub-step drains
/// it with the crew's hourly output (WorkKind::kConstruction), construction
/// completes the site when it reaches zero. `labor_days_total` is the norm
/// it started from, FROZEN at start: "invested 40 of 120" (construction
/// design §8) must read the same after a balance edit of the class norm
/// mid-build, and a site is priced once, when it starts.
struct ConstructionState {
  ConstructionPhase phase = ConstructionPhase::kNone;

  /// The level being built: 1 for a new unit, N + 1 for an upgrade of a
  /// unit standing at N (unit rules §11). 0 while demolishing and when
  /// nothing is in progress. While an upgrade is in progress the unit
  /// WORKS at N — and its capacities stay N's until the day the level
  /// moves: the stable keeps its horses under a roof, the house keeps its
  /// family, and nothing grows early (unit rules §11, the rule of
  /// 2026-08-31).
  std::uint8_t target_level = 0;

  /// Game man-days the site started with — the build class's norm of the
  /// target level, or the demolition share of it. Frozen at start.
  float labor_days_total = 0.0F;

  /// Game man-days still to invest. > 0 only in kBuilding and kDemolishing.
  float labor_days_remaining = 0.0F;

  /// At most this many builders on the site at once — the build class's
  /// ceiling, copied from the level's row (unit_levels.csv max_crew) when
  /// the site starts, so that the labor sub-step reads the cap where it
  /// reads the days left and never opens a table. 0 = uncapped (a
  /// demolition, or a class that names none). What keeps "a barn is a
  /// couple of weeks for a brigade" from becoming three days for the whole
  /// village (construction design §8).
  std::uint8_t max_crew = 0;
};

/// @brief One unit. Plain data; behavior comes from the type's table row.
struct UnitRow {
  /// Row of tables/unit_types.csv: barn, warehouse, well, house, heap...
  UnitTypeId type;

  Vec2 position;

  /// Unit level, 1-based on the type's own ladder (unit rules design §11:
  /// the first level IS the built unit). LEVEL 0 MEANS "NOT BUILT": a
  /// marked or unfinished site, or a unit being demolished. A level-0 unit
  /// produces nothing, stores nothing for anyone, houses nobody and holds
  /// no herd — every consumer that reads a unit's level treats 0 as absent
  /// (manual/71-construction.md §2), and that one rule replaces a flag in
  /// every table.
  std::uint8_t level = 1;

  /// The family living here, for house-kind units; invalid otherwise.
  /// One family - one house (families design §1).
  FamilyId household;

  /// What the unit holds, dense by ResourceId: a warehouse's stores, a
  /// stock-yard's feed buffer, a heap's logs or manure — and, while the
  /// unit is a site, the materials delivered for the level being built
  /// (consumed on completion, recipe-exact). Empty vector = holds nothing
  /// yet (sized on first delivery).
  ResourceAmounts stock;

  /// The site block (see ConstructionState). All zeros for a unit that is
  /// simply standing, which is most units most of the time.
  ConstructionState construction;
};

/// @brief The units table type used by WorldState.
using UnitTable = StateTable<UnitId, UnitRow>;

}  // namespace core

#endif  // CORE_COMMON_UNIT_STATE_H_
