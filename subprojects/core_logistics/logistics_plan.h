/// @file
/// @brief The groom's plan of the day (routing stage B, B3; boss, the
///        logistics thread [11]-[12]): the placement gives the carts — the
///        carters on a horse — and the plan gives each a CHAIN of loads in
///        order: its morning's load first, then the open tasks by level and,
///        within a level, round the ring. The labour hour follows the chain
///        (B4): a cart that has carted its load goes on to the next one the
///        same day instead of standing about.
/// @threading SINGLE_THREADED
/// Built by the logistics sub-step at hour 1, after the top-up.

#ifndef CORE_LOGISTICS_LOGISTICS_PLAN_H_
#define CORE_LOGISTICS_LOGISTICS_PLAN_H_

#include <cstdint>

#include "core_common/logistics_state.h"
#include "core_logistics/logistics_system.h"
#include "logistics_config.h"

namespace core {

struct WorldState;

/// The most loads a cart's chain names: a day's carting rarely empties more
/// than two or three heaps; the rest of the chain would be a promise the
/// light cannot keep. A bound, not a design number.
inline constexpr std::uint32_t kMaxChainLoads = 6;

/// @brief Builds the day's plan over the carts the placement put on a horse.
///
/// THE CHAIN: the cart's own morning load first — the placement's choice
/// stands, and the passengers seated on its first leg (0.37.164) with it;
/// then every open task (not paused, its seam not empty) the cart can reach —
/// the ride from its first load within the road limit (labor.csv
/// travel_limit_hours at the harness pace, the way by the roads) — ordered by
/// level, then the longest-waiting (aged_from_day), then the row; within a
/// level each cart starts the ring one task further on than the cart before
/// it (§12, «Кольцевая очередь»), so the carts of a level spread over its
/// tasks instead of queueing on one.
///
/// THE CARRIER ON FOOT (B4b; boss [11], default 1): his morning load, then
/// the nearest open loads of its level within a walk — never a log, never a
/// far one. THE PEOPLE'S CART (B4b): out to its work with its riders and back
/// in the evening, two legs; it carries no load. THE RIDERS: whoever the
/// labour hour seated on a driver's cart this morning, on its first leg.
///
/// THE LEGS' TICKS (B4b; default 2): estimated by a clock of five-minute
/// steps over today's light — the way at the pace (harness, or a walker's),
/// a load's seam shared by every mover on it, the evening when the way home
/// no longer fits the light (logistics_state.h, CartLeg).
///
/// NOT IN THE PLAN (boss [11], default 4): the brigade's cart of the reaping
/// and the sowing and the meadow's mower — they carry no load of a task.
///
/// APPROXIMATIONS, NAMED: the clock drains a seam at the norm, one cart-hour
/// an hour (a walker his share of it, haul.h) — not at each man's efficiency
/// as the labour hour does; the trips to the store are inside a load's seam,
/// so a load leg stands at the load; a cart on its way when the light ends
/// goes home from the place it was going to.
/// @return The plan; `tally` counts the carts, the legs and the tasks by level.
GroomPlan BuildGroomPlan(const LogisticsConfig& config,
                         const WorldState& world,
                         LogisticsTally& tally);

}  // namespace core

#endif  // CORE_LOGISTICS_LOGISTICS_PLAN_H_
