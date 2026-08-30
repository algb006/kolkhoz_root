/// @file
/// @brief The herd day: feeding, produce, offspring, aging, slaughter.
/// @threading SINGLE_THREADED
/// Runs in the production sub-step of the decisions phase (slot 3), once per
/// day boundary, from the sim thread. It walks whole tables — herds, units,
/// families — and draws from the world's sequential RNG, so it can only live
/// in a sequential slot.
///
/// Model: manual/66-food-model.md §6. Design sources: livestock design §6
/// (the age ladder, the lifespan band, "above capacity go to the yards, not
/// under the knife"), §11 (the fodder unit and what eats what), the mobs
/// canon (counts by rung, never per-animal ages), and boss's answers of
/// 2026-08-30 on billeting and on the game-unit ages.
///
/// THE UNIT TRAP, restated because this is where it would bite: livestock
/// ages are GAME units with the x4 life acceleration already applied by the
/// design, while feed and care rates are REAL. Nothing here divides an age
/// by the life speedup.

#ifndef CORE_PRODUCTION_HERD_SYSTEM_H_
#define CORE_PRODUCTION_HERD_SYSTEM_H_

#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief Runs one day of every herd, in this order: billeting, feeding,
/// produce, maturation, offspring, deaths, slaughter.
///
/// The order is the day's own logic and not an accident. Billeting first,
/// because how much room there is decides both the leak on today's produce
/// and whether anything is born at all. Feeding before produce, because a
/// hungry day costs milk the same day. Deaths before slaughter, so that a
/// head is never both.
///
/// @pre Called once per day boundary, from the sequential decisions slot.
/// @note A config without a livestock roster (a table-less test world) makes
///       the whole day a no-op rather than an error.
void RunHerdDay(const ProductionConfig& config, WorldState& current);

}  // namespace core

#endif  // CORE_PRODUCTION_HERD_SYSTEM_H_
