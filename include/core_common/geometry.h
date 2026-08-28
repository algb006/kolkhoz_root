/// @file
/// @brief Map coordinate system and the two-dimensional vector of the core.
/// @threading PARALLEL_READONLY
/// Type aliases and compile-time constants only; no mutable state. Readable
/// from any phase and any thread.
///
/// The core never includes engine headers, so it carries its own vector type
/// (architecture, §2). The world is flat for simulation purposes: the map is
/// 10 x 10 km, positions are metres in the XY plane, height matters only to
/// the presentation. All travel-time and distance math works on this plane.
///
/// Precision: float at coordinate 10'000 m resolves ~1 mm — far below
/// anything the simulation distinguishes.

#ifndef CORE_COMMON_GEOMETRY_H_
#define CORE_COMMON_GEOMETRY_H_

namespace core {

/// @brief A distance or coordinate, in metres.
using Meters = float;

/// @brief Side length of the square map (world design: 10 x 10 km).
inline constexpr Meters kMapSizeMeters = 10'000.0f;

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
