/// @file
/// @brief The private plot: how many hours the day leaves for it, and what
/// the garden gives back for them.
/// @threading PARALLEL_WRITE
/// Runs in the metrics phase (slot 5), parallel by FAMILY: a worker owns
/// whole family rows and reads the resident rows of their members. Only the
/// owned family row is written — its hours, its season accumulators, its
/// pantry.
///
/// Timed to the tick of hour 22 on purpose. Labor closes the working day at
/// hour 23 and clears the assignments, so hour 22 is the last moment at
/// which the day's orders — and with them hours_away_today — are still
/// readable. That is also why `household_hours` has ONE writer since stage
/// 6: labor's close-out used to write the bare remainder, and this phase now
/// writes the whole number, factors included.
///
/// Model: manual/66-food-model.md §5. Design source: household design §1
/// (the additive table of factors, and the yard's yearly yield at full
/// attention). The deferred factors of that table — the rush job, the
/// boarding school, the bicycle, the second day off, the distant school —
/// have no systems in phase 1 and therefore no factors here: they arrive
/// with their systems, not before.

#ifndef CORE_RESIDENTS_HOUSEHOLD_PLOT_H_
#define CORE_RESIDENTS_HOUSEHOLD_PLOT_H_

#include <cstdint>

#include "core_common/world_state.h"
#include "food_config.h"

namespace core {

/// @brief Computes one family's plot hours for the day, folds them into the
/// growing season's average and, on the last day of the garden's month,
/// pays the yard's harvest into the pantry.
/// @param family_item Row index in current.families; the worker owns it.
/// @param life_speedup Biological years per game year (life.csv).
/// @note Self-gated to the tick of hour 22 (see @file).
void RunHouseholdPlot(const FoodConfig& config,
                      float life_speedup,
                      WorldState& current,
                      std::uint32_t family_item);

}  // namespace core

#endif  // CORE_RESIDENTS_HOUSEHOLD_PLOT_H_
