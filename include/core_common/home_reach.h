/// @file
/// @brief How far a place is from where people sleep: the road, one way,
///        from the nearest lived-in house, by the way a mode travels.
/// @threading PARALLEL_READONLY
/// A pure read of the world (units and the road index); called from the
/// sequential slots and between steps.
///
/// ONE HOME FOR TWO READERS (0.36.29): core_production's unreachable-stand
/// alarms and core_labor's felling offers ask the same question of the same
/// road, so the answer lives here rather than in one of them — the day they
/// asked it by two functions was the day the canon marked stands the core
/// then refused (0.36.12), from the other side.

#ifndef CORE_COMMON_HOME_REACH_H_
#define CORE_COMMON_HOME_REACH_H_

#include <cstdint>

#include "core_common/geometry.h"
#include "core_common/road_route.h"

namespace core {

struct WorldState;

/// @brief Game hours of the road, one way, from the nearest lived-in house
///        to `place` at `speed_kmh`; negative when nobody lives anywhere or
///        the speed is not positive. BY THE WAY `mode` travels (road_route.h;
///        0.36.2): a team, a log cart off the roads, a walker.
float NearestHomeTravelHours(const WorldState& world, Vec2 place, float speed_kmh, TravelMode mode);

/// @brief The knobs of the one question below, as the labour model holds
///        them (labor.csv, transport.csv): the walker's and the rider's pace,
///        the road limit and the least of a day a road must leave.
struct ReachRule {
  float walk_speed_kmh = 0.0F;
  float ride_speed_kmh = 0.0F;
  float travel_limit_hours = 0.0F;
  float min_usable_hours = 0.0F;
};

/// @brief How a hand of a WALKING work could be sent to a place today.
enum class HandReach : std::uint8_t {
  kOnFoot,  ///< The walk is within the limit and leaves a working day.
  kByRide,  ///< Only a ride does — a seat on the people's cart, from the house.
  kNone,    ///< Neither: past the ride's reach, or the day too short for it.
};

/// @brief COULD A HAND BE SENT TO THIS PLACE TODAY (0.37.208) — the one
///        question of a work that walks and takes the people's cart (labor_
///        state.h, TakesThePeoplesCart: felling, planting, building, digging,
///        the herds' care, a unit's own work), asked from the nearest lived-in
///        house: the placement's own test of a hand (assignment.cpp,
///        ConsiderCandidate — the road limit and RoadLeavesAWorkingDay), by
///        his feet and then by a ride.
///
///        ONE HOME FOR FOUR READERS: the offering of a felling, the
///        unreachable-stand lamps, the chairman's door (ISimulation::
///        FellingCanBeManned) and — through the same predicate — the
///        placement. Until 0.37.208 the offering asked «does the LOG CART
///        reach it within the limit», the lamp the log cart's road and the
///        day's light, and the placement a WALK within the limit: a stand
///        seven hours' walk out was offered every morning, dark on the lamp
///        all summer, and refused to every hand by the road (the canon's
///        stuck felling marks: 1 442 of 1 845 small-felling job-days).
/// @return kByRide says a ride WOULD do; whether a horse is free for it is
///         the caller's to ask.
HandReach HandReachToday(const WorldState& world, Vec2 place, const ReachRule& rule);

/// @brief WOULD A FELLING AT `place` BE OFFERED TODAY (0.37.208): the log
///        cart reaches it within the road limit (the logs go out by it;
///        0.36.29) AND a feller can be sent — on foot, or by a ride while
///        `draught_horses` is not zero. The accountant's offering IS this
///        function (labor_system.cpp), and so are the chairman's door
///        (ISimulation::FellingCanBeManned) and the count of the days a mark
///        stands out of reach (timber_felling.h, ReleaseUnreachableMarks).
/// @param draught_horses The adult kolkhoz horses, the pool a ride is given
///        from. Whether one is FREE this morning is the placement's.
/// @return False as well when nobody lives anywhere.
bool FellingCanBeMannedToday(const WorldState& world,
                             Vec2 place,
                             const ReachRule& rule,
                             std::uint32_t draught_horses);

}  // namespace core

#endif  // CORE_COMMON_HOME_REACH_H_
