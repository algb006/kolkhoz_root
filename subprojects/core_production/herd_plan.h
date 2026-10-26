/// @file
/// @brief The herd plan's window line, the body (#1289; livestock.md «План
///        стада — "держать не больше N голов"», registry 319; econ's
///        herd-plan-against-the-meadows-2026-10-13): «сена на · мест ·
///        держим», «по норме» and the three warnings, per kolkhoz herd.
/// @threading SINGLE_THREADED
/// Between steps on the sim thread (the door) and the run bot's evening;
/// a pure read of the world.
///
/// «СЕНА НА» IS THE HAY LAMP'S FORECAST TURNED INSIDE OUT, so the window and
/// the lamp cannot part: the most heads of the herd that ForecastHerdFeed,
/// to the lamp's own horizon (FeedHorizon::kNearestScythes), feeds with no
/// day short - every OTHER kolkhoz herd held at its own plan («одно число на
/// стадо, каждое — при плане другого»), this herd's births stopping at the
/// heads asked (the plan gate, which the forecast now reads too). Fewer heads
/// never eat more, so the answer is found by halving.
#ifndef CORE_PRODUCTION_HERD_PLAN_H_
#define CORE_PRODUCTION_HERD_PLAN_H_

#include <cstdint>

#include "core_common/herd_plan_view.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief The window's line for the kolkhoz herd in row `herd_row`: heads,
///        places (HerdPlaces), the plan, «сена на» (the halving above) and
///        «по норме» (year 1 before its cut closes: the village has no cut of
///        its own yet, and the forecast reads the mowing's norm), and the
///        warnings - the plan under the milk plan's cows (HerdFloors::
///        cows_kept, the milk kind), under the spring's teams (horses_kept,
///        the horses), under two (no birth at all).
/// @pre `herd_row` names a kolkhoz herd (no household).
HerdPlanLine HerdPlanLineOf(const ProductionConfig& config,
                            const WorldState& world,
                            std::uint32_t herd_row);

/// @brief Scales a herd's cohorts to `heads` whole heads in their present
///        proportions (adults, juveniles, the rest newborns; the sires with
///        the adults, at least one kept while there were any). An empty herd
///        grows adults. For the forecast's what-if alone: no real herd moves.
void ScaleHerdHeads(HerdRow& herd, std::uint16_t heads);

}  // namespace core

#endif  // CORE_PRODUCTION_HERD_PLAN_H_
