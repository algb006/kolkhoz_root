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
/// Stage 6 task O1 laid the bookkeeping; task O4 added the yearly recompute
/// that turns the window into VitalsState::life_expectancy_years.

#ifndef CORE_RESIDENTS_VITALS_H_
#define CORE_RESIDENTS_VITALS_H_

#include "core_common/world_state.h"
#include "life_config.h"

namespace core {

/// @brief Folds one day into the vitals window and, on the year's first day,
/// recomputes life expectancy from it.
///
/// The order inside is the calendar's: the finished year is averaged into
/// VitalsState::satiety_year_means, life expectancy is recomputed over the
/// three-year window that now includes it, and only then does today — the
/// first day of the new year — join the running sum.
///
/// Since stage 7 the same daily pass also feeds the run ledger's satiety
/// extremes (core_common/ledger_state.h): the leanest day the year saw and
/// the most people it ever had hungry at once. They ride here because they
/// need exactly the loop this function already runs — one sweep over every
/// resident — and because the extremes of a seasonal metric are precisely
/// what a yearly mean cannot be asked for afterwards.
///
/// @param hungry_satiety_threshold Satiety below which a resident counts as
///        hungry for the ledger — the food config's health-loss threshold,
///        so "hungry" means the same thing here as it does to health.
/// @pre Called once per day boundary, from the sequential decisions slot.
/// @note A settlement with nobody in it contributes no day at all, rather
///       than a zero: an empty village is not a starving one.
void AccumulateVitals(const LifeConfig& config,
                      float hungry_satiety_threshold,
                      WorldState& current);

}  // namespace core

#endif  // CORE_RESIDENTS_VITALS_H_
