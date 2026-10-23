/// @file family_food_watch.h
/// @brief The family's own food's daily mean, which the hunger lamp's yellow
///        stage reads (#1046; econ forecast-lamp-2026-09-30 §2).
/// @threading SINGLE_THREADED - the residents' food writers and day close
///        (core_residents, phases 3 and 6), and the yard's produce
///        (core_production, phase 3). Both modules write FamilyRow; the
///        helpers are here because neither module may include the other.
///
/// The 12-day trudodni ring that stood here beside the mean (save 168) left
/// with 0.37.295 (save 169): from 0.37.292 the forecast's share is the
/// family's workers (CountHousehold; econ §6 (a1)), and nothing read the ring.

#ifndef CORE_COMMON_FAMILY_FOOD_WATCH_H_
#define CORE_COMMON_FAMILY_FOOD_WATCH_H_

#include "core_common/family_state.h"

namespace core {

/// The own food's mean's time constant, days (econ §2's «7 суток»; the
/// exponential mean is the contract's named deviation, boss [2]).
inline constexpr float kOwnFoodMeanDays = 7.0F;

/// @brief Books `kcal` of the family's OWN food brought into its pantry today
///        (the plot, the yard, the nets, the forest's basket - never the issue).
inline void AddTodaysOwnKcal(FamilyRow& family, float kcal) {
  if (kcal > 0.0F) {
    family.own_kcal_today += kcal;
  }
}

/// @brief The family's food day closes: today's own kcal fold into the mean
///        and clear.
inline void CloseFamilyFoodDay(FamilyRow& family) {
  family.own_kcal_mean += (family.own_kcal_today - family.own_kcal_mean) / kOwnFoodMeanDays;
  family.own_kcal_today = 0.0F;
}

}  // namespace core

#endif  // CORE_COMMON_FAMILY_FOOD_WATCH_H_
