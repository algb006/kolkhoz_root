/// @file
/// @brief THE NEW HOUSEHOLD'S ADVANCE (labor-payment design, «Аванс
///        новосёлу»; #170, 0.37.246): who is advanced, and how much - the
///        rule's own arithmetic, with no store in it.
/// @threading SINGLE_THREADED
/// Pure functions of their arguments, called from the family exchange in the
/// residents sub-step of the decisions phase (family_exchange.cpp moves the
/// goods; this file only decides).
///
/// WHY (boss's decision of 8 October 2026 on econ's reading, widened 13
/// October on core's print): the satiety floor is drawn for a family WITH A
/// YARD - the yard feeds 59 % of its table and the ration tops it up. A
/// household that has just come into being has neither a yard's harvest nor
/// trudodni to be issued against: an arrival in a barrack lives on the ration
/// alone, about 43 % of the norm, and a couple wed in the village starts on a
/// quarter of a parents' pantry. Nine canon villages, households under a game
/// year, year 3: hungry on 61 household-days in a barrack before the first
/// trudoden and on 55 in a house of their own - beside full stores, with
/// nothing the chairman could have foreseen.
///
/// THE RULE: a household formed in play is ADVANCED on the exchange's days
/// while it has earned less than `trudodni_per_worker` trudodni per worker
/// since it was formed, no longer than `ceiling_days`, and - one formed in a
/// barrack - only while it still lives in one. The store tops its pantry up
/// to the norm of the period: the eaters' need LESS what the pantry holds
/// after the day's issue and ration - to the norm, never above it.
/// Only out of the free remainder. It is a loan against trudodni: of every
/// later issue, half of what it gives ABOVE the period's norm stays in the
/// store until the loan is paid (IssueAboveNorm; never out of the norm).
/// A household with no worker gets nothing (0 < 0 is false): a loan with
/// nobody to earn it back is not this rule's (boss, 13 October 2026).
#ifndef CORE_RESIDENTS_HOUSEHOLD_ADVANCE_H_
#define CORE_RESIDENTS_HOUSEHOLD_ADVANCE_H_

#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/family_state.h"
#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_common/world_state.h"
#include "food_config.h"

namespace core {

/// @brief A household as the advance counts it, on one day.
struct HouseholdCounts {
  /// Members who eat out of the pantry and are at home (EaterCount's test).
  std::uint32_t eaters = 0;

  /// Of them, those of working age - from the adult norm's age to the
  /// elderly one (food.csv). NOT the accountant's «can be put to work»: that
  /// test is core_labor's and this module does not reach it; the band is the
  /// one the print behind the rule used.
  std::uint32_t workers = 0;

  /// The eaters' need of one ordinary day, kilograms of grain equivalent
  /// (family_meal.h, DailyNeedKilograms, nobody's day counted heavy).
  float need_kg_per_day = 0.0F;
};

/// @brief Counts `family` on `day`. A member away in the district eats the
///        district's bread and is in no count.
HouseholdCounts CountHousehold(const FoodConfig& config,
                               float life_speedup,
                               const WorldState& world,
                               FamilyId family,
                               SimDay day);

/// @brief THE ONE PREDICATE: whether `family` is advanced on `day`. True
///        while all hold - the row was formed in play; the ceiling's days
///        have not run out; it has a worker and has earned less than
///        `trudodni_per_worker` a worker since formed; and, formed in a
///        barrack, it is still in one.
/// @note «Any household under a game year» later is the fourth clause out of
///       this body - there is no second place to change.
bool AdvanceEligible(const AdvanceRules& rules,
                     const FamilyRow& family,
                     const HouseholdCounts& counts,
                     SimDay day);

/// @brief Grams of GRAIN EQUIVALENT one gram of roster position `index` is
///        (kcal per gram over the reference grain's); 0 for what is no food.
float GrainEquivalentPerGram(const FoodConfig& config, std::uint32_t index);

/// @brief The food in a pantry, grams of grain equivalent.
Grams PantryGrainEquivalent(const FoodConfig& config, const ResourceAmounts& pantry);

/// @brief What the store tops an eligible household up with on an exchange
///        day, grams of grain equivalent: the need of `period_days` less
///        what the pantry holds, never below nought. To the norm and never
///        above it; there is no cap in kilograms - the ceiling's days are
///        the advance's only limit.
/// @param pantry_grain_eq PantryGrainEquivalent AFTER the day's issue and
///        ration - they are in the pantry too.
Grams AdvanceTopUp(const HouseholdCounts& counts, std::uint32_t period_days, Grams pantry_grain_eq);

/// @brief The part of an issue that lifts the family's pantry OVER the norm
///        of the period, grams of grain equivalent: the only part a repayment
///        is taken from. An issue that leaves the family at or under the norm
///        has none.
/// @param pantry_grain_eq What the pantry holds BEFORE this issue.
Grams IssueAboveNorm(const HouseholdCounts& counts,
                     std::uint32_t period_days,
                     Grams pantry_grain_eq,
                     Grams issue_grain_eq);

/// @brief What the store keeps of an issue against the family's debt, grams
///        of grain equivalent: `repay_share` of what the issue gives ABOVE
///        THE NORM (IssueAboveNorm), never more than the debt.
///
/// NOT OF THE WHOLE ISSUE (boss, 13 October 2026, on the first pair of
/// 0.37.246): kept as half of every issue, the repayment took half of a
/// thin issue from households just past their first trudoden - in a barrack
/// past the line the hungry household-days of year 3 rose 44 -> 65 over nine
/// villages, on days 17-48, while the advance had taken 78 to nought before
/// the line. A loan that puts its debtor under the line of hunger is the
/// rule's fault, not its price.
Grams AdvanceRepayment(const AdvanceRules& rules, Grams debt, Grams above_norm_grain_eq);

}  // namespace core

#endif  // CORE_RESIDENTS_HOUSEHOLD_ADVANCE_H_
