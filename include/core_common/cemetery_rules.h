/// @file
/// @brief The cemetery's fill: how many graves a cemetery holds, how full it
/// is, and the stop the game makes when somebody dies and none has room.
/// @threading PARALLEL_READONLY
/// Pure reads of a world or of numbers; called from the sequential decisions
/// slot (the demography sub-step lays the graves) and by consumers between
/// steps (the layer's fill frames and its clock). They write nothing.
///
/// Design: social.md «Два кладбища» and «Заполнение — метрика юнита,
/// 0–100 %» (#1199; boss, boss-core-epoch1-resume [16]):
///   * CAPACITY IS THE PLOT'S AREA: unit_types cemetery capacity_by_plot 1,
///     graves = the plot's area / world_params cemetery_m2_per_grave (15 m²,
///     STUB, 6..30). The core's plot is the type's circle (plot_radius_m),
///     not an outline the player draws, so every cemetery holds the same
///     number: π · 25² / 15 = 130 graves.
///   * THE OLD ONE STARTS HALF FULL: start_layout.csv fill_pct (50) of its
///     capacity. Until #1199 that column was read by nobody.
///   * SINCE #1367 THE CAPACITY IS NOT A LIMIT (the human's word of 1 November
///     2026, #1564): the area sets only how dense the rows look (CemeteryFill,
///     0..1, full at the area's graves). A DEATH IN THE VILLAGE LAYS ONE GRAVE
///     in the nearest cemetery BY ROAD of the dead's own settlement
///     (social.md «Где хоронят»); until #1367 the oldest with room. A death
///     outside the village (the district's ambulance) lays none.
///   * NO STOP: BurialHeld() answers no. Only a world with no cemetery built at
///     all counts its waiting graves (burials_waiting); the first that stands
///     takes them, and kBurialsLaid fires. No grave is lost.

#ifndef CORE_COMMON_CEMETERY_RULES_H_
#define CORE_COMMON_CEMETERY_RULES_H_

#include <cstdint>
#include <string_view>

#include "core_common/unit_state.h"
#include "core_common/world_state.h"

namespace core {

/// world_params cemetery_m2_per_grave: the source default and its range
/// (boss, boss-core-epoch1-resume [16]; STUB, the design's own word).
inline constexpr float kCemeteryM2PerGraveDefault = 15.0F;
inline constexpr float kCemeteryM2PerGraveMin = 6.0F;
inline constexpr float kCemeteryM2PerGraveMax = 30.0F;

/// The unit_types key of the cemetery: the one type that holds graves.
inline constexpr std::string_view kCemeteryTypeKey = "cemetery";

/// @brief The graves a cemetery's plot holds: floor(π · radius² /
///        m2_per_grave).
/// @param plot_radius_m The type's plot radius (unit_types plot_radius_m), > 0.
/// @param m2_per_grave world_params cemetery_m2_per_grave, > 0.
/// @return 0 for a non-positive argument.
[[nodiscard]] std::uint16_t CemeteryCapacityGraves(float plot_radius_m, float m2_per_grave);

/// @brief How full a cemetery is, 0..1: graves / capacity. The layer's fill
///        frames (fill_00…fill_100) blend on it; the unit window prints it.
/// @return 1 for a capacity of 0 (a cemetery that can take nobody is full).
[[nodiscard]] float CemeteryFill(std::uint16_t graves, std::uint16_t capacity);

/// @brief THE GAME'S STOP, RETIRED (#1367; social.md «Остановки игры нет»):
///        always false. Kept as the layer's clock still asks it.
[[nodiscard]] bool BurialHeld(const WorldState& world);

}  // namespace core

#endif  // CORE_COMMON_CEMETERY_RULES_H_
