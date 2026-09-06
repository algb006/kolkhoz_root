/// @file
/// @brief IProductionSystem — the boundary of the land-and-production subsystem.
/// @threading PARALLEL_WRITE
/// The production phase (slot 4) writes world state from many workers, each
/// over its own range of FIELD rows — and today over nothing else: the only
/// parallel phase in the module is the field-growth one, which writes five
/// members of the owned row — FieldRow::drought_stress, ::wet_stress,
/// ::drought_run_days, ::wet_run_days and ::weather_state — and reads the
/// calendar and weather blocks phase 1 froze. The single ::weather_stress
/// this line used to name was split in two on 2026-09-04, and the label
/// outlived it by two days: a write map naming a field that no longer
/// exists cannot be checked against the code at all. Unit work cycles are sequential, in the
/// decisions sub-step; if they are ever parallelised the write map widens THEN, and this line says
/// so then. A label wider than the truth is worse than none: it stops being
/// an ownership contract a reader can use. All of it strictly under the
/// buffer law (core_sim/step.h). The production-decisions sub-step runs sequentially
/// inside the decisions slot (phase 3) on the sim thread; accessor and
/// factory are wiring-time, sim thread only.
///
/// Subsystem law (state model, manual/52-state-model.md): implementations
/// hold configuration only — every fact about the simulated world lives in
/// WorldState; nothing is carried between steps outside it.
///
/// Responsibilities (stage 4 of the plan): crop growth and harvest, field
/// fertility, unit work cycles with their input and output buffers, herds
/// and their feed. Worker productivity is read from resident state in
/// core_common — this module never names core_residents.
///
/// Stabling the horses (project phase 2, task A7; manual/74-posts.md): the
/// herd day, before billeting, looks for a kolkhoz horse herd still at a
/// private yard while some built unit has a resident holding the groom's
/// post there (ResidentRow::post). Found — every kolkhoz horse herd is
/// merged into one at that unit, ChairmanState::horses_stabled is set,
/// kHorsesStabled is raised. Once per campaign by construction: after the
/// move no such herd exists, and the flag keeps the labor lock from ever
/// returning. The rule is the herd system's because herds are its rows;
/// it recognises the groom by the profession key in labor's tables, which
/// is data, not a dependency on core_labor.

#ifndef CORE_PRODUCTION_PRODUCTION_SYSTEM_H_
#define CORE_PRODUCTION_PRODUCTION_SYSTEM_H_

#include <memory>
#include <vector>

#include "core_sim/step.h"
#include "core_tables/stub_tables.h"

namespace core {

class ITableSet;  // Defined in core_tables (stage 1, task F5).

/// @brief The land-and-production subsystem: owner of step slot 4 and of the
/// production sub-step of the decisions slot.
class IProductionSystem {
 public:
  virtual ~IProductionSystem() = default;

  /// @brief The production slot implementation (phase 4, parallel over
  /// FIELDS — the crop's growth day, and nothing per unit: the unit work of
  /// this subsystem is structure-changing and lives in the sequential
  /// decisions sub-step below). Valid for the lifetime of the system; wired
  /// into StepPhaseSet::production.
  virtual IParallelPhase& ProductionPhase() = 0;

  /// @brief Production decisions: the sub-step of the decisions slot.
  /// Called by core_world every step (phase 3, sim thread), third in the
  /// fixed order of that slot (manual/54-modules.md, §3). Sequential,
  /// structure-changing work of the domain: what each unit produces next
  /// (nomenclature), starting and closing cycles, seasonal transitions of
  /// fields. Runs every tick; daily work gates itself to day boundaries.
  virtual void RunProductionDecisions(const WorldState& previous, WorldState& current) = 0;

  /// @brief Appends the production alarms standing in `completed`
  /// (core_common/alarm_state.h; manual/72-storage-and-alarms.md §3):
  /// kStoreFull for every numbered store at its level's capacity;
  /// kHarvestWaitingOnField for every field with reaped produce waiting
  /// (FieldRow::reaped_grams); kHarvestWillNotFit for every field whose
  /// claim on the shared room — this subsystem's own estimate of everything
  /// that will still arrive from it, straw included — exceeds what is LEFT
  /// of the free room after the fields that will be HARVESTED EARLIER have
  /// spent theirs; kSeedShort for every field whose next sowing the stores
  /// cannot seed to the norm; kHerdStarving for every kolkhoz herd with
  /// unfed_days > 0; kHerdWithoutStable, one line for the whole kolkhoz
  /// horse team, while the yard has not reached its second step.
  /// Each subject at most once. Within a kind the order is the walk's own —
  /// row order for the store, herd and seed kinds, HARVEST ORDER for the
  /// two harvest kinds, because the room is spent in the order the fields
  /// are reaped and not in the order the table happens to hold them. Either
  /// way the session sorts by (kind, subject id), so what the player sees
  /// does not depend on this. A pure read of `completed` with the configuration: no
  /// state of the subsystem changes, no log is written. Called between
  /// steps on the sim thread through ISimulation::CollectAlarms.
  virtual void CollectAlarms(const WorldState& completed, std::vector<Alarm>& alarms) const = 0;

  /// @brief Appends the stock lights this subsystem owns (task: the stock
  /// traffic light). Two: StockKind::kFeed and StockKind::kSeed.
  ///
  /// Both rules are already here and neither can move: the feed rate per
  /// head per day, the pasture season that says which days are stall days,
  /// the sowing norm per hectare and the rotation that says which crop is
  /// next. The lights are those same numbers read forward instead of
  /// backward.
  ///
  /// THE FEED LIGHT COUNTS THE WINTERING, not today (office design §5).
  /// In summer the herd is out and the daily draw on the stores is near
  /// nothing; a light that counted that would stand green until November
  /// and turn yellow when the hay can no longer be cut. It is most useful
  /// in haymaking, and that is only true if it looks at the winter.
  ///
  /// A pure read of `completed` with the configuration; no state changes,
  /// nothing is logged. Called between steps on the sim thread through
  /// ISimulation::CollectStockForecast.
  virtual void CollectStockForecast(const WorldState& completed,
                                    std::vector<StockForecast>& lights) const = 0;

  /// @brief Game days to the next harvest window, 0 while one is open.
  ///
  /// Exposed for the FOOD light, which lives in core_residents with the
  /// eating norms: "will it last to the harvest" needs a date this module
  /// owns and that one may not read from here.
  /// @note Called between steps on the sim thread. A pure read.
  virtual std::int32_t DaysToNextHarvest(const WorldState& completed) const = 0;
};

/// @brief Creates the production subsystem.
/// @param tables Balance tables (crops, unit types, livestock kinds, norms);
///               non-owning, must outlive the returned object.
/// Implemented in core_production (stage 4 of the plan; no-op STUB — task O0).
/// @param stubs Whether this subsystem may be built WITHOUT its balance
///        tables, on the documented defaults (core_tables/stub_tables.h).
///        There is no default value: a caller that has not thought about
///        it cannot be served a different world in silence.
std::unique_ptr<IProductionSystem> CreateProductionSystem(const ITableSet& tables,
                                                          StubTables stubs);

}  // namespace core

#endif  // CORE_PRODUCTION_PRODUCTION_SYSTEM_H_
