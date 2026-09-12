/// @file
/// @brief UnitRow — the per-unit state: type, place, stock, and the site it
/// is while it is being built, raised a level or taken down.
/// @threading PARALLEL_READONLY
/// Rows live in WorldState::units under the double-buffer discipline. EVERY
/// write to a unit row is sequential: stores move in the production
/// decisions sub-step (slot 3), houses appear in the demography sub-step,
/// the construction sub-step of the same slot moves the `construction`
/// block, the level, `wear` and `dead`, `paused` is set by the production sub-step
/// reading kPauseUnit and only read by construction and the boundary after
/// it (task A8), and the parallel slots touch no unit at all — slot
/// 4 is split by FIELD (land_state.h), slot 5 by FAMILY (metrics). The
/// instant-delivery stub that used to sit between them was the logistics
/// slot, removed with the seventh phase on 2026-09-05. When logistics becomes
/// real and takes units as its unit of parallelism, that is a threading
/// change and this block changes with it. Structural
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
/// WEAR IS A NUMBER ON THE ROW (project phase 2, task A5;
/// manual/73-wear-and-repair.md). A building ages: 0..100, faster while
/// lived in or worked in, by the amortization years of the build class of
/// the level it stands at; at 100 it is a ruin that still works — nothing
/// vanishes from wear except the start's old houses, which the canon lets
/// collapse (start design §4). Repair is a SITE on the unit, like an
/// upgrade: kDelivering, then kRepairing, and the number returns to zero.
/// An upgrade repairs on its way (unit rules §11). What wear DOES to output
/// and comfort is not the core's yet: this stage makes it grow and be seen.
///
/// What is deliberately NOT here yet: staff assignments, upgrade modules.
/// Fields for them are added when their systems arrive — appending is the
/// cheap extension.

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

  /// Being repaired (task A5; construction design §11, unit rules §15):
  /// the spare parts are on site, labour is being invested, and the unit
  /// WORKS meanwhile at its own level — like an upgrade, a repair is a
  /// site on a standing unit. target_level equals the current level, so
  /// that every reader of the site block sees "nothing moves". At zero the
  /// parts are consumed and UnitRow::wear returns to 0. Appended after
  /// kDemolishing so that no stored value changes meaning.
  kRepairing,

  /// NOT A VALUE, and never written to a save or read from one: the
  /// codecs range-check 0..kConstructionPhaseCount-1 and this is what they check against.
  /// Values are appended BEFORE it — that is the whole rule, and it is a
  /// fact here rather than an instruction somewhere else. A length
  /// written out by hand beside an enum drifts, and four of them already
  /// had (journal_codec.cpp).
  kConstructionPhaseCount,
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

  /// Wear of the building, 0..100 (unit rules §15: 0-25 as new, 26-50
  /// worn but sound, 51-75 visibly decayed, 76-99 falling apart, 100 a
  /// ruin that still works and never vanishes). Grows once a day in the
  /// construction sub-step by the amortization of the STANDING level's
  /// build class — a full scale in `wear_years_idle` years of standing
  /// empty, in `wear_years_in_use` years while a household lives here or
  /// somebody works here today (task A5, manual/73-wear-and-repair.md §2).
  /// Stays 0 for a type with no building (has_wear = 0: a heap, a stack,
  /// a trench) and for a site at level 0. Reset to 0 by a finished repair
  /// or upgrade. The start's old houses begin part worn — each drawn from
  /// the band in construction.csv, 45..60 as shipped, so that they do not
  /// all fall on one night — and are the one type that collapses at 100
  /// (start design §4).
  ///
  /// Written only by the construction sub-step and by genesis; read by the
  /// boundary's UnitSignals::wear and, later, by the systems whose numbers
  /// it will move. Float on purpose: the daily share is well under one
  /// percent, and an integer would truncate it to nothing — the lesson of
  /// the herds and the clothing scales, learned three times already.
  Metric wear = 0.0F;

  /// Production here is STOPPED by the chairman's order (unit rules §5;
  /// task A8). Not demolished and not mothballed — paused: the order says
  /// when, and the unit waits where it stands.
  ///
  /// What it means in the slice, said plainly because it is less than the
  /// design describes: **the building does not wear while it is stopped**
  /// (unit rules §15 — "a standing unit does not wear out"), and the layer
  /// can show the state. The rest of §5 — production halted, workers
  /// released to the pool, supply stopped while carrying continues — needs
  /// unit work CYCLES, and the core has none yet: what a unit does today is
  /// hold a herd, hold goods and wear out. Pausing must not stop the herd
  /// being fed, so it does not touch herd care.
  ///
  /// "Stops when the running cycle ends" (§5) is likewise nothing to
  /// implement yet and everything to remember: when cycles arrive, this flag
  /// is what they will consult, and the ORDER is what will wait.
  std::uint8_t paused = 0;

  /// THE UNIT STANDS AND DOES NOT WORK AT ALL until it is restored — the
  /// wrecked water mill of the first morning (start design; boss's decision
  /// of 2026-09-12). Set by genesis from start_layout.csv and by nothing
  /// else; cleared by a finished repair OR UPGRADE, like `wear` — an upgrade
  /// rebuilds, and a rebuild revives as surely as a repair does.
  ///
  /// The second writer is named here on purpose. In this module the
  /// per-field writer sentence IS the threading contract: it is what a later
  /// reader checks a proposed new writer against before deciding which slot
  /// may touch a unit row, so a list short by one is the shape in which a
  /// parallel writer slips in unchallenged.
  ///
  /// A STATE OF ITS OWN AND NOT A WEAR, because wear never stops anything:
  /// unit rules §15 ends the scale at "a ruin that still works". A hundred
  /// per cent said "worn to the limit and still grinding", which is the
  /// opposite of the picture the canon opens on, and there was no other
  /// number left to say it with.
  ///
  /// And not `paused` either: that is an ORDER the chairman gave and can
  /// take back, and the two would be told apart by nothing once both were
  /// bytes in a row. A dead mill is a fact about the mill.
  std::uint8_t dead = 0;

  /// HOW FAR THE STINK OF THIS SOURCE REACHES TODAY, in metres from the unit
  /// (water design §4). 0 for everything that does not smell, and for a
  /// source that has not started yet.
  ///
  /// METRES AND NOT A SHARE, because the decay is a SPEED and not a term —
  /// boss's rule of 2026-09-06, and the whole reason the design needs only
  /// one decay rule instead of two. The zone shrinks by so many metres a
  /// day, so a big one takes longer to go out than a small one WITHOUT
  /// anybody saying so: a forge that worked a morning reached twenty metres
  /// and is clean by evening; a tannery that ran a season stands at its full
  /// two hundred and still stinks a week into its idleness. Give the decay a
  /// LENGTH instead and you must then explain why the forge's is different.
  ///
  /// Grows while the source emits, up to the full radius of its strength;
  /// falls when it does not. Written once a day by the construction
  /// sub-step, read by the two seam queries. A unit whose type does not
  /// smell keeps this at zero for ever.
  float stink_radius_m = 0.0F;
};

/// @brief The units table type used by WorldState.
using UnitTable = StateTable<UnitId, UnitRow>;

}  // namespace core

#endif  // CORE_COMMON_UNIT_STATE_H_
