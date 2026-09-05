/// @file
/// @brief Where a unit may stand: the no-overlap rule of unit rules §9 and
/// the search for the nearest place that satisfies it.
/// @threading SINGLE_THREADED
/// Called from the SEQUENTIAL phases only — decisions (the wedding stub in
/// core_residents) and the order book (core_construction). No state of its
/// own.
///
/// "Read-only" is NOT what makes this safe, and the const reference must
/// not be read as a promise that it is. UnitRow is PARALLEL_WRITE in the
/// logistics slot, and both calls here scan the WHOLE units table,
/// including rows another worker would own — FreePlot up to 128 rings'
/// worth of scans. A call from a parallel phase would therefore race over
/// every unit in the settlement. What keeps it safe is the barrier before
/// the sequential slot, and nothing else.
///
/// WHY THE RULE LIVES HERE AND NOT WITH THE ORDER THAT CARRIES IT. It used
/// to live inside core_construction, as a private method of the system that
/// reads the order book — so "no two plots overlap" was enforced on ONE WAY
/// IN. The residents' wedding stub appends its house row straight into the
/// units table, and walked past the guard: three generations of one family
/// came out as three houses at the same coordinates, and every migrant
/// couple in the campaign at the mean of all the others. The layer drew
/// what it was given — a stack of eight identical slabs in the middle of
/// the village (boss, 2026-09-04).
///
/// **A guard on a door does not protect a statement about the room.** "No
/// two plots overlap" is a property of the WORLD; kTooClose was a check on
/// the order book. Whoever came in another way was never checked, and the
/// fix is not a second guard but one home for the rule, reachable from
/// every place a unit row is born.

#ifndef CORE_COMMON_PLOT_H_
#define CORE_COMMON_PLOT_H_

#include <span>

#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/unit_state.h"

namespace core {

/// @brief Everything the plot rule needs that is not in the units table:
/// the radii, and the edge of the world.
///
/// It travels as ONE object because it crosses a module seam as one:
/// core_construction reads both out of the tables, core_world carries them
/// to core_residents, and a settlement whose radii and whose map came from
/// different places would be two rules wearing one name.
struct PlotRules {
  /// The radius nothing else may come inside, in metres, by UnitTypeId
  /// value — the type's PLOT where it has one (unit_types.csv
  /// `plot_radius_m`) and its BODY where it does not (`footprint_r_m`).
  /// One rule with two numbers: a plot says "keep clear of my yard", a body
  /// says "you cannot stand where I stand", and both are answered by the
  /// same comparison. A type outside the span, or one whose radius is zero,
  /// takes no part: it has neither, or the player draws its outline, and an
  /// outline is the presentation's to guard (unit rules §9).
  std::span<const float> radius_by_type;

  /// Side of the square map in metres, from tables/map.csv `side_m`. ZERO
  /// MEANS THE TABLE SET DECLARES NO MAP, and then no position is off it —
  /// the same convention as construction_config.h, and for the same
  /// reason: a wrong edge is worse than no edge.
  float map_side_m = 0.0F;
};

/// @brief Whether a plot of `radius` centred on `place` would overlap the
/// plot of any unit already standing.
/// @param radius Radius of the plot being placed; zero or less means the
///        thing being placed has no plot, and nothing can overlap it.
/// @param ignore A unit to skip — itself, when an existing unit is moved.
/// @note Plots that TOUCH do not overlap: the comparison is strict, so two
///       25-metre yards exactly 50 metres apart are both legal and stable.
/// @note The map edge takes no part here: this answers "is it crowded",
///       and a position off the map is a different refusal with a different
///       name (kRuleForbids, not kTooClose).
bool PlotOverlaps(
    const UnitTable& units, const PlotRules& rules, const Vec2& place, float radius, UnitId ignore);

/// @brief `wanted` itself when it is free, otherwise the nearest place that
/// is free AND on the map, searched outward on a square lattice of
/// 2*`radius`.
///
/// NEVER REFUSES. A wedding may not fall through because the village is
/// crowded (life-cycle §12), so the rings widen until a place is found; the
/// last resort is the last place looked at, which is a real place and not
/// the origin. The walk is a fixed order over integer offsets — no
/// trigonometry, no randomness, no addresses — so the same world always
/// puts the same house on the same spot, on one worker and on many.
///
/// THE EDGE IS OBEYED HERE TOO, and that is the whole point of the rules
/// travelling together. A player's order is REFUSED outside the map
/// (construction design §6); the stub cannot refuse, so it must not go
/// there instead — otherwise the one invariant would again have two doors,
/// which is the defect this file was written to close.
Vec2 FreePlot(const UnitTable& units, const PlotRules& rules, const Vec2& wanted, float radius);

}  // namespace core

#endif  // CORE_COMMON_PLOT_H_
