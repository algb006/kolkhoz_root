/// @file family_food_watch.h
/// @brief The family's two memories the hunger lamp's yellow stage reads
///        (#1046; econ forecast-lamp-2026-09-30 §2): the trudodni of the last
///        kHungerShareDays days and its own food's daily mean.
/// @threading SINGLE_THREADED - the trudodni accrual (core_labor, phase 3)
///        and the residents' food writers and day close (core_residents,
///        phases 3 and 6). Both modules write FamilyRow; the helpers are here
///        because neither module may include the other.
///
/// THE RING'S SLOT IS TODAY'S: `calendar.day % kHungerShareDays`. The day's
/// close clears TOMORROW's slot (StartFamilyFoodDay), so the ring holds
/// exactly the last kHungerShareDays days, today's included, a day with no
/// work writing nought.

#ifndef CORE_COMMON_FAMILY_FOOD_WATCH_H_
#define CORE_COMMON_FAMILY_FOOD_WATCH_H_

#include "core_common/calendar.h"
#include "core_common/family_state.h"
#include "core_common/quantities.h"

namespace core {

/// The own food's mean's time constant, days (econ §2's «7 суток»; the
/// exponential mean is the contract's named deviation, boss [2]).
inline constexpr float kOwnFoodMeanDays = 7.0F;

/// @brief Books `hundredths` of trudodni earned today into the family's ring.
inline void AddTodaysTrudodni(FamilyRow& family, SimDay today, TrudodniHundredths hundredths) {
  family.trudodni_by_day[today % kHungerShareDays] += hundredths;
}

/// @brief Books `kcal` of the family's OWN food brought into its pantry today
///        (the plot, the yard, the nets, the forest's basket - never the issue).
inline void AddTodaysOwnKcal(FamilyRow& family, float kcal) {
  if (kcal > 0.0F) {
    family.own_kcal_today += kcal;
  }
}

/// @brief The family's food day closes: today's own kcal fold into the mean
///        and clear, and tomorrow's ring slot is cleared for tomorrow's work.
/// @param today The closing day.
inline void CloseFamilyFoodDay(FamilyRow& family, SimDay today) {
  family.own_kcal_mean += (family.own_kcal_today - family.own_kcal_mean) / kOwnFoodMeanDays;
  family.own_kcal_today = 0.0F;
  family.trudodni_by_day[(today + 1U) % kHungerShareDays] = 0;
}

/// @brief The family's trudodni over the ring's days, hundredths.
inline TrudodniHundredths RingTrudodni(const FamilyRow& family) {
  TrudodniHundredths sum = 0;
  for (const TrudodniHundredths day : family.trudodni_by_day) {
    sum += day;
  }
  return sum;
}

}  // namespace core

#endif  // CORE_COMMON_FAMILY_FOOD_WATCH_H_
