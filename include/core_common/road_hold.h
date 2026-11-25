/// @file
/// @brief THE ROAD'S LOST TIME (#2129; roads.md §8 «Организация движения», transport.md
///        «Наложение агентов недопустимо»; the design in claude/mailbox/core-boss-2129-traffic-
///        design-2026-11-25, approved by boss [2]-[3]): where a vehicle of the groom plan stood
///        or crawled on the carriageway, and why. CONTRACT ONLY (core §12а): the types and the
///        door; RoadObstaclesToday's body and the ledger (core_logistics road_traffic.h) are the
///        next task.
///
/// THE BOUNDARY (boss [2]). The CORE owns the time with a consequence: a vehicle following a
/// slower one on dirt or gravel (no overtaking there, roads.md «Обгон только на асфальте»), and
/// one waiting behind a standing obstacle on any surface, in the order they wait. The LAYER owns
/// what is seen: no agent through another, lanes, the shoulder, the horn, the chickens, yard
/// manoeuvres; it may lay a standing obstacle on a carriageway ONLY where RoadObstacles names
/// one. Pedestrians are outside the core's traffic: the design gives them no time.
///
/// THE FIRST SLICE: the plan's vehicles (goods carts, log carts, the people's carts) and the
/// FOLLOWING rule. The pig in a puddle is kPigInPuddle, a STUB behind world_params
/// `road_pig_obstacles` (0, off): it returns with moving wear and road repair (boss [2] q1).
/// Named gaps the print counts: teams, horsemen, brigade carts and mowers are not in the plan;
/// a dock queue on the road is the layout's (transport.md); a people's cart's hold is drawn,
/// not charged to its riders.
/// THE PAY: OPEN. econ's [3] («not paid, shortens the day») is questioned by Alex (#2154,
/// pay idle time not the worker's fault); until his word the hold is TIME ONLY - it lengthens
/// the plan's leg and touches neither the carter's day nor a task's norm-days (boss 25 Nov).
/// @threading PARALLEL_READONLY
/// Plain data, built in phase 3 (decisions) and read between steps.

#ifndef CORE_COMMON_ROAD_HOLD_H_
#define CORE_COMMON_ROAD_HOLD_H_

#include <cstdint>
#include <vector>

#include "core_common/ids.h"

namespace core {

struct WorldState;

/// @brief Why a vehicle lost time on the road. Values are appended before the count.
enum class RoadHoldCause : std::uint8_t {
  kFollowing = 0,  ///< Behind a slower mover on dirt or gravel, where none overtakes.
  kPigInPuddle,    ///< STUB (off, road_pig_obstacles 0): a pig lying in a road puddle.
  kHerdPassing,    ///< STUB: no producer (the core has no herd drive).
  kRoadHoldCauseCount,
};

inline constexpr std::size_t kRoadHoldCauseCountValue =
    static_cast<std::size_t>(RoadHoldCause::kRoadHoldCauseCount);

/// @brief A standing obstacle on a carriageway today: derived each morning, never saved.
struct RoadObstacle {
  RoadHoldCause cause = RoadHoldCause::kPigInPuddle;
  RoadId road;
  /// Along the road's axis, metres.
  float chainage_m = 0.0F;
  /// The game minutes of the day it stands, [from, to).
  std::uint16_t from_minute = 0;
  std::uint16_t to_minute = 0;
  /// 1 when it blocks both directions.
  std::uint8_t both_directions = 1;
  /// The herd it came from (a yard's pigs), when it has one.
  HerdId herd;
};

/// @brief One vehicle's lost time on the road today: saved with the plan. THE DAY'S RECORD:
///        a re-plan keeps every hold of the legs it kept, and of a dropped cart those begun
///        before it (road_traffic.h CarryHolds), so the evening's plan holds the whole day's.
struct RoadHold {
  /// The plan's cart and the leg of its day (CargoFormOfLeg's address); both kHoldOffThePlan
  /// once a re-plan dropped the cart (its driver's work ended) - the hold happened all the same.
  std::uint32_t cart = 0;
  std::uint32_t leg = 0;
  /// The cart's driver: the hold's lasting address, a re-plan renumbering the carts.
  ResidentId driver;
  RoadHoldCause cause = RoadHoldCause::kFollowing;
  RoadId road;
  float chainage_m = 0.0F;
  /// The game minutes it stood or crawled, [from, to).
  std::uint16_t from_minute = 0;
  std::uint16_t to_minute = 0;
  /// The cart directly ahead (kNoCartAhead for an obstacle with none), and its place in the
  /// queue: 1 directly behind the cause.
  std::uint32_t behind_cart = 0;
  std::uint16_t rank = 0;
};

inline constexpr std::uint32_t kNoCartAhead = 0xFFFFFFFFU;
/// A cart (or the cart ahead) a re-plan dropped from the day's plan.
inline constexpr std::uint32_t kHoldOffThePlan = 0xFFFFFFFEU;

/// @brief Today's standing obstacles: a pure function of the roads' wear, the yard herds and
///        the day's weather. Empty while world_params `road_pig_obstacles` is 0 (the first
///        slice; boss [2] q1). STUB body until the implementation task.
std::vector<RoadObstacle> RoadObstaclesToday(const WorldState& world);

}  // namespace core

#endif  // CORE_COMMON_ROAD_HOLD_H_
