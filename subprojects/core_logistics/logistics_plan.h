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
/// THE TRIPS (stage В, F3 (A)): a load's carting is drawn as its trips —
/// loaded from the load to its receiver (WorkReceiverOf), empty back — each
/// as long as the round trip the load's seam is priced at (haul.h, HaulRate),
/// as many as the seam's carting by the clock holds; the last trip's way back
/// is not drawn when the cart goes on from the receiver (to its next load, or
/// home). A trip the light cannot finish is not begun: the cart goes home
/// from where it is. A load with no receiver written is drawn as before, one
/// leg standing at the load, and counted (a shout in the tally's legs).
///
/// APPROXIMATIONS, NAMED: the clock drains a seam at the norm, one cart-hour
/// an hour (a walker his share of it, haul.h) — not at each man's efficiency
/// as the labour hour does; the trips are cut whole from the seam's span by
/// the priced round trip, while the labour hour carts half a trip at the day's
/// end as half a load (haul.h) — the drawing rounds, the carting does not; a
/// carrier on foot's day stays one leg a load (he has no cart to draw); a
/// cart on its way when the light ends goes home from the place it was going
/// to.
/// @return The plan; `tally` counts the carts, the legs and the tasks by level.
///
/// THE RE-PLAN (B5): with `earlier` — today's plan as it stands — the carts'
/// legs begun by now are kept as they were (a begun leg is never broken) and
/// the clock plans the rest of the day from where each cart then is, not
/// before the next hour; the people's carts as the morning planned them.
GroomPlan BuildGroomPlan(const LogisticsConfig& config,
                         const WorldState& world,
                         LogisticsTally& tally,
                         const GroomPlan* earlier = nullptr);

/// @brief The cargo form of a leg's load (cargo_form.h): the form of the
///        resource its task carries — a field's heap its reaped resource, a
///        stand `log`, a pit its dug resource, a works' supply, a district lot
///        or a store being emptied the resource of MOST grams (one form a leg
///        until the mixing, F4). kNone for a ride and for a trip's WAY BACK
///        (task set, not loaded, from ≠ to); a load leg standing at its load
///        (from = to: no receiver written, or a carrier on foot) keeps its
///        load's form, as F2 drew it.
CargoForm LegCargoForm(const LogisticsConfig& config, const WorldState& world, const CartLeg& leg);

/// @brief THE EVENING RETURN (stage В, F3 (B); transport design §11,
///        «Обратный путь», 357c60d0; «Подвода бригады — как была»,
///        ddbf6f48): seats the day's riders for the way home on `plan`'s
///        carts and writes every such worker's WorkAssignment::
///        travel_home_hours and rides_home_with.
///
/// WHO WANTS A RIDE HOME: every worker who rode a GOODS cart this morning
/// (rides_cart_of names a goods cart's driver) — a feller seated on the empty
/// log cart going out to the stand among them: by ddbf6f48 a felling or log
/// brigade rides by the form table, so the loaded log trip home refuses him.
/// Not asked: a crew on the people's cart (its evening leg brings it home, as
/// the morning planned), the field brigade of the harvest on its own cart
/// (its cart counts no seats, §11, «Подвода бригады — как была»), a walker,
/// a horseman.
///
/// WHICH LEG TAKES HIM: of the carts whose last trips of the day or ride home
/// PASS within a walk of his work — the nearest point of the leg's way, as the
/// morning's pick-up (§11, «на тех же условиях»; boss, 11 October 2026) — the
/// one that costs his day least: he leaves work to be at that point when the
/// cart passes, so a leg passing at noon costs him the afternoon and loses to
/// his walk (the cart does not turn aside and does not wait for the day's
/// end: «Подвода с маршрута не сворачивает. Попутчик подстраивается под
/// неё»); he gets off at the point that brings him home soonest; on a LOADED
/// trip only if its load's form seats riders
/// (CargoFormSeatsRiders: not logs, manure or the live), on an empty way
/// always — within the cart's seats, the farthest walkers first (§11, «Кому
/// место, если желающих больше»), the morning's wait term on the evening
/// pick-up too.
///
/// HIS WAY HOME, AND THE DAY IT COSTS (boss, 11 October 2026: «a rider the
/// evening trip cannot take walks home, and his day loses the
/// walk-minus-ride»): seated — sunset less the leg's departure plus his walk
/// to it, and never less than that walk plus the ride plus the walk from the
/// leg's end; not seated — his whole walk
/// home (WorkRoadHours on foot). A worker who refuses nothing and is refused
/// nothing keeps no new number: travel_home_hours stays negative and the
/// labour hour reads travel_hours, as before.
///
/// Counts `tally`'s evening fields. Called after BuildGroomPlan, and after
/// every re-plan (the evening is the day's rest, re-seated from the new legs).
/// @pre `plan` is current.groom_plan's, built for today.
void SeatEveningRiders(const LogisticsConfig& config,
                       WorldState& current,
                       GroomPlan& plan,
                       LogisticsTally& tally);

}  // namespace core

#endif  // CORE_LOGISTICS_LOGISTICS_PLAN_H_
