/// @file
/// @brief Production's half of marking a meadow (core_common/meadow_mark.h;
/// 0.37.211): the tables' numbers and the stands' contours put beside the
/// map's half, the order kMarkMeadow read, and the preview answered.
/// @threading SINGLE_THREADED
/// The order runs from the production sub-step of the decisions slot
/// (phase 3) on the sim thread; the preview is a pure read between steps.

#ifndef CORE_PRODUCTION_MEADOW_ORDERS_H_
#define CORE_PRODUCTION_MEADOW_ORDERS_H_

#include <vector>

#include "core_common/meadow_mark.h"
#include "core_common/order_state.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief Everything PreviewMeadowMark reads besides the world: the map's
///        half from `config.meadow_ground` (empty — no map: no raster, no
///        polygons, no pits), the plots' radii and the map's side from the
///        config, the mowing's days and the two yields from farming.csv, and
///        each stand's contour radius written into `stand_radii`.
/// @param with_raster false leaves the raster out and unbuilt.
/// @param stand_radii Filled by the stands table's row; must outlive the
///        returned ground, which points into it.
/// @note `mow_days_per_ha` is REAL man-days: the config holds game days and
///       the answer's unit is the design's («8 человеко-дней на гектар»).
MeadowMarkGround MeadowGroundOf(const ProductionConfig& config,
                                const WorldState& world,
                                bool with_raster,
                                std::vector<float>& stand_radii);

/// @brief The core's answer to a mark being drawn (ISimulation::
///        PreviewMeadowMark).
MeadowMarkAnswer PreviewMeadow(const ProductionConfig& config,
                               const WorldState& world,
                               Vec2 position,
                               float area_ha,
                               FieldId field);

/// @brief Reads a kMarkMeadow (MarkMeadow, meadow_mark.h).
/// @return The order's refusal; kNone when the meadow is marked.
/// @note Side effects on success: MarkMeadow's — a row appended or a byte
///       set, and kMeadowMarked.
OrderRefusal OrderMarkMeadow(const ProductionConfig& config,
                             WorldState& current,
                             const OrderRow& order);

}  // namespace core

#endif  // CORE_PRODUCTION_MEADOW_ORDERS_H_
