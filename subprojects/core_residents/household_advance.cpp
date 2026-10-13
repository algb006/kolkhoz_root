// The new household's advance: who and how much (household_advance.h).

#include "household_advance.h"

#include <cstddef>
#include <cstdint>

#include "core_common/away_in_district.h"
#include "core_common/calendar.h"
#include "core_common/family_state.h"
#include "core_common/quantities.h"
#include "core_common/resident_state.h"
#include "core_common/world_state.h"
#include "family_meal.h"
#include "food_config.h"

namespace core {

HouseholdCounts CountHousehold(const FoodConfig& config,
                               float life_speedup,
                               const WorldState& world,
                               FamilyId family,
                               SimDay day) {
  const ConsumptionConfig& eat = config.consumption;
  HouseholdCounts counts;
  for (const ResidentRow& resident : world.residents.rows) {
    if (resident.family.value != family.value || AwayInDistrict(resident, world.calendar.tick)) {
      continue;
    }
    const float age = BiologicalAgeYears(life_speedup, resident.birth_day, day);
    if (age < eat.eat_from_bio_years) {
      continue;
    }
    ++counts.eaters;
    counts.need_kg_per_day += DailyNeedKilograms(eat, age, false);
    if (age >= eat.adult_from_bio_years && age < eat.elderly_from_bio_years) {
      ++counts.workers;
    }
  }
  return counts;
}

bool AdvanceEligible(const AdvanceRules& rules,
                     const FamilyRow& family,
                     const HouseholdCounts& counts,
                     SimDay day) {
  if (family.formed_day == kNotFormedInPlay || day < family.formed_day ||
      day - family.formed_day > rules.ceiling_days || rules.ceiling_days == 0) {
    return false;
  }
  // The trudoden's line, in hundredths: less than `trudodni_per_worker` a
  // worker. No worker - nothing is less than nought.
  const float line = rules.trudodni_per_worker * static_cast<float>(counts.workers) *
                     static_cast<float>(kTrudodniScale);
  if (!(static_cast<float>(family.earned_since_formed) < line)) {
    return false;
  }
  // The barrack family's other end: the day it moves to a house with a yard.
  return family.formed_in_barrack == 0 || family.in_barrack != 0;
}

float GrainEquivalentPerGram(const FoodConfig& config, std::uint32_t index) {
  const float reference = config.consumption.grain_reference_kcal_per_gram;
  if (index >= config.resources.size() || !(reference > 0.0F)) {
    return 0.0F;
  }
  const float kcal = config.resources[index].kcal_per_gram;
  return kcal > 0.0F ? kcal / reference : 0.0F;
}

Grams PantryGrainEquivalent(const FoodConfig& config, const ResourceAmounts& pantry) {
  double grams = 0.0;
  for (std::size_t index = 0; index < pantry.size(); ++index) {
    if (pantry[index] > 0) {
      grams += static_cast<double>(pantry[index]) * static_cast<double>(GrainEquivalentPerGram(
                                                        config, static_cast<std::uint32_t>(index)));
    }
  }
  return static_cast<Grams>(grams);
}

Grams AdvanceTopUp(const HouseholdCounts& counts,
                   std::uint32_t period_days,
                   Grams pantry_grain_eq) {
  const Grams need = GramsFromKilograms(counts.need_kg_per_day * static_cast<float>(period_days));
  return need > pantry_grain_eq ? need - pantry_grain_eq : 0;
}

Grams IssueAboveNorm(const HouseholdCounts& counts,
                     std::uint32_t period_days,
                     Grams pantry_grain_eq,
                     Grams issue_grain_eq) {
  const Grams norm = GramsFromKilograms(counts.need_kg_per_day * static_cast<float>(period_days));
  const Grams after = pantry_grain_eq + issue_grain_eq;
  const Grams above = after > norm ? after - norm : 0;
  return above < issue_grain_eq ? above : issue_grain_eq;
}

Grams AdvanceRepayment(const AdvanceRules& rules, Grams debt, Grams above_norm_grain_eq) {
  if (debt <= 0 || above_norm_grain_eq <= 0) {
    return 0;
  }
  const auto share = static_cast<Grams>(static_cast<double>(above_norm_grain_eq) *
                                        static_cast<double>(rules.repay_share));
  return share < debt ? share : debt;
}

}  // namespace core
