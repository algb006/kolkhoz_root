/// @file
/// @brief The floors of the hay lamp's advice «hand over N heads», and the
///        least heads that feed the rest named BY them (0.37.142; Livestock
///        design §6 as boss rewrote it on 2 October 2026; boss-all-carts-
///        carry-people-go-2026-10-02 [114], [119], [128], [131]).
/// @threading SINGLE_THREADED
/// Read-only over a completed state, between steps (CollectAlarms). Nothing
/// here writes the world.
///
/// WHY FLOORS. 0.37.137-138 named the least heads «the stock before the
/// draught», and on host's 27 villages the team bred 22 -> 60 in four years
/// while the advice closed every year's hay with cows: three cows left by
/// the median, the plan failed in twenty villages, and a village that did
/// not read the lamp failed none. The order is now:
///   1. the ADULT HORSES above the ploughing's floor with its margin;
///   2. the ADULT COWS above the milk plan's floor;
/// and when both are exhausted and the fodder is still short, what would
/// have to go BELOW the floors is named apart — the price of the plan, the
/// chairman's to choose.
///
/// ADULTS, AND WHY NOT THE YOUNG. The numbers are an order's: the hand-over's
/// door takes the oldest first — adults before juveniles (OrderHandStock) —
/// so «N heads of the team» IS its N oldest adults, and a number that counted
/// a foal above the floor would take a plough horse below it when executed.
/// The young go only with the remainder below the floors, where the door
/// reaches them: after every adult of their class.
///
/// THE HORSES' FLOOR IS THE PLOUGHING'S AND NOT THE HORSES' OCCUPANCY. Three
/// measures of occupancy were tried on 2 October 2026 and withdrawn by
/// print: the idle share of the tightest month, the most horses a day held
/// under work that cannot wait, the month's sum of such work. All three
/// followed the herd up — a horse more goes under the plough and does not
/// fill its norm, the windowless carting takes any horse that stands. The
/// ploughing's floor is 13 teams, 18 kept, on the plan's 70 ha whatever the
/// herd.
#ifndef CORE_PRODUCTION_HERD_FLOORS_H_
#define CORE_PRODUCTION_HERD_FLOORS_H_

#include <cstdint>

#include "core_common/ids.h"
#include "core_common/world_state.h"
#include "herd_forecast.h"
#include "production_config.h"

namespace core {

/// @brief The kind the milk plan's floor is the floor of: livestock.csv's
///        kind with the most milk a head a year (the cow). Invalid when no
///        kind gives milk.
/// @note The closed year's milk (YearLedger::herd_produce) is every kolkhoz
///       herd's; a kolkhoz herd of another milking kind would raise «a cow's
///       yield» and lower the floor. No run keeps one; named, not handled.
LivestockKindId MilkKind(const ProductionConfig& config);

/// @brief The adults the floors keep.
struct HerdFloors {
  /// The teams that plough the plan's base of worked hectares
  /// (PlanState::worked_ha_last_year; all the arable before a base is
  /// written) in farming.csv `plough_window_days`, rounded up.
  std::int64_t plough_teams = 0;

  /// The adult kolkhoz horses kept: `plough_teams` times farming.csv
  /// `plough_floor_margin`, rounded up.
  std::int64_t horses_kept = 0;

  /// The adult kolkhoz cows kept: the highest milk position the district
  /// has named (PlanState::highest_due, this year's and the closed book's
  /// beside it) over a cow's yield of the closed year (its milk over its
  /// cow-days, YearLedger::adult_head_days), times farming.csv
  /// `milk_floor_margin`, rounded up. EVERY COW (the type's maximum) while
  /// there is a position and no closed year to read a yield from; 0 with no
  /// milk position ever named.
  std::int64_t cows_kept = 0;
};

/// @brief The floors as the world stands today.
HerdFloors HerdFloorsOf(const ProductionConfig& config, const WorldState& world);

/// @brief The numbers of the advice «fewer heads» (Alarm::hand_over_horses,
///        hand_over_stock, below_floor_stock, below_floor_horses).
struct HandOverAdvice {
  /// Adult horses above the ploughing's floor to hand over — step 1.
  std::int64_t horses = 0;

  /// Adult cows above the milk plan's floor to hand over — step 2, named
  /// only when every horse above its floor gone leaves the fodder short.
  std::int64_t stock = 0;

  /// The floors exhausted and the fodder still short: the least heads BELOW
  /// them, not in the two numbers above. The stock first — the heads of the
  /// other kolkhoz kinds (they have no floor and no step of their own:
  /// STUB, the design names two kinds), then the cows; the horses last.
  /// Within a class as the door takes them, adults then juveniles.
  std::int64_t below_floor_stock = 0;
  std::int64_t below_floor_horses = 0;
};

/// @brief THE LEAST HEADS TO HAND OVER FOR THE REST TO BE FED to the lamp's
///        horizon (FeedHorizon::kNearestScythes), in the floors' order. Each
///        step only as far as the shortage asks, found by halving: fewer
///        heads never eat more. WITH NO PURCHASE COUNTED: the heads above the
///        floors go before the district's feed (AdviseOnShortFodder's ladder,
///        0.37.142), and the feed is asked for what is left.
/// @return All nought for a forecast that is not short. With every head
///         standing gone and the forecast still short (a feed no head's
///         leaving brings — it cannot be, with none left) every head is
///         named, the floors' share in the first pair and the rest in the
///         second.
HandOverAdvice LeastHeadsToHandOver(const ProductionConfig& config, const WorldState& world);

}  // namespace core

#endif  // CORE_PRODUCTION_HERD_FLOORS_H_
