/// @file
/// @brief What the family eats, what it does to satiety and health, and what
/// the season's table looked like.
/// @threading PARALLEL_WRITE
/// Runs in the needs phase (slot 2), parallel by FAMILY: a worker owns whole
/// family rows and the resident rows of their members, which are disjoint
/// sets (family_state.h, resident_state.h). Nothing outside the owned family
/// is written; the pantry, the members' satiety and health, and the variety
/// mask are all the family's own.
///
/// Reads of `previous` are the buffer law's own channel: the meal happens on
/// the tick of hour 23, and the previous state — hour 22 — still holds live
/// work assignments, which is how a heavy day is seen at all. Labor closes
/// the day in the decisions slot, which runs AFTER this phase, so no row
/// moves between the two buffers at this point in the step.
///
/// Model: manual/66-food-model.md §3 and §4. Design sources: metrics design
/// §8 (the age curve of the norm, satiety, the variety ceiling), health
/// design §2 (chronic malnutrition costs health, recovery is slower than the
/// fall).

#ifndef CORE_RESIDENTS_FAMILY_MEAL_H_
#define CORE_RESIDENTS_FAMILY_MEAL_H_

#include <cstdint>

#include "core_common/quantities.h"
#include "core_common/world_state.h"
#include "food_config.h"

namespace core {

/// @brief Feeds one family for the day out of its own pantry and moves its
/// members' satiety and health.
/// @param family_item Row index in current.families; the worker owns it.
/// @param life_speedup Biological years per game year (life.csv).
/// @note Self-gated to the tick of hour 23: called on every tick, it works
///       on one of them. The season's variety mask is cleared here too, on
///       the season's first day, before that day's meal fills it again.
void RunFamilyMeal(const FoodConfig& config,
                   float life_speedup,
                   const WorldState& previous,
                   WorldState& current,
                   std::uint32_t family_item);

/// @brief The family's satiety component: the members' mean satiety, cut
/// down by the variety ceiling of the epoch (metrics design §8 — one bread
/// all winter caps the component at 50 in Epoch I).
/// @param family_item Row index in current.families.
Metric SatietyComponent(const FoodConfig& config,
                        const WorldState& current,
                        std::uint32_t family_item);

}  // namespace core

#endif  // CORE_RESIDENTS_FAMILY_MEAL_H_
