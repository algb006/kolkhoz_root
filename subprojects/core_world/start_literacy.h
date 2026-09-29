/// @file
/// @brief Who can read on the first day: the founders' primary schooling, by
///        counts per sex and age band with guarantees, not by lot (education
///        design §2; register 301, the timekeeper; econ's review
///        uchetchik-epoch1-2026-09-29 §7а).
/// @threading SINGLE_THREADED
/// Genesis, once the yards and the marriages are made.
///
/// WHAT IT REPLACED. Every founder of sixteen and over was literate on one
/// draw of 0.3: on the poorest seed nine could read, two of them men of
/// thirty and over, and a yard in three had nobody — while the design's
/// village has some fourteen of fifty adults at primary school (the parish
/// school before 1917 for the older, the 1920s school and the army for the
/// younger). The counts and the guarantees are world_params rows (STUB until
/// checked against the 1926 census).
///
/// FROM THE COUNTER HASH, for the hygiene's reason (genesis.cpp): the lot's
/// draws are still taken from the world's generator and thrown away, so the
/// rest of the village is drawn exactly as before and a before/after pair
/// measures the schooling alone.
#ifndef CORE_WORLD_START_LITERACY_H_
#define CORE_WORLD_START_LITERACY_H_

#include <cstdint>
#include <span>
#include <string_view>

#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace core {

/// @brief The counts and the guarantees (world_params `start_literate_*`).
struct StartLiteracy {
  std::uint32_t men_30plus = 6;
  std::uint32_t women_30plus = 2;
  std::uint32_t men_16_29 = 4;
  std::uint32_t women_16_29 = 2;
  /// Guarantees: never fewer literate founders than this in all...
  std::uint32_t min_total = 10;
  /// ...nor fewer men of thirty and over...
  std::uint32_t min_men_30plus = 4;
  /// ...nor fewer yards with one who reads...
  std::uint32_t min_yards = 9;
  /// ...and never more than this many in one yard.
  std::uint32_t max_per_yard = 2;
};

/// @brief The world_params keys this file reads, for the assembly's union
/// (core_catalog/table_value.h).
std::span<const std::string_view> StartLiteracyWorldParamKeys();

/// @brief Reads the counts. A missing row keeps its documented number above
/// silently (the knob reader's way); one out-of-range row sends all eight back
/// to the documented numbers, logged.
StartLiteracy ReadStartLiteracy(const ITableSet& tables);

/// @brief Gives primary schooling to the founders by the counts: each band in
/// turn (men 30+, men 16-29, women 30+, women 16-29) takes its count from its
/// people, the yard with the fewest who read first, never past max_per_yard,
/// ties by the counter hash. What the bands could not fill is taken from any
/// founder of sixteen and over, the same way, up to min_total; then from any
/// founder of sixteen in a yard with nobody who reads, up to min_yards. The
/// men's guarantee is filled from men of thirty before the total's. A
/// guarantee the village cannot meet (too few men of thirty, too few yards
/// with a founder of sixteen) is logged, not forced past the ceiling.
/// @pre The founders' literacy is nought (the lot is thrown away).
void SeedStartLiteracy(const StartLiteracy& start,
                       float life_speedup,
                       std::uint64_t world_seed,
                       WorldState& world);

}  // namespace core

#endif  // CORE_WORLD_START_LITERACY_H_
