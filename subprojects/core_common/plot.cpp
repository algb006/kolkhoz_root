// The plot rule of plot.h (unit rules §9).

#include "core_common/plot.h"

#include <cstdint>

#include "core_common/state_table_ops.h"

namespace core {
namespace {

float DistanceSquared(const Vec2& from, const Vec2& to) {
  const float dx = from.x - to.x;
  const float dy = from.y - to.y;
  return (dx * dx) + (dy * dy);
}

/// How far the search may walk before it gives up and takes the last place
/// it looked at. Rings of two radii each: at the houses' 25 metres this is
/// six kilometres, half the map, and the village is a thousand metres
/// across at its most crowded. A cap and not a loop without one, because a
/// table that gave every type a radius of a metre would otherwise spin.
constexpr std::int32_t kMaxRings = 128;

/// Whether a plot of `radius` at `place` lies wholly on the map. A table
/// set with no map declared has no edge, and then nothing is off it.
bool OnTheMap(const PlotRules& rules, const Vec2& place, float radius) {
  if (!(rules.map_side_m > 0.0F)) {
    return true;
  }
  return place.x - radius >= 0.0F && place.y - radius >= 0.0F &&
         place.x + radius <= rules.map_side_m && place.y + radius <= rules.map_side_m;
}

}  // namespace

bool PlotOverlaps(const UnitTable& units,
                  const PlotRules& rules,
                  const Vec2& place,
                  float radius,
                  UnitId ignore) {
  if (!(radius > 0.0F)) {
    return false;
  }
  for (std::uint32_t row = 0; row < units.rows.size(); ++row) {
    if (units.row_ids[row].value == ignore.value) {
      continue;
    }
    const UnitRow& other = units.rows[row];
    if (other.type.value >= rules.radius_by_type.size()) {
      continue;
    }
    const float other_radius = rules.radius_by_type[other.type.value];
    if (!(other_radius > 0.0F)) {
      continue;
    }
    const float reach = radius + other_radius;
    if (DistanceSquared(place, other.position) < reach * reach) {
      return true;
    }
  }
  return false;
}

Vec2 FreePlot(const UnitTable& units, const PlotRules& rules, const Vec2& wanted, float radius) {
  if (!(radius > 0.0F)) {
    return wanted;
  }
  if (OnTheMap(rules, wanted, radius) && !PlotOverlaps(units, rules, wanted, radius, UnitId{})) {
    return wanted;
  }
  // Two radii to a step: neighbours placed on the same lattice then just
  // touch, which the strict comparison in PlotOverlaps allows, and the
  // village packs as tightly as the rule permits instead of drifting.
  const float step = radius + radius;
  Vec2 last = wanted;
  for (std::int32_t ring = 1; ring <= kMaxRings; ++ring) {
    // The ring is the square |dx| = ring or |dy| = ring, walked row by row
    // from the top: a fixed order, and the nearer sides come first.
    for (std::int32_t dy = -ring; dy <= ring; ++dy) {
      const bool edge_row = dy == -ring || dy == ring;
      for (std::int32_t dx = -ring; dx <= ring; ++dx) {
        if (!edge_row && dx != -ring && dx != ring) {
          continue;  // the inside of the square was walked by an earlier ring
        }
        const Vec2 candidate{.x = wanted.x + (static_cast<float>(dx) * step),
                             .y = wanted.y + (static_cast<float>(dy) * step)};
        // The edge is asked FIRST, and not only because it is cheaper than
        // a scan of every unit: a candidate off the map is not a place at
        // all, so it must not become `last` either.
        if (!OnTheMap(rules, candidate, radius)) {
          continue;
        }
        if (!PlotOverlaps(units, rules, candidate, radius, UnitId{})) {
          return candidate;
        }
        last = candidate;
      }
    }
  }
  return last;
}

}  // namespace core
