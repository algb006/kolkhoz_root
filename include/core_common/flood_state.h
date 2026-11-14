/// @file
/// @brief The spring flood's state and its calendar (#1375; livestock.md, the row «Половодье
///        забирает то, что стоит на пойме», the human's «Да» of 1 November 2026; boss,
///        core-boss-1375-flood-contract [2]).
/// @threading SINGLE_THREADED
/// FloodState is written once a year by core_production's flood rule (spring_flood.h,
/// RunSpringFlood). The calendar functions below are reads of a completed state, here in
/// core_common because the heap readers of the carting (core_logistics) and the flood rule
/// (core_production) must ask one home: modules meet only through core_common.

#ifndef CORE_COMMON_FLOOD_STATE_H_
#define CORE_COMMON_FLOOD_STATE_H_

#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/ids.h"

namespace core {

struct WorldState;

/// @brief What the flood left in the world: when it came last, and until when its water
///        stands on the floodplain. Save 211.
struct FloodState {
  /// The calendar year (1-based) of the last flood; 0 before the first. A flood comes at most
  /// once a year, and never in year 1 (the design: «первой весной паводка нет»).
  std::uint16_t last_flood_year = 0;

  /// The first day the floodplain is dry again: the flood day plus
  /// FarmingConfig::flood_water_days. While `calendar.day` is below it the water stands
  /// (the layer paints it). 0 when no flood has come.
  SimDay water_until_day = 0;
};

/// STUB (#1375, boss [2] p. 1): the month (0-based, May) a spring whose cover never clears
/// floods in, on its first day.
inline constexpr std::uint8_t kFloodFallbackMonth = 4;

/// The first month (0-based, March) the cover's clearing can bring the flood.
inline constexpr std::uint8_t kFloodFirstMonth = 2;

/// The month (0-based, December) the lamp kFloodplainFodderAhead lights from, for the next
/// spring's flood (boss, the contract thread, 15 Nov: 1 February left the winter carting four
/// days; 1 December gives it the sleigh season's twelve).
inline constexpr std::uint8_t kFloodLampFirstMonth = 11;

/// @brief No flood ahead: ProjectedFloodDay's and FloodDeadlineOfField's «none».
inline constexpr SimDay kNoFloodDay = 0xFFFFFFFFU;

/// @brief The day the flood is expected, for the lamp's «~n суток» and the heap deadline:
///        1 March of this year before it, and today from 1 March until the flood - the
///        earliest day the rule allows, so early by construction, never late (the design:
///        «ошибается только в сторону раньше»; boss took this way (b), 15 Nov - no climate
///        door). From 1 December (kFloodLampFirstMonth) it is the NEXT year's 1 March.
///        kNoFloodDay from January to November of year 1, after this year's flood, and from
///        1 May to the end of November.
SimDay ProjectedFloodDay(const WorldState& world);

/// @brief Whether a field lies on floodplain ground for the flood: kind kFloodplainMeadow.
bool FieldFloods(const WorldState& world, FieldId field);

/// @brief The carting deadline the flood puts on a field's heap (#1200's interlock): the
///        projected flood day for a field that floods (FieldFloods), kNoFloodDay for any
///        other or when no flood is ahead. Stacks on floodplain cells are not heaps: the
///        heaps' readers never cart a unit's store, and a stack's way out is where it stands.
SimDay FloodDeadlineOfField(const WorldState& world, FieldId field);

}  // namespace core

#endif  // CORE_COMMON_FLOOD_STATE_H_
