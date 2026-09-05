/// @file
/// @brief The catalogue: what the balance tables SAY things are, read once
/// and read here.
/// @threading PARALLEL_READONLY
/// Built at factory time, before the simulation exists, and never written
/// again. Anything holding it may read it from any thread; nothing may add
/// a field the step writes.
///
/// WHAT BELONGS HERE, AND THE RULE THAT KEEPS IT A CATALOGUE. Definitions
/// read out of the tables, and their validation. **No behaviour** — not one
/// function that moved in because two modules called it. A catalogue that
/// accepts code becomes the dump for whatever is common, which is the
/// opposite of what it is for (boss, 2026-09-04).
///
/// WHEN A COLUMN MOVES IN: when it gets a second reader, and not before. A
/// column read by one module belongs to that module; the catalogue is not a
/// place to collect columns, it is the answer to "who owns this one".
///
/// THE TWO THAT MOVED IN FIRST, and why each had to:
///
///   `map.side_m` had THREE readers with THREE policies. core_boundary
///   refused a zero ("the map is missing"), core_construction took a zero
///   to mean "the table set declares no map, so nothing is off it", and
///   core_world's genesis read it with no range at all. One cell, two
///   meanings and one unchecked read — and nothing would ever have made
///   them disagree out loud.
///
///   `unit_types.plot_radius_m` had one reader and a COURIER: core_world
///   carried it from core_construction to core_residents, because the
///   wedding stub needs the same plot rule as an ordered building. The
///   courier was the visible symptom of the missing owner, and it is gone.

#ifndef CORE_CATALOG_DEFINITIONS_H_
#define CORE_CATALOG_DEFINITIONS_H_

#include <cstdint>
#include <string>
#include <vector>

#include "core_common/plot.h"
#include "core_tables/tables.h"

namespace core {

/// @brief What the tables say each unit TYPE is. Indexed by UnitTypeId
/// value throughout; a type the tables do not have is simply out of range,
/// and every lookup checks.
struct UnitTypeDefs {
  /// Plot radius in metres (unit_types.csv `plot_radius_m`). Zero means the
  /// type takes no part in the overlap rule: either it has no plot by
  /// design, or the player draws its outline and the outline is the
  /// presentation's to guard (unit rules §9).
  std::vector<float> plot_radius_m;

  /// The radius NOTHING ELSE MAY COME INSIDE, in metres — the plot where a
  /// type has one, and the type's own body where it does not
  /// (unit_types.csv `footprint_r_m`). This is what the overlap rule reads
  /// (plot.h); `plot_radius_m` above stays the plot alone, because "claims
  /// a plot and names no radius" is a different question and a body must
  /// not answer it.
  ///
  /// TWO RULES, ONE NUMBER, AND THE DESIGN SAYS THEY ARE THE SAME RULE WITH
  /// A DIFFERENT NUMBER (boss, 2026-09-05). A plot says "keep your distance
  /// from my yard"; a body says "you cannot stand where I stand". A well, a
  /// lamp post and a notice board had no plot and therefore took no part in
  /// the rule at all — two of them could occupy the same metre, which the
  /// human called out by name: "two similar objects will overlap each
  /// other… that is not decor". No type carries both numbers, so one span
  /// serves both readings without a choice ever having to be made.
  std::vector<float> keep_out_radius_m;

  /// 1 for every type of the housing class (unit_types.csv `class`). A unit
  /// of one of these with no household in it is a FREE HOUSE, which is what
  /// a wedding needs first (life-cycle §12).
  std::vector<std::uint8_t> is_housing;

  /// @brief Number of types the tables define. Zero in a table-less world.
  std::uint32_t Count() const { return static_cast<std::uint32_t>(plot_radius_m.size()); }
};

/// @brief Everything the catalogue knows, in one immutable object.
struct Definitions {
  UnitTypeDefs units;

  /// Side of the square map in metres (map.csv `side_m`). ZERO MEANS THE
  /// TABLE SET DECLARES NO MAP, and then no position is off it — a world
  /// with no map has no edge to fall off, and refusing every build in a
  /// table-less test would be inventing a rule out of a missing file. This
  /// is now the ONE meaning of a zero here; a consumer that genuinely
  /// cannot work without a map says so itself, on top of this value.
  float map_side_m = 0.0F;

  /// @brief The plot rules as core_common wants them. A span into this
  /// object: it must outlive the call, which it does — the catalogue is
  /// built before the systems and outlives them.
  PlotRules Plots() const {
    return PlotRules{.radius_by_type = units.keep_out_radius_m, .map_side_m = map_side_m};
  }
};

/// @brief Reads the catalogue out of a table set.
/// @param error Receives the reason on failure, table and column named.
/// @return false when a PRESENT table is malformed — a value that is not a
///         number or out of range. A MISSING table keeps the documented
///         defaults, like every subsystem factory: a unit test's world has
///         no tables at all, and then nothing has a plot and there is no
///         map.
bool LoadDefinitions(const ITableSet& tables, Definitions& definitions, std::string& error);

}  // namespace core

#endif  // CORE_CATALOG_DEFINITIONS_H_
