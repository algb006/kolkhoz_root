/// @file
/// @brief The memory the resident's look is drawn from: a year's satiety, a
/// childhood's satiety and a family's year of satisfaction (live signals
/// design §6, «Облик растёт с благополучием»; register 297; boss-core-epoch1-
/// queue-2026-09-29 [5], [14]; 0.37.17).
/// @threading PARALLEL_WRITE
/// Runs in the metrics phase (slot 5), per family, from FamilyMetricsPhase:
/// each invocation writes its own family row and its own members' resident
/// rows, and reads nothing another invocation writes.
///
/// WHY THE CORE KEEPS IT. The layer's look remembers nothing (live signals
/// §3), and the design's look moves slowly — «месяцами игры, и в обе
/// стороны» — and a child's carries the years of its childhood for life. So
/// the memory is here, and the layer draws from it every frame: an adult's
/// freshness from the worse of their year's satiety and their family's year
/// of satisfaction (boss [14] (1), the stubble's rule with a memory), a
/// grown child's health of build from their childhood.
///
/// kNotYetRemembered (−1) on a row this memory has not seen yet — a world
/// just made, a resident just born, a family just wed: the first pass seeds
/// it with the current value, and until then the layer reads the current
/// value itself (live signals §6, «до полей слой берёт текущую сытость»).
///
/// THE START'S ADULTS: their childhood is seeded with their satiety on the
/// first day and frozen at once — STUB «измождённое село» (boss [14] (2)):
/// the generation the chairman inherits carries its ruined years for life.

#ifndef CORE_RESIDENTS_APPEARANCE_MEMORY_H_
#define CORE_RESIDENTS_APPEARANCE_MEMORY_H_

#include <cstdint>

#include "core_common/world_state.h"
#include "life_config.h"

namespace core {

/// @brief Brings one family's memory and its members' up to today: seeds
/// what is not yet remembered (any step), and on a new day moves the year's
/// memories a year's step towards today's value — satiety for each member,
/// satisfaction for the family — and a child's childhood mean by a day.
/// A member at or past `life.adult_age_years` keeps their childhood as it
/// stands. Call after the family's satisfaction is computed for the step.
/// @param new_day Whether this step crossed a day boundary.
void RememberWellbeing(const LifeConfig& life,
                       WorldState& current,
                       std::uint32_t family_item,
                       bool new_day);

}  // namespace core

#endif  // CORE_RESIDENTS_APPEARANCE_MEMORY_H_
