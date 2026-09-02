/// @file
/// @brief Map coordinate system and the two-dimensional vector of the core.
/// @threading PARALLEL_READONLY
/// Type aliases and compile-time constants only; no mutable state. Readable
/// from any phase and any thread.
///
/// The core never includes engine headers, so it carries its own vector type
/// (architecture, §2). The world is flat for simulation purposes: the map is
/// square with kMapSizeMeters on a side, positions are metres in the XY
/// plane, height matters only to the presentation. All travel-time and distance math works on this
/// plane.
///
/// Precision: float at coordinate 12'000 m resolves ~1 mm — far below
/// anything the simulation distinguishes.

#ifndef CORE_COMMON_GEOMETRY_H_
#define CORE_COMMON_GEOMETRY_H_

namespace core {

/// @brief A distance or coordinate, in metres.
using Meters = float;

/// @brief Side length of the square map, in metres.
///
/// TWELVE kilometres since the human's decision of 1 September 2026. It was
/// ten, and the start layout moved to the new map while this line did not —
/// so the core carried a scene reaching y = 10934 inside a world it declared
/// 10000 wide, and the graphics layer, which takes its terrain size from
/// here, would have built a 10 x 10 landscape under a 12 x 12 map and left
/// the cemetery, the north forest and two contours off the edge.
///
/// THIS IS A COPY, and the only reason it is a constant is that nothing
/// exports the side yet. The one home of the number is `db/map.db`, table
/// `map`, column `side_m`: maps differ in size and the limits are measured
/// against whichever is loaded. When the side arrives in a core table this
/// constant goes away — until then, a map of another size means editing this
/// line, and PlaceStartLayout says so out loud when a layout row falls
/// outside it (core_world/genesis.cpp).
inline constexpr Meters kMapSizeMeters = 12'000.0f;

/// @brief A point or displacement on the map plane, in metres.
/// Origin is the map's south-west corner; x grows east, y grows north. Valid
/// world positions lie in [0, kMapSizeMeters] on both axes.
/// Plain data: arithmetic helpers are free functions of the core_common
/// implementation, not members, so the type stays a trivially copyable
/// aggregate.
struct Vec2 {
  Meters x = 0.0f;

  Meters y = 0.0f;
};

}  // namespace core

#endif  // CORE_COMMON_GEOMETRY_H_
