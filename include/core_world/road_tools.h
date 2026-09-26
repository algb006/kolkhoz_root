/// @file
/// @brief The player's road tools on the world side (delivery 7b): what the
///        tracer needs out of the tables, read once at assembly, and the
///        world it needs out of the state, gathered per call.
/// @threading SINGLE_THREADED
/// Called between steps on the sim thread (PreviewRoad), and from 7c by the
/// order that lays a road in the step's sequential slot. The obstacle raster
/// is built on the FIRST call and kept: a pure function of the tables, so
/// building it late changes no answer, and a run that never traces a road
/// never pays its memory (kObstacleCellMetres names the price).

#ifndef CORE_WORLD_ROAD_TOOLS_H_
#define CORE_WORLD_ROAD_TOOLS_H_

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/map_obstacles.h"
#include "core_common/obstacle_raster.h"
#include "core_common/road_draft.h"
#include "core_common/road_trace.h"

namespace core {

class ITableSet;
struct WorldState;

/// @brief The tables' half of the road tools.
class RoadTools {
 public:
  /// @brief Reads map_areas, map_lines, the road levels, the plot radii, the
  ///        map's side and the grove's timber.
  /// @return Nothing, with `error` set, when a present table is malformed.
  static std::optional<RoadTools> Read(const ITableSet& tables, std::string& error);

  /// @brief The trace of `draft` on `world` (road_trace.h): what the preview
  ///        shows on the completed world and what kLayRoad lays on the
  ///        step's — one object for both, so the two cannot trace apart.
  RoadDraftResult Trace(const WorldState& world, const RoadDraft& draft) const;

  /// @brief The pieces `selection` takes on `world` for `operation`
  ///        (road_pieces.h): what the tools 6-9 show before the order.
  RoadPieces Select(const WorldState& world,
                    const RoadSelection& selection,
                    RoadOperation operation) const;

  /// @brief Every tool of the roads menu on `world` (roads design §9, «Дизаблим
  ///        но не скрываем»): a path and a dirt road open (7c); a surface its
  ///        epoch has not opened kByEpoch; the rest kNotYetBuilt until their
  ///        part lands (road work 7e, the demolition 7d). STUB for those —
  ///        and kNoMaterial and kNothingToWork are not produced yet at all
  ///        (they come with the paved tools and the selection).
  RoadToolStates ToolStates(const WorldState& world) const;

  /// @brief The raster, built if it was not — for a test's count and the
  ///        delivery's price. Null when the tables carry no map areas.
  const ObstacleRaster* Raster() const;

  /// @brief Every unit of `world` as the tracer sees it: the body's circle,
  ///        or plot_core_share of the plot's. The door the preview uses, so
  ///        an instrument asking it measures the same circles.
  std::vector<RoadUnitDisc> UnitDiscs(const WorldState& world) const;

 private:
  RoadTools() = default;

  MapObstacles obstacles_;
  std::vector<RoadFord> fords_;
  std::vector<std::vector<Vec2>> village_;
  /// By RoadSurface: the price per 100 m (its `open` set per call from the
  /// world's epoch) and the epoch each opens in.
  std::array<RoadSurfaceCost, kRoadSurfaceSlots> costs_{};
  std::array<Epoch, kRoadSurfaceSlots> opens_{};
  std::vector<float> plot_radius_m_;      ///< By UnitTypeId; 0 — no plot.
  std::vector<float> keep_out_radius_m_;  ///< The plot's, or the body's.
  float map_side_m_ = 0.0F;

  /// world_params `road_access_m` (units rules §12): how near a road must
  /// come to a unit for the unit to be on the network.
  float road_access_m_ = 10.0F;
  float timber_m3_per_ha_ = 0.0F;
  RoadTraceConfig config_;

  /// Built on first use (see @file). shared_ptr so the object stays copyable
  /// for std::optional; nothing else shares it.
  mutable std::shared_ptr<ObstacleRaster> raster_;
};

}  // namespace core

#endif  // CORE_WORLD_ROAD_TOOLS_H_
