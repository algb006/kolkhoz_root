/// @file
/// @brief core_world boundary: world genesis and the assembled simulation.
/// @threading SINGLE_THREADED
/// Everything here is called from the sim thread: the factories at setup, and
/// the composite decisions/events slots that core_world implements internally
/// run in sequential phases. Worker threads never enter this module.
///
/// core_world is the composition root (manual/54-modules.md): it creates the
/// subsystems, wires the StepPhaseSet and owns the two composite sequential
/// slots. The decisions slot (phase 3) calls the subsystems' sub-steps in a
/// fixed order — assignments (core_labor), then demography (core_residents),
/// then production decisions (core_production); the events slot (phase 6) is
/// a STUB until the event system exists (project phase 3). Consumers — unit
/// tests, the balance run, later the UE layer — see only the two factories
/// below and the ISimulation they yield.

#ifndef CORE_WORLD_WORLD_H_
#define CORE_WORLD_WORLD_H_

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "core_tables/stub_tables.h"

namespace core {

class ITableSet;            // Defined in core_tables (stage 1, task F5).
class IConstructionSystem;  // Defined in core_construction.

/// @brief Creates the starting world: the settlement the campaign begins with.
/// The genesis of the design's start conditions — 80 residents, 21 yards,
/// 160 ha of arable land — built from the balance tables, deterministically
/// from the seed: same tables, same seed — same settlement.
/// @param tables     Balance tables; used during the call only.
/// @param capacities The construction subsystem, used ONLY to ask what a
///                   unit type holds at a level (StorageCapacityGrams), so
///                   that the start stock is measured against the ladder
///                   the rest of the core measures against. `nullptr` is
///                   legal and means "no capacity is known here": the stock
///                   is then placed as the table writes it, which is the
///                   answer a world without unit_levels.csv always gave.
///                   THE ARGUMENT EXISTS TO CLOSE A SECOND READER: genesis
///                   used to parse unit_levels.csv on its own, so one
///                   quantity had two homes and two sets of bands (boss,
///                   2026-09-06). Used during the call only.
/// @param world_seed Campaign seed; stored in the returned state.
/// @param error   Where the start layout's refusal goes: the parser of
///                tables/start_layout.csv names the ROW and the COLUMN it
///                choked on, and a caller that can refuse (the assembly
///                below) turns that into a refusal. Written only on that
///                one failure; untouched otherwise.
///
///                THERE IS NO DEFAULT, deliberately, and for the same reason
///                StubTables has none: this world is still returned when the
///                layout is refused — people-only, no houses, no fields —
///                and a caller who has not thought about it would take that
///                for the designed start. `nullptr` is the sentence "I have
///                nowhere to report it", and tools and unit tests say it out
///                loud. Everything ELSE that is missing is not an error and
///                never reaches here: an absent table is not a wrong one.
/// @note STUB until stage 3: returns a world at day 0 with weather, epoch,
/// seed and plan defaults but no resident, family, field or unit rows —
/// exactly enough for the empty-world criterion of stage 1. Of the tables
/// only the campaign setup is read (day-zero weekday); a missing campaign
/// table means the documented defaults.
/// @param stubs Whether a set without the tables genesis reads is
///        legitimate; passed on to the catalogue, which owns unit_types and
///        map and refuses their absence by name
///        (core_catalog/definitions.h).
WorldState CreateStartWorld(const ITableSet& tables,
                            StubTables stubs,
                            const IConstructionSystem* capacities,
                            std::uint64_t world_seed,
                            std::string* error);

/// @brief The world_params.csv keys genesis reads (the figure of a person).
///
/// Declared so that the assembly can union them with every other module's
/// and judge the table's `reader` column as a whole. A module cannot judge
/// "the core" — it is not the core — and this list is the honest half a
/// module can answer. See core_catalog/table_value.h for what the union is
/// for and what it caught.
/// @return A view of a static array; valid for the life of the program.
std::span<const std::string_view> GenesisWorldParamKeys();

/// @brief Everything CreateStandardSimulation needs.
struct StandardSimulationConfig {
  /// Balance tables; non-owning — the caller keeps them alive for the whole
  /// lifetime of the returned simulation.
  const ITableSet* tables = nullptr;

  /// Campaign seed for CreateStartWorld and every derived random draw.
  std::uint64_t world_seed = 0;

  /// 0 = one worker per hardware core minus one; 1 = the mandatory
  /// verification mode. Results are identical for every value — that is the
  /// determinism check, not an option.
  std::uint32_t worker_count = 1;

  /// May the subsystems be built WITHOUT their balance tables, on the
  /// documented defaults (core_tables/stub_tables.h)?
  ///
  /// THE DEFAULT IS THE REFUSAL, and that is the whole correction. Every
  /// factory of this core used to fall back to its defaults in silence, so
  /// an INCOMPLETE table set was indistinguishable from a complete one —
  /// and on 2026-09-05 that cost a day of measurements taken in a climate
  /// the game does not have. Silence now means the strict answer: a caller
  /// who has thought about it says kAllowed, and a caller who has not
  /// cannot be quietly served another world.
  StubTables stub_tables = StubTables::kRefused;
};

/// @brief Creates the fully wired simulation: subsystems, phases, engine.
/// @pre config.tables != nullptr — the caller supplies a loaded table set;
/// the default-constructed config is a template to fill in, not a valid
/// input (asserted in Debug).
/// The returned object owns the subsystems and the step engine; destroying
/// it releases everything. World lifecycle from here on: AdvanceStep to run,
/// CompletedState to observe, ResetWorld to rewind — subsystems hold no
/// world state, so rewinding needs no notification (subsystem law,
/// manual/52-state-model.md).
/// @return nullptr when a subsystem factory refuses its configuration — a
///         malformed balance table, or, under the default
///         stub_tables = StubTables::kRefused, a table that is not there at
///         all. The refusing factory logs which, and names the table.
///         The absent case is new since 2026-09-06: it used to be served
///         silently from the subsystem's own defaults. Since the same day a
///         REFUSED START LAYOUT is the other way in: a scene whose row or
///         column cannot be read is not a scene, and the sentence naming
///         which row and which column is logged before the nullptr.
/// Implemented in core_world (stage 1, task O2 wires the stubs of task O0).
std::unique_ptr<ISimulation> CreateStandardSimulation(const StandardSimulationConfig& config);

}  // namespace core

#endif  // CORE_WORLD_WORLD_H_
