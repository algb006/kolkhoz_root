/// @file
/// @brief IResidentsSystem — the boundary of the people subsystem.
/// @threading PARALLEL_WRITE
/// The subsystem's parallel phases — needs (slot 2) and metrics (slot 5) —
/// write world state from many workers, each over its own range of family
/// rows AND over the resident rows of those families' members: the needs
/// phase moves ResidentRow::satiety and ::health, which is the buffer law's
/// own reading of ownership (an item owns every row that belongs to it, in
/// any table — core_sim/step.h rule 4). Saying "family rows" alone made the
/// map narrower than the truth, and a reader checking the resident writes
/// against it would have called them a violation. Its demography
/// sub-step runs sequentially inside the decisions slot (phase 3) on the sim
/// thread; accessors and factory are wiring-time, sim thread only.
///
/// Subsystem law (state model, manual/52-state-model.md): implementations
/// hold configuration only — every fact about the simulated world lives in
/// WorldState. Scratch buffers inside a phase are fine, but nothing carried
/// between steps; ISimulation::ResetWorld needs no callback here.
///
/// Responsibilities (stage 3 of the plan): resident needs — food from the
/// family pantry, rest, cold, mood; family satisfaction aggregates; and the
/// structural work of demography — births, deaths, marriages, migration.
/// The resident and family row layouts are designed at stage 3, behind this
/// interface; the interface does not change with them.

#ifndef CORE_RESIDENTS_RESIDENTS_SYSTEM_H_
#define CORE_RESIDENTS_RESIDENTS_SYSTEM_H_

#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "core_sim/step.h"
#include "core_tables/stub_tables.h"

namespace core {

class ITableSet;  // Defined in core_tables (stage 1, task F5).

/// @brief The people subsystem: owner of step slots 2 and 6 and of the
/// demography sub-step of the decisions slot.
class IResidentsSystem {
 public:
  virtual ~IResidentsSystem() = default;

  /// @brief The needs slot implementation (phase 2, parallel by family).
  /// Valid for the lifetime of the system; wired into StepPhaseSet::needs.
  virtual IParallelPhase& NeedsPhase() = 0;

  /// @brief The metrics slot implementation (phase 5, parallel by family).
  /// Valid for the lifetime of the system; wired into StepPhaseSet::metrics.
  virtual IParallelPhase& MetricsPhase() = 0;

  /// @brief The residents' sub-step of the decisions slot: demography and,
  /// from stage 6 on, the family-kolkhoz food exchange.
  /// Called by core_world every step (phase 3, sim thread), second in the
  /// fixed order of that slot (manual/54-modules.md, §3). The only place
  /// where resident and family rows are appended or removed — births,
  /// deaths, marriages, migration — and, per manual/66-food-model.md, where
  /// warehouse stock moves into family pantries (the monthly distribution
  /// against trudodni, the minimum ration) and the settlement vitals
  /// (life expectancy) are maintained. The name keeps its stage-3 form for
  /// interface stability; the contract is the whole sub-step. Runs every
  /// tick; the implementation itself gates daily and monthly work.
  virtual void RunDemographyDecisions(const WorldState& previous, WorldState& current) = 0;

  /// @brief Appends the people alarms standing in `completed`
  /// (core_common/alarm_state.h): kFamilyGoingHungry for every family whose
  /// satiety component is under the ration floor of the food configuration
  /// — the same threshold at which the exchange hands out the safety
  /// ration (manual/66-food-model.md §5), so the alarm stands exactly while
  /// the kolkhoz feeds the family by right. Row order within the kind; the
  /// session sorts by id. A pure read with the configuration; no state
  /// changes, nothing is logged. Called between steps on the sim thread
  /// through ISimulation::CollectAlarms.
  virtual void CollectAlarms(const WorldState& completed, std::vector<Alarm>& alarms) const = 0;

  /// @brief Appends the stock lights this subsystem owns (task: the stock
  /// traffic light). Today one: StockKind::kFood.
  ///
  /// The eating norms live here — the grain-equivalent year norm, the age
  /// factors, the kcal reference — so the forecast of how long the food
  /// lasts lives here too, beside them. A panel that reached the same
  /// number from the stores would be right until the first balance change
  /// and wrong afterwards with no sign of it.
  ///
  /// The forecast is deliberately dull (stock_forecast.h): the edible stock
  /// that is there against the eating that is known, to the harvest. It
  /// guesses nothing the player might do.
  ///
  /// A pure read of `completed` with the configuration; no state changes,
  /// nothing is logged. Called between steps on the sim thread through
  /// ISimulation::CollectStockForecast.
  /// @param days_to_harvest Game days to the next harvest window. Passed IN
  ///        because the farming calendar belongs to core_production and this
  ///        module may not reach for it (CLAUDE.md §7): the assembly point
  ///        carries the number across, which is what an assembly point is
  ///        for. The alternative was a second copy of the harvest months,
  ///        and a second copy is a second answer.
  virtual void CollectStockForecast(const WorldState& completed,
                                    std::int32_t days_to_harvest,
                                    std::vector<StockForecast>& lights) const = 0;

  /// @brief How tall this person is, in METRES; 0 for a child and for a
  /// resident who is not there.
  ///
  /// IT LIVES ON THE PEOPLE SUBSYSTEM because people are its rows and
  /// because it owns both halves of the answer: the person's own deviation,
  /// which is a fraction stored on the row, and the base height of their
  /// sex, which is a world_params.csv row this module reads. The core is the
  /// only side of the seam that holds both — the graphics layer scales bone
  /// by the fraction and a story script sees neither — so the comparison the
  /// design asks for by name, "is the wife taller than her husband", can be
  /// made here and nowhere else (boss, 2026-09-06).
  ///
  /// ADULTS ONLY, and named rather than forgotten: the world carries a base
  /// for a man and for a woman and none for the steps of childhood, so a
  /// child's height in metres is a question nobody can answer. The fraction
  /// is valid at every age and is on the row for whoever wants it.
  /// @note Called between steps on the sim thread. A pure read.
  virtual float HeightMeters(const WorldState& completed, ResidentId resident) const = 0;
};

/// @brief Creates the people subsystem.
/// @param tables Balance tables (consumption norms, demography rates);
///               non-owning, must outlive the returned object.
/// Implemented in core_residents (stage 3 of the plan; no-op STUB — task O0).
/// @param stubs Whether this subsystem may be built WITHOUT its balance
///        tables, on the documented defaults (core_tables/stub_tables.h).
///        There is no default value: a caller that has not thought about
///        it cannot be served a different world in silence.

std::unique_ptr<IResidentsSystem> CreateResidentsSystem(const ITableSet& tables, StubTables stubs);

/// @brief The world_params.csv keys this module reads (the spreads of the
/// figure a newborn is given).
///
/// Declared for the assembly to union with every other module's: a module
/// cannot judge the table's `reader` column, because "the core" is more than
/// any one of them (core_catalog/table_value.h).
/// @return A view of a static array; valid for the life of the program.
std::span<const std::string_view> LifeWorldParamKeys();

/// A NOTE ON WHERE THIS STANDS, because putting it anywhere else cost a
/// finding. Declared first between CreateResidentsSystem's doc block and
/// CreateResidentsSystem itself, it took that whole block for itself and
/// left the factory — @param stubs and all — undocumented. FIFTH time in two
/// cycles that inserting a declaration stole the comment above it. A doc
/// comment documents whatever FOLLOWS it, so an insertion point is never
/// neutral: before adding a declaration, ask whose comment you are standing
/// under, and prefer the end of the namespace when the answer is "someone
/// else's".

}  // namespace core

#endif  // CORE_RESIDENTS_RESIDENTS_SYSTEM_H_
