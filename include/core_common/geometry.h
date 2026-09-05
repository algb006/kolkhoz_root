/// @file
/// @brief Map coordinate system and the two-dimensional vector of the core.
/// @threading PARALLEL_READONLY
/// A type alias and a plain-data aggregate; no mutable state. Readable from
/// any phase and any thread.
///
/// The core never includes engine headers, so it carries its own vector type
/// (architecture, §2). The world is flat for simulation purposes: the map is
/// square, its side is DATA and not a constant of this file (see Vec2 below),
/// positions are metres in the XY plane, height matters only to the
/// presentation. All travel-time and distance math works on this plane.
///
/// Precision: float at coordinate 12'000 m resolves ~1 mm — far below
/// anything the simulation distinguishes.

#ifndef CORE_COMMON_GEOMETRY_H_
#define CORE_COMMON_GEOMETRY_H_

#include <cmath>

namespace core {

/// @brief A distance or coordinate, in metres.
using Meters = float;

/// @brief A point or displacement on the map plane, in metres.
/// Origin is the map's south-west corner; x grows east, y grows north; a
/// coordinate is never negative. HOW BIG THE MAP IS IS NOT HERE and is not a
/// constant of the core: it is `side_m` of tables/map.csv, exported from
/// db/map.db, and maps differ in size. It used to be `kMapSizeMeters` here
/// as well; the map grew from ten kilometres to twelve, the copy did not,
/// and for a day the core carried a scene reaching past the edge it
/// declared. Read the side, never hold it: the world reads it at genesis,
/// construction validates against it, and the presentation asks the session
/// (core_boundary/session.h, MapSideMeters).
/// Plain data: arithmetic helpers are free functions of the core_common
/// implementation, not members, so the type stays a trivially copyable
/// aggregate.
struct Vec2 {
  Meters x = 0.0f;

  Meters y = 0.0f;
};

/// @brief Game hours of one-way travel between two places at a given pace.
/// @param hours_per_km Game hours per kilometre for the traveller: on foot
///        or behind a harness, from transport.csv through the config that
///        owns it. NOT a constant — a village-wide half-hour cost this
///        project a third of every working hour it had measured
///        (resident_activity.h, 2026-09-05).
///
/// ONE HOME FOR THE ARITHMETIC. It was written in core_labor and needed
/// again by the resident's activity and by the site that nobody can reach;
/// three copies of a square root is how three answers about one road come
/// to disagree.
inline float TravelHoursBetween(const Vec2& from, const Vec2& to, float hours_per_km) {
  const float dx_km = (from.x - to.x) / 1000.0F;
  const float dy_km = (from.y - to.y) / 1000.0F;
  return std::sqrt((dx_km * dx_km) + (dy_km * dy_km)) * hours_per_km;
}

}  // namespace core

#endif  // CORE_COMMON_GEOMETRY_H_
