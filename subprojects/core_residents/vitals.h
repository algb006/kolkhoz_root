/// @file
/// @brief Settlement vital statistics: the satiety window behind life
/// expectancy (design decision 105).
/// @threading SINGLE_THREADED
/// Runs in the residents sub-step of the decisions phase (slot 3), once per
/// day boundary, from the sim thread. WorldState::vitals has exactly one
/// writer, and this is it.
///
/// Model: manual/66-food-model.md §7. Life expectancy is
/// 60 + medicine + nutrition + living + working conditions, its factors
/// averaged over three years and recomputed once a year. Phase 1 keeps
/// medicine and living at zero and working conditions constant, so only
/// nutrition moves — and nutrition is the settlement's mean satiety, which
/// has to be accumulated day by day before any year can be averaged.
///
/// Stage 6 task O1 lays the bookkeeping; the recompute that turns the window
/// into VitalsState::life_expectancy_years is task O4.

#ifndef CORE_RESIDENTS_VITALS_H_
#define CORE_RESIDENTS_VITALS_H_

#include "core_common/world_state.h"

namespace core {

/// @brief Folds one day into the vitals window: the finished year is
/// averaged into VitalsState::satiety_year_means on the year's first day,
/// then today's settlement mean satiety joins the running sum.
/// @pre Called once per day boundary, from the sequential decisions slot.
/// @note A settlement with nobody in it contributes no day at all, rather
///       than a zero: an empty village is not a starving one.
void AccumulateVitals(WorldState& current);

}  // namespace core

#endif  // CORE_RESIDENTS_VITALS_H_
