/// @file
/// @brief The two stock lights core_production owns: feed and seed.
/// @threading SINGLE_THREADED
/// Read-only over a completed state, called between steps on the sim thread
/// through ISimulation::CollectStockForecast. Nothing here writes.
///
/// WHY THESE TWO AND NOT THE OTHER TWO. A light lives with the consumption
/// it forecasts (core_common/stock_forecast.h). The fodder rate per head per
/// day, the pasture season that decides which days are stall days, the
/// sowing norm per hectare and the rotation that says which crop comes next
/// are all here and none of them can move. Food belongs to core_residents
/// with the eating norms; firewood belongs to nobody yet.

#ifndef CORE_PRODUCTION_STOCK_LIGHTS_H_
#define CORE_PRODUCTION_STOCK_LIGHTS_H_

#include <cstdint>

#include "core_common/stock_forecast.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief Game days from `world`'s today to the next start of the harvest
/// window, 0 when the window is open now.
///
/// It is here because the farming calendar is here, and it is PUBLIC because
/// the food light needs it and core_residents may not reach into this module
/// for it (CLAUDE.md §7). The assembly point passes it across — that is what
/// an assembly point is for.
std::int32_t DaysToHarvest(const ProductionConfig& config, const WorldState& world);

/// @brief The feed light: will the fodder reach the pasture.
///
/// COUNTS THE WINTERING, NOT TODAY. In the pasture months the grass covers
/// its share and the daily draw on the stores falls to nearly nothing; a
/// light that counted that would stand green until November and turn yellow
/// when the hay can no longer be cut (office design §5). The light is most
/// useful in haymaking, and that is only true if it looks at the winter.
///
/// DRAINED DAY BY DAY, not divided. Every feed has a ceiling on the share of
/// the day's need it may cover — a ruminant does not live on grain however
/// much of it there is — so total units over daily need OVERSTATES what a
/// lopsided store will carry. An overstating light is the green one that
/// lies, which is the one thing this whole mechanism exists to prevent, so
/// the forecast walks the days and drains a copy of the stores by the real
/// order and the real ceilings.
StockForecast FeedLight(const ProductionConfig& config, const WorldState& world);

/// @brief Game days to the start of the pasture season, 0 when it is open.
std::int32_t DaysToPasture(const ProductionConfig& config, const WorldState& world);

/// @brief Game days to the next sowing window, 0 when one is open.
std::int32_t DaysToSowing(const ProductionConfig& config, const WorldState& world);

/// @brief The seed light: is there enough to sow what the nearest campaign
/// will put in the ground.
///
/// TWO @brief LINES STOOD HERE, and they said different things: the older
/// one asked "will there be anything left to sow", which is the DAYS
/// question this light stopped answering on 2026-09-04. Doxygen keeps the
/// last and drops the first silently, so the contract a reader saw and the
/// contract a tool saw were not the same one. The older sentence is kept
/// below, where it belongs — as the reason the light exists rather than as
/// a second statement of what it returns.
///
/// Food and seed are the same grain and still two lights, because this is
/// the start's most expensive mistake: an eaten seed fund shows nothing at
/// all until sowing, and by then it costs a whole year (office design §5).
///
/// COVERAGE, NOT DAYS, and that is boss's answer of 2026-09-04 rather than a
/// simplification. Seed is not spent day by day — it goes in at once, on the
/// sowing — so "days of seed" is not a hard number but a number that does
/// not exist: infinite until the sowing and zero on the day of it. The light
/// answers the question the stock actually has: what share of the campaign
/// can be sown.
///
/// The date travels beside it and is a CALENDAR number. A shortage of seed
/// shows the moment the harvest is in and mends only slowly; a light that
/// waited for the sowing to draw near would come on when nothing can be
/// done. Its deadline is not when it lights up — it is how long is left to
/// fix it.
StockForecast SeedLight(const ProductionConfig& config, const WorldState& world);

}  // namespace core

#endif  // CORE_PRODUCTION_STOCK_LIGHTS_H_
