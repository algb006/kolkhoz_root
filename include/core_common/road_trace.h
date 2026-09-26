/// @file
/// @brief The road tracer (roads design §9, «Свободная прокладка» and
///        «Инструмент прокладки»; delivery 7b): a draft of two, three or four
///        points in, the axis it would lay and everything said about it out
///        (RoadDraftResult, road_draft.h). One function for the preview and
///        for the order that lays it (7c), so the two cannot disagree on the
///        same world.
/// @threading PARALLEL_READONLY
/// A pure function of the site and the draft: no state, no randomness beyond
/// a hash of the draft itself, so the same draft on the same world traces to
/// the bit.
///
/// HOW IT TRACES.
///   * TWO POINTS: a straight line with its surface's gentle wander (terrain
///     design, «Лёгкая извилистость в плане»: amplitude and wave by surface,
///     none within kWanderCalm of either end). It goes round nothing: what is
///     in the way is a red span (§9: «Не огибает»).
///   * THREE OR FOUR: a smooth curve through the points (centripetal
///     Catmull-Rom). Where a stretch between two points is in the way, a
///     path is searched round it inside a band along the chord (the band is
///     boss [64] item 4), pulled taut and smoothed; no path — kNoWayRound on
///     that stretch, the curve left red where it was («Молча дорогу в другое
///     место не уводят»). A drawn curve is not waved further.
///   * THE ENDS snap to a road's axis near them (a new junction) or to a
///     junction or road end near them.
///   * EVERY SAMPLE of the axis (every sample_step_m) is judged across the
///     corridor's width: off the map, a unit's circle, water, the river away
///     from a ford, forest, reserve, ruins, and — by surface — trees (a path
///     and a dirt road) or the floodplain (gravel and asphalt), and for
///     asphalt with walks the village's contour. Runs of one refusal are the
///     blocks, along `s`.
///
/// WHAT IT DOES NOT SEE YET, named so it is not taken for a pass:
///   * a road laid ALONG an existing one — two ends snapped to one road at
///     two places lay a second road on the first, unrefused (7c decides);
///   * an obstacle thinner than the search cell between two of its centres:
///     the way round may hop it, and the hop is judged again sample by
///     sample and shows as its own red span, not as kNoWayRound;
///   * the clearing's labour: trees add hectares and timber to the estimate
///     but no man-days, while the design says the work grows with the trees
///     (roads design §9, «Трудоёмкость растёт») — STUB, the number is
///     asked of boss.
///
/// EVERY NUMBER HERE IS A STUB, named in RoadTraceConfig with its source;
/// the design gives the bed (8 m), the clearing (a bed's width) and the calm
/// 20 m, and nothing else.

#ifndef CORE_COMMON_ROAD_TRACE_H_
#define CORE_COMMON_ROAD_TRACE_H_

#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include "core_common/geometry.h"
#include "core_common/obstacle_raster.h"
#include "core_common/quantities.h"
#include "core_common/road_draft.h"
#include "core_common/road_state.h"

namespace core {

/// @brief A surface's wander in plan (terrain design table; `look`'s
///        candidates for the first prototype, NOT accepted — so named).
struct RoadWander {
  float amplitude_m = 0.0F;   ///< The most the axis strays from the chord.
  float wavelength_m = 0.0F;  ///< The main wave's length.
};

inline constexpr std::size_t kRoadSurfaceSlots =
    static_cast<std::size_t>(RoadSurface::kRoadSurfaceCount);

/// @brief The tracer's numbers. Defaults are the delivery's; each says where
///        it came from.
struct RoadTraceConfig {
  /// Metres between the axis samples that are judged. STUB.
  float sample_step_m = 2.0F;

  /// Half the road's bed: roads design §7, «Полотно целиком — 8».
  float road_half_width_m = 4.0F;

  /// Half a path's tread. STUB: the design gives no width for a path.
  float path_half_width_m = 0.5F;

  /// The clearing strip of a gravel or asphalt road through trees: a bed's
  /// width (roads design §2, «Просека шириной в полотно»).
  float clearing_width_m = 8.0F;

  /// How far past the river's half-width at a ford a road may still cross.
  /// STUB (boss [64] item 3).
  float ford_extra_m = 10.0F;

  /// The band a way round is searched in, as a share of the stretch between
  /// two points, clamped (boss [64] item 4). STUB.
  float band_share = 0.3F;
  float band_min_m = 50.0F;
  float band_max_m = 300.0F;

  /// The search grid's cell. STUB: coarser than the raster, the pulled and
  /// smoothed path is judged again sample by sample.
  float search_cell_m = 10.0F;

  /// An end this near a road's axis snaps onto it. STUB.
  float snap_to_road_m = 12.0F;

  /// A snapped end this near a junction or a road's end takes that point.
  /// STUB.
  float snap_to_junction_m = 20.0F;

  /// No wave within this of either end (terrain design: «В 20 м от
  /// перекрёстка и ворот новой волны нет»).
  float wander_calm_m = 20.0F;

  /// Metres between the points of a drawn curve.
  float curve_step_m = 5.0F;

  /// The share of a unit's PLOT radius a road may not enter: the building,
  /// not the yard. STUB, measured (7b): the plot circle whole refused the
  /// start's own streets — the church store's is 35 m and the street runs
  /// 15 m from its centre — and the core does not know yards at all
  /// (RoadTracerGaps::yard_plots_unchecked). At 0.3 a house's 25 m plot
  /// keeps 7.5 m clear, the church's 10.5 m. A unit with a body and no plot
  /// keeps its body whole.
  float plot_core_share = 0.3F;

  /// By RoadSurface: path, dirt, gravel, asphalt, asphalt with walks. The
  /// path's 0.4 m and 15-25 m bend is terrain design's; the rest its table.
  std::array<RoadWander, kRoadSurfaceSlots> wander = {{
      {.amplitude_m = 0.4F, .wavelength_m = 20.0F},
      {.amplitude_m = 5.0F, .wavelength_m = 240.0F},
      {.amplitude_m = 3.0F, .wavelength_m = 270.0F},
      {.amplitude_m = 1.5F, .wavelength_m = 300.0F},
      {.amplitude_m = 1.5F, .wavelength_m = 300.0F},
  }};
};

/// @brief What laying 100 m of a surface costs, and whether the epoch has
///        opened it. Path and dirt are free and always open (§9).
struct RoadSurfaceCost {
  bool open = false;

  /// Трудодни per 100 m of bed: unit_levels.csv `road` rows, read «на 100 м
  /// полотна 8 м» — STUB of the length basis (boss [64]).
  float man_days_per_100m = 0.0F;

  /// Materials per 100 m, dense by ResourceId (unit_level_cost.csv).
  ResourceAmounts materials_per_100m;
};

/// @brief A place on the river where a road may cross: the ford's point and
///        how far from it a crossing still counts (the half-width there plus
///        ford_extra_m).
struct RoadFord {
  Vec2 position;
  float reach_m = 0.0F;
};

/// @brief A unit as a road sees it: a circle it may not enter — a share of
///        the plot's where the type has a plot, the body's where it has
///        none (boss [64] item 1, loosened by measure:
///        RoadTraceConfig::plot_core_share).
struct RoadUnitDisc {
  Vec2 centre;
  float radius_m = 0.0F;
};

/// @brief The world as the tracer reads it. Everything by pointer or span:
///        the caller owns it for the call.
struct RoadTraceSite {
  /// Null, or a raster with no map, when the table set has no map_areas and
  /// map_lines: the trace then sees no ground and says so
  /// (RoadTracerGaps::obstacles_unread).
  const ObstacleRaster* raster = nullptr;

  std::span<const RoadFord> fords;

  /// The village's contours (map_areas.csv `village_zone`).
  std::span<const std::vector<Vec2>> village;

  std::span<const RoadUnitDisc> units;

  /// The laid network, for snapping the ends. May be null.
  const RoadTable* roads = nullptr;

  /// Side of the square map, metres; 0 — no edge is known and none refuses.
  float map_side_m = 0.0F;

  /// By RoadSurface.
  std::array<RoadSurfaceCost, kRoadSurfaceSlots> costs{};

  /// Cubic metres of timber a hectare of cleared grove or orchard gives
  /// (world_params `timber_grove_stock_m3_per_ha`; the orchard's own is a
  /// STUB, boss [64] item 2).
  float timber_m3_per_ha = 0.0F;
};

/// @brief Traces `draft` on `site` (see @file).
/// @return Never empty-handed: a draft it cannot trace at all (bad points, a
///         surface the kind has not) comes back with one block at its first
///         point and no axis.
RoadDraftResult TraceRoad(const RoadTraceSite& site,
                          const RoadDraft& draft,
                          const RoadTraceConfig& config = {});

}  // namespace core

#endif  // CORE_COMMON_ROAD_TRACE_H_
