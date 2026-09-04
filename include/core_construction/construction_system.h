/// @file
/// @brief IConstructionSystem — the boundary of the construction subsystem:
/// a unit is built over time and costs materials and labour days.
/// @threading SINGLE_THREADED
/// The subsystem owns no phase slot: all its work runs sequentially inside
/// the decisions slot (phase 3), on the sim thread, called by core_world
/// LAST in that slot's fixed order — after assignments, demography and
/// production decisions (manual/54-modules.md §3). It never sees worker
/// threads and does not know core_sim. Sequential by decision: a site
/// appears, changes level and disappears, and every one of those is a
/// change of table shape, which only a sequential slot may make (buffer-law
/// rule 6, core_sim/step.h). The volume — a handful of sites a year — would
/// never earn a parallel phase in any case.
///
/// Subsystem law (state model, manual/52-state-model.md): implementations
/// hold configuration only — every fact about the simulated world lives in
/// WorldState. A site is the unit row itself (unit_state.h,
/// ConstructionState): nothing is kept between steps outside the state.
///
/// WHAT IT DOES (project phase 2, task A2; manual/71-construction.md):
///   * consumes the four construction orders of the order book —
///     kBuildUnit (mark), kStartBuild, kUpgradeUnit, kDemolishUnit — each
///     decided IN THE STEP IT IS READ: kDone or kRefused, never kAccepted
///     or kActive. The order is the chairman's word; the work that follows
///     is the unit's state, visible in its row, not the order's;
///   * checks what an order needs checked — that the type exists and the
///     player may build it, that its gate is open (unlocks design §1), that
///     the plot does not overlap a neighbour's (unit rules §9), that a unit
///     to be demolished houses nobody and holds no herd (unit rules §14);
///   * moves a site through its phases at the day boundary (construction
///     design §7): marked → delivering → building → built; or → demolishing
///     → gone. While delivering, the instant-delivery STUB of project phase
///     1 brings whatever the recipe still lacks from the stores in row
///     order — task A4 replaces this with real logistics behind the same
///     seam, which is the site's own `stock`;
///   * sets the labour seam when a site starts building or dismantling —
///     construction.labor_days_total and labor_days_remaining — and reads
///     it back: at zero the level moves and the recipe is consumed, or the
///     row is removed. The labor sub-step drains it; the two modules never
///     call each other (manual/65-labor-model.md §2);
///   * emits kUnitBuilt and kUnitDemolished into the step's outbox
///     (event_state.h); the order events themselves are the events slot's,
///     which sweeps terminal rows (order_state.h);
///   * WEAR (project phase 2, task A5; manual/73-wear-and-repair.md): at
///     the day boundary every built unit whose type has a building ages
///     by the amortization of the level it stands at — a full scale in
///     `wear_years_idle` years standing empty, in `wear_years_in_use`
///     years while a household lives there or somebody works there today
///     (a herd's crew at its barn; a building crew does not count, the
///     site is not "working"). Clamped at 100: a ruin still works and
///     never vanishes — except the start's old houses, which begin part
///     worn (genesis draws the band from construction.csv),
///     age by their own `old_house_collapse_years`, and at 100 are
///     REMOVED with kUnitCollapsed, their household left without a house
///     for the demography sub-step to rehouse the next day (start design
///     §4, housing design §10);
///   * REPAIR, the fifth order: kRepairUnit opens a site on a standing
///     unit exactly as an upgrade does — kDelivering for the spare parts,
///     kRepairing for the labour, the unit working meanwhile — and at the
///     end consumes the parts and sets wear to 0 with kUnitRepaired. Cost
///     is the level's norm times repair_labor_share, times wear / 100 at
///     the order, and repair_spare_parts_per_labor_day pieces per man-day
///     of it: a neglected repair is dearer in both (construction design
///     §11), and a building takes nothing but spare parts (§2). A finished
///     UPGRADE sets wear to 0 as well — "an upgrade repairs on its way"
///     (unit rules §11) — which is why the two never race: a unit is one
///     site at a time.
///
/// WHAT IT DOES NOT DO, and who will:
///   * seasons of building (winter stops masonry, not carpentry —
///     construction design §8): STUB, builds year-round. A per-class
///     property when the design names it;
///   * site preparation — clearing trees, levelling (construction design
///     §7): STUB of zero cost, the core has no trees and no relief;
///   * the road condition (unit rules §12): STUB, every site is reachable.
///     The alarm "no road" is task A3's, the roads themselves later;
///   * a brigade's size is DATA, not this module's rule: `max_crew` per
///     level in unit_levels.csv (the build class's ceiling — twelve on the
///     stable, eight on a barn; boss decision 2026-08-31). The subsystem
///     exposes it through the seam it already has: a site's crew cap rides
///     with the site (unit_state.h, ConstructionState::max_crew), and the
///     labor sub-step reads it there like it reads the days left;
///   * hired brigades and district builders (construction design §9): the
///     three currencies are Epoch II and the district system;
///   * fields as an obstacle (unit rules §13): the core has a field's centre
///     and area, not its contour. The presentation, which has the contour,
///     guards it — the same division as with the drawn outlines of heaps;
///   * what wear DOES to numbers — a production unit's output, a barn's
///     warmth, a house's comfort (unit rules §15) — is not this module's
///     and not this stage's: task A5 makes wear grow and be seen
///     (UnitSignals::wear, the layer's keyframes), the systems that own
///     those numbers read the row when they are ready. STUB by decision
///     (boss, 2026-09-03: "today, only what is visible");
///   * pause slowing wear (unit rules §5): STUB, kPauseUnit has no
///     consumer yet and the row carries no paused flag; weather, season
///     and insulation moving the rate (§15): STUB, the design names the
///     direction and no number. Both are multipliers on the daily share
///     when they arrive, nothing else moves;
///   * roads, vehicles and their own wear (unit rules §15: kilometres,
///     not years): the core has neither, and the design says their wear
///     is a different counter — mileage over resource, repaired at the MTS
///     by a mechanic (vehicles design §6). Not this order, not this stage.

#ifndef CORE_CONSTRUCTION_CONSTRUCTION_SYSTEM_H_
#define CORE_CONSTRUCTION_CONSTRUCTION_SYSTEM_H_

#include <memory>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/deadline.h"
#include "core_common/world_state.h"

namespace core {

class ITableSet;  // Defined in core_tables.

/// @brief The construction subsystem: sites, levels and demolition inside
/// the decisions slot.
class IConstructionSystem {
 public:
  virtual ~IConstructionSystem() = default;

  /// @brief The construction sub-step of the decisions slot.
  /// Called by core_world every step (phase 3, sim thread), last in the
  /// fixed order of that slot: a site that finishes here is seen by every
  /// consumer from the next tick, and a site that starts here gets its
  /// crew at the next morning's placement — the day after the order, which
  /// is the design's own rule for every change of assignment (time design
  /// §11). Runs every tick and gates its own cadence: orders are consumed
  /// on the tick they appear, phase transitions and the delivery stub run
  /// at the day's first tick, completion the tick the seam reaches zero.
  /// @note Writes UnitRow::construction, level, stock and wear of the units
  /// it works on; appends a unit row per kBuildUnit; removes the row of a
  /// finished demolition and of a collapsed old house; moves order rows to
  /// kDone / kRefused; appends kUnitBuilt / kUnitDemolished / kUnitRepaired
  /// / kUnitCollapsed to the outbox. Reads residents (a house's household,
  /// who works where today) and herds (what stands where). Writes ONE field
  /// of another module's row and says so: a collapse clears
  /// FamilyRow::house of the household that lived there — the house is
  /// gone and the row must not point at a dead id; rehousing is the
  /// demography sub-step's, the next day (task A5).
  virtual void RunConstructionDecisions(const WorldState& previous, WorldState& current) = 0;

  /// @brief Appends the construction alarms standing in `completed`
  /// (core_common/alarm_state.h): kSiteWithoutMaterials for every site in
  /// kDelivering whose recipe the stores cannot complete — the first
  /// material short in recipe order and the grams short of it, so the
  /// presentation can say "the barn waits for 4 t of boards"; kNoRoad is in
  /// the roster and yields nothing (STUB: the core has no roads). Row order
  /// within the kind; the session sorts by id. A pure read with the
  /// configuration; nothing changes, nothing is logged. Called between
  /// steps on the sim thread through ISimulation::CollectAlarms.
  virtual void CollectAlarms(const WorldState& completed, std::vector<Alarm>& alarms) const = 0;

  /// @brief How long this unit has before its wear reaches the scale's end,
  /// at the rate it is wearing today (unit rules §15).
  ///
  /// TWO READERS, ONE CALCULATION. The HUD's wear layer compares it with a
  /// date and paints; the story reads the days and can set a quest WHILE THE
  /// HOUSE IS STILL STANDING. That is the whole point of a deadline over a
  /// level: `wear = 92` does not say "four days", and the story that can
  /// only see the level can react but never warn.
  ///
  /// IT LIVES HERE because the rate does: the amortisation term of the level,
  /// the type's own pace, and whether anybody is using the unit today. A
  /// panel that reached the same number from `wear` would be right until the
  /// first table change and wrong afterwards with no sign of it.
  ///
  /// THE FORECAST IS DULL ON PURPOSE, like the stock lights: today's rate,
  /// carried forward. It does not guess that somebody will move in, that the
  /// chairman will repair it, or that the unit will be paused.
  ///
  /// @return kDays with the days left, 0 when the unit is already at the end
  ///         of the scale; kNever for a unit that is not wearing at all
  ///         right now (a stopped one) — an answer that changes the moment it
  ///         is started; kNotApplicable for a unit that has no wear and never
  ///         will (a stack, a heap, a trench, a site); kNoData when the
  ///         tables carry no term to compute with.
  /// @note Called between steps on the sim thread. A pure read.
  virtual Deadline WearDeadline(const WorldState& completed, UnitId unit) const = 0;
};

/// @brief Creates the construction subsystem.
/// @param tables Balance tables; non-owning, must outlive the returned
///               object. Reads unit_types.csv (era, player_built, gate,
///               gate_ref, has_plot, plot_radius_m), unit_levels.csv (the
///               ladder: labor_days, build_class and max_crew per level),
///               unit_level_cost.csv (the recipe per level, in each
///               resource's own unit), resources.csv (`measure` and
///               `kg_per_unit`: the bridge to the grams every store counts
///               in — grams = amount x kg_per_unit x 1000, one rule for
///               tonnes, litres, cubic metres and pieces alike) and
///               construction.csv (the demolition share; since task A5 the
///               repair knobs — repair_labor_share,
///               repair_spare_parts_per_labor_day, old_house_collapse_years).
///               Task A5 also reads `has_wear` of unit_types.csv and
///               `wear_years_idle` / `wear_years_in_use` of unit_levels.csv;
///               a table set without those columns has no wear at all,
///               which is the honest reading of "no data".
/// @return nullptr when a present table is malformed — including a type
///         that says it has a plot and names neither a radius nor the
///         marking class (unit rules §9: "no third legal case"), a level
///         whose recipe names a resource the tables do not carry a mass
///         for, or a buildable type with no level 1. Missing tables mean
///         the documented defaults, like every subsystem factory — a
///         table-less world simply has nothing that can be built.
std::unique_ptr<IConstructionSystem> CreateConstructionSystem(const ITableSet& tables);

}  // namespace core

#endif  // CORE_CONSTRUCTION_CONSTRUCTION_SYSTEM_H_
