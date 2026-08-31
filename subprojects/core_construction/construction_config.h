// Internal to core_construction: the parsed build data.
//
// Five tables become four arrays and one scalar, all indexed the way the
// core indexes everything — by DefId, which is the row number of the table
// the id came from (core_common/ids.h). Parsed once at factory time; the
// simulation never opens a table again.
//
// The policy is every factory's: a MISSING table or key keeps the canonical
// default — a unit test's world has no tables at all, and then nothing can
// be built, which is the honest answer — while a PRESENT row that cannot be
// read or contradicts another table refuses the whole subsystem.

#ifndef CORE_CONSTRUCTION_CONSTRUCTION_CONFIG_H_
#define CORE_CONSTRUCTION_CONSTRUCTION_CONFIG_H_

#include <cstdint>
#include <string>
#include <vector>

#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_common/world_state.h"

namespace core {

class ITableSet;  // Defined in core_tables.

/// What opens a unit type (unlocks design §1). One gate per type, never two.
enum class UnitGate : std::uint8_t {
  kEra = 0,  ///< Its era has come. The default, and 97 of 106 types.
  kStart,    ///< Stands from day one; nothing to build, ever.
  kEvent,    ///< A way-of-life event. Closed in the core until phase III.
  kQuest,    ///< A quest. Closed until phase III.
  kUnit,     ///< A predecessor on the map. Closed until phase III.
};

/// One material line of one level's recipe, already in grams.
struct BuildMaterial {
  ResourceId resource;

  /// The recipe's amount converted once, at parse time: amount x
  /// kg_per_unit x 1000 (resources.csv). The core counts every store in
  /// grams (state model §5), the tables state each resource in its own
  /// measure, and this is the single place the two meet.
  Grams grams = 0;
};

/// One step of a unit type's ladder (unit_levels.csv, unit_level_cost.csv).
struct BuildLevel {
  /// Game man-days of the build class of THIS level, already divided by
  /// kRealDaysPerGameDay: the tables carry the agronomy books' real
  /// man-days, every consumer divides once at parse time (calendar.h).
  float labor_days = 0.0F;

  /// The build class's brigade ceiling; 0 = none named.
  std::uint8_t max_crew = 0;

  /// The era this level belongs to (1..3): an upgrade waits for its era
  /// even when the unit itself is older.
  std::uint8_t era = 1;

  /// 0/1: the class is "marking" — the player draws the outline and there
  /// is neither work nor material (build_class `plot`). Such a level is
  /// finished the moment it starts.
  std::uint8_t is_marking = 0;

  std::vector<BuildMaterial> recipe;
};

/// Everything the subsystem knows about one unit type.
struct BuildType {
  UnitGate gate = UnitGate::kEra;

  /// 0/1 from unit_types.csv: whether the player may raise it at all.
  std::uint8_t player_built = 0;

  std::uint8_t era = 1;

  /// Plot radius in metres; 0 = takes no plot in the core's arithmetic
  /// (either none by design, or an outline the player draws — the
  /// distinction lives in the tables, not here: both mean "not in the
  /// overlap check", unit rules §9).
  float plot_radius_m = 0.0F;

  /// Levels 1..N, dense: index 0 is level 1. Empty for a type with no
  /// ladder at all, which is what "cannot be built" looks like in data.
  std::vector<BuildLevel> levels;
};

/// The subsystem's own knobs (construction.csv) and the parsed tables.
struct ConstructionConfig {
  /// Demolition costs this share of the level's build norm in labour days
  /// (construction design §12: "noticeably less than building"). ASSUMPTION.
  float demolition_labor_share = 0.25F;

  /// By UnitTypeId value. Sized to the unit_types table; a type the tables
  /// do not have is simply out of range, and every lookup checks.
  std::vector<BuildType> types;
};

/// @brief Reads the five tables into `config`.
/// @param error Receives the reason on failure, table and row named.
/// @return false when a present table is malformed or contradicts another —
///         a type that claims a plot and names neither radius nor marking
///         class, a recipe naming a resource with no mass, a buildable type
///         with no level 1.
bool ParseConstructionConfig(const ITableSet& tables,
                             ConstructionConfig& config,
                             std::string& error);

}  // namespace core

#endif  // CORE_CONSTRUCTION_CONSTRUCTION_CONFIG_H_
