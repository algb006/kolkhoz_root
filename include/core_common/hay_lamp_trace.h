/// @file hay_lamp_trace.h
/// @brief The hay lamp's day, laid open - a DIAGNOSTIC view (#1586; econ,
/// econ-boss-1585-novice-dieoff
///        [1] §4: «the forecast's terms, the rung reached and why the rungs above it were skipped,
///        the heads above each floor»). The lamp itself is kHerdHayShortAhead (alarm_state.h); this
///        says why it said what it said. Nothing reads it to decide: runs print it.
/// @threading SINGLE_THREADED
/// Plain data, built on the sim thread between steps and copied out.

#ifndef CORE_COMMON_HAY_LAMP_TRACE_H_
#define CORE_COMMON_HAY_LAMP_TRACE_H_

#include <cstdint>
#include <vector>

#include "core_common/alarm_state.h"

namespace core {

/// @brief Why a rung of the hay ladder was passed or where it stopped (herd_forecast.h,
///        AdviseOnShortFodder's order).
enum class HayRungReason : std::uint8_t {
  kNotShort = 0,     ///< the forecast is not short: no ladder at all
  kAvralLifted,      ///< the avral stood its days: «lift it» named
  kDayOffWorked,     ///< a dry day off in the cut: «declare it working» named
  kAvralNamed,       ///< the cut hurried: «avral» named
  kGrassInOpenCut,   ///< a cut is open and grass stands: only the cut's moves (or none)
  kCheapOff,         ///< the cheap rung is off (hay_feed_before_heads_points 0)
  kCheapNoLot,       ///< no feed lot buyable today within the price (`value`: its points, 0 none)
  kCheapNotClosing,  ///< the dearest lot within the price leaves the herds short
  kCheapTaken,       ///< the cheap purchase named (`value`: its points)
  kTolerated,        ///< within the tolerance: no move (`value`: short days)
  kHeadsNamed,       ///< heads above the floors named (`value`: heads)
  kFeedLeast,        ///< the least lots that close it, beside the heads above (`value`: points)
  kGranary,          ///< a lot refused for want of a store alone: «finish the granary»
  kBelowFloorOnly,   ///< no purchase closes it: the remainder below the floors named, no move
  kReasonCount,
};

/// @brief One rung the ladder asked, in order.
struct HayRungStep {
  HayRungReason reason = HayRungReason::kNotShort;
  std::int64_t value = 0;
};

/// @brief The lamp's day: the forecast's terms, the floors, the rungs.
struct HayLampTrace {
  /// False when the world has no hay, no feed roster or no kolkhoz herd.
  bool traced = false;

  // THE FORECAST'S TERMS (to the lamp's horizon, kNearestScythes).
  bool short_ahead = false;
  std::uint16_t days_ahead = 0;
  std::uint16_t horizon_days = 0;
  std::uint16_t short_days = 0;
  bool death_ahead = false;
  /// The hay the herds may eat today from the stores (HerdFeedAllowance), kg.
  float store_hay_kg = 0.0F;
  /// THE FIELD HEAPS COUNTED AS HELD, kg: the meadows' heaps above the plan's debt
  /// (HeapAbovePlanDebt), held from today. NO SPOILAGE AND NO HAUL ARE COUNTED against them -
  /// econ's hypothesis of #1585 is about exactly this term.
  float heap_hay_counted_kg = 0.0F;
  /// The cut still to come before the horizon, kg, as the forecast lands it.
  float cut_to_come_kg = 0.0F;
  float need_units = 0.0F;
  float covered_units = 0.0F;
  float short_hay_kg = 0.0F;
  /// #1606: need_units by LivestockKindId, the projected head-days (adults +
  /// juveniles) over the horizon, and today's real kolkhoz heads (adults +
  /// juveniles) by kind - the trace compares the projection with the days
  /// as they come.
  std::vector<float> need_units_by_kind;
  std::vector<float> head_days_by_kind;
  std::vector<std::int64_t> heads_today_by_kind;
  /// #1606's gate A1: the forecast's expected hay held (kg) at each day's end,
  /// by offset from today; -1 a day not drained.
  std::vector<float> hay_held_kg_by_day;

  // THE FLOORS (herd_floors.h): the adults standing and those kept.
  std::int64_t horses = 0;
  std::int64_t horses_kept = 0;
  std::int64_t cows = 0;
  /// The milk floor; -1 «every cow kept» (no closed year to read a yield from).
  std::int64_t cows_kept = 0;
  std::int32_t points = 0;

  // THE LADDER, in the order asked, and the advice it gave.
  std::vector<HayRungStep> rungs;
  AlarmAdvice advice = AlarmAdvice::kNone;
  std::int64_t hand_over_stock = 0;
  std::int64_t hand_over_horses = 0;
  std::int64_t below_floor_stock = 0;
  std::int64_t below_floor_horses = 0;
};

}  // namespace core

#endif  // CORE_COMMON_HAY_LAMP_TRACE_H_
