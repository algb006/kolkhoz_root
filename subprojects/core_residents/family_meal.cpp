// The family's daily meal (family_meal.h): the need, what the pantry gives
// against it, and what that does to the people.
//
// The unit of account is the KILOCALORIE, and the norm is stated in grain
// equivalent — so a resource enters the meal through its own density and
// leaves it as a share of the norm. That is exactly the v4 balance's own
// arithmetic, which is what makes the stage criterion comparable to it.

#include "family_meal.h"

#include <cstddef>
#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/ids.h"

namespace core {
namespace {

float BiologicalAgeYears(float life_speedup, std::int32_t birth_day, SimDay day) {
  const float game_years = static_cast<float>(static_cast<std::int32_t>(day) - birth_day) /
                           static_cast<float>(kDaysPerYear);
  return game_years * life_speedup;
}

std::uint32_t EpochIndex(Epoch epoch) {
  return static_cast<std::uint32_t>(epoch) - 1;
}

float ClampMetric(float value) {
  const float low = value < kMetricMin ? kMetricMin : value;
  return low > kMetricMax ? kMetricMax : low;
}

/// @brief Did this person spend the day on one of the heavy kinds? The mask
/// is food's own (ConsumptionConfig::heavy_kinds_mask) rather than a read of
/// labor's parsed rates: the food side must not depend on another module's
/// configuration, and "which work is heavy" is a fact about the work, not
/// about the pay for it.
bool WorkedHeavy(const ConsumptionConfig& eat, const ResidentRow& resident) {
  const auto kind = static_cast<std::uint32_t>(resident.work.kind);
  if (kind >= 32U || resident.work.worked_norm_days_today <= 0.0F) {
    return false;
  }
  return (eat.heavy_kinds_mask & (1U << kind)) != 0U;
}

/// @brief True on the first day of a season — computed from the day number
/// rather than from a buffer comparison, because the meal happens at hour 23
/// and by then both buffers are already inside the new season.
bool IsFirstDayOfSeason(SimDay day) {
  if (day == 0) {
    return true;
  }
  return SeasonOfMonth(DateFromDay(day).month) != SeasonOfMonth(DateFromDay(day - 1).month);
}

/// @brief What the household owes its people today, in kilocalories.
///
/// The heavy-day surcharge is read from the PREVIOUS buffer: at hour 23 it
/// still holds hour 22, where the day's orders are alive. Row indices match
/// between the buffers at this point in the step — the decisions slot, which
/// is the only place that adds or removes people, runs after this phase.
float FamilyNeedKcal(const FoodConfig& config,
                     float life_speedup,
                     const WorldState& previous,
                     const WorldState& current,
                     FamilyId id,
                     SimDay day) {
  float kilograms = 0.0F;
  for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
    const ResidentRow& resident = current.residents.rows[row];
    if (resident.family.value != id.value) {
      continue;
    }
    const bool heavy = row < previous.residents.rows.size() &&
                       WorkedHeavy(config.consumption, previous.residents.rows[row]);
    kilograms += DailyNeedKilograms(
        config.consumption, BiologicalAgeYears(life_speedup, resident.birth_day, day), heavy);
  }
  return kilograms * static_cast<float>(kGramsPerKilogram) *
         config.consumption.grain_reference_kcal_per_gram;
}

/// @brief How many pantry entries both the roster and the family have.
std::uint32_t PantryRoster(const FoodConfig& config, const FamilyRow& family) {
  const std::size_t smaller = family.pantry.size() < config.resources.size()
                                  ? family.pantry.size()
                                  : config.resources.size();
  return static_cast<std::uint32_t>(smaller);
}

/// @brief Puts the meal on the table and takes it out of the pantry.
///
/// Eating is PROPORTIONAL to what is stored: it is deterministic, it burns
/// the bins evenly instead of emptying one at a time, and it fills the
/// variety mask with what actually stood on the table.
/// @return The kilocalories eaten; never more than `need_kcal`.
float EatFromPantry(const FoodConfig& config, FamilyRow& family, float need_kcal) {
  const std::uint32_t roster = PantryRoster(config, family);
  float available_kcal = 0.0F;
  for (std::uint32_t index = 0; index < roster; ++index) {
    if (config.resources[index].kcal_per_gram > 0.0F && family.pantry[index] > 0) {
      available_kcal +=
          static_cast<float>(family.pantry[index]) * config.resources[index].kcal_per_gram;
    }
  }
  if (!(available_kcal > 0.0F) || !(need_kcal > 0.0F)) {
    return 0.0F;
  }
  const float share = need_kcal < available_kcal ? need_kcal / available_kcal : 1.0F;
  float eaten_kcal = 0.0F;
  for (std::uint32_t index = 0; index < roster; ++index) {
    const FoodResourceDef& def = config.resources[index];
    if (def.kcal_per_gram <= 0.0F || family.pantry[index] <= 0) {
      continue;
    }
    const auto take = GramsFromFloat(static_cast<float>(family.pantry[index]) * share);
    if (take <= 0) {
      continue;
    }
    family.pantry[index] -= take;
    eaten_kcal += static_cast<float>(take) * def.kcal_per_gram;
    if (def.category != FoodCategory::kNotFood && def.category != FoodCategory::kCount) {
      family.food_variety_mask |=
          static_cast<std::uint16_t>(1U << static_cast<std::uint32_t>(def.category));
    }
  }
  return eaten_kcal;
}

/// @brief Moves the members toward the day's outcome.
///
/// Satiety drifts toward "how much of the norm was met", slowly by design:
/// February's failure cannot be hidden by September, so the design's monthly
/// reading of the metric is guaranteed by inertia alone. Both health rates
/// are stated per week and charged daily, the way labor charges its own
/// weekly loss; recovery is the slower of the two, as health design §2 asks.
void MoveSatietyAndHealth(const FoodConfig& config,
                          WorldState& current,
                          FamilyId id,
                          float target) {
  const SatietyConfig& satiety = config.satiety;
  const float drift = satiety.drift_per_day;
  const float loss_per_day = satiety.health_loss_per_week / static_cast<float>(kDaysPerWeek);
  const float gain_per_day = satiety.health_recovery_per_week / static_cast<float>(kDaysPerWeek);
  for (ResidentRow& resident : current.residents.rows) {
    if (resident.family.value != id.value) {
      continue;
    }
    const float delta = target - resident.satiety;
    float step = delta;
    step = step > drift ? drift : step;
    step = step < -drift ? -drift : step;
    resident.satiety = ClampMetric(resident.satiety + step);
    if (resident.satiety < satiety.health_loss_satiety_threshold) {
      resident.health = ClampMetric(resident.health - loss_per_day);
    } else if (resident.satiety >= satiety.health_recovery_satiety_threshold) {
      resident.health = ClampMetric(resident.health + gain_per_day);
    }
  }
}

}  // namespace

/// @brief One person's share of the adult norm today, in grain-equivalent
/// kilograms per GAME day (metrics design §8).
///
/// Nothing until the breast is left behind, a straight ramp from there to
/// the adult norm at sixteen, a little less in old age, a fifth more on a
/// day of heavy work. The ramp is linear because the design draws it that
/// way; the two multipliers are ASSUMPTION and live in the table.
float DailyNeedKilograms(const ConsumptionConfig& eat, float age_years, bool worked_heavy) {
  if (age_years < eat.eat_from_bio_years) {
    return 0.0F;  // still at the breast: the pantry feeds the mother
  }
  const float adult_per_day = eat.adult_kg_grain_eq_per_year / static_cast<float>(kDaysPerYear);
  float need = adult_per_day;
  if (age_years < eat.adult_from_bio_years) {
    const float span = eat.adult_from_bio_years - eat.eat_from_bio_years;
    need =
        span > 0.0F ? adult_per_day * (age_years - eat.eat_from_bio_years) / span : adult_per_day;
  } else if (age_years >= eat.elderly_from_bio_years) {
    need = adult_per_day * eat.elderly_factor;
  }
  return worked_heavy ? need * eat.heavy_work_factor : need;
}

float SettlementDailyNeedKilograms(const FoodConfig& config,
                                   float life_speedup,
                                   const WorldState& world) {
  float kilograms = 0.0F;
  for (const ResidentRow& resident : world.residents.rows) {
    const float age = BiologicalAgeYears(life_speedup, resident.birth_day, world.calendar.day);
    kilograms += DailyNeedKilograms(config.consumption, age, false);
  }
  return kilograms;
}

void RunFamilyMeal(const FoodConfig& config,
                   float life_speedup,
                   const WorldState& previous,
                   WorldState& current,
                   std::uint32_t family_item) {
  if (HourFromTick(current.calendar.tick) != kTicksPerDay - 1U || config.resources.empty()) {
    return;  // one meal a day, and none at all without a food roster
  }
  const SimDay day = current.calendar.day;
  const FamilyId id = current.families.row_ids[family_item];
  FamilyRow& family = current.families.rows[family_item];
  if (IsFirstDayOfSeason(day)) {
    family.food_variety_mask = 0;  // a new season sets a new table
  }
  const float need_kcal = FamilyNeedKcal(config, life_speedup, previous, current, id, day);
  const float eaten_kcal = EatFromPantry(config, family, need_kcal);
  // A household that owes nobody anything — nobody in it old enough to eat
  // from the pantry — is fed by definition rather than starving on zero.
  const float target = need_kcal > 0.0F ? kMetricMax * (eaten_kcal / need_kcal) : kMetricMax;
  MoveSatietyAndHealth(config, current, id, target);
  // And the slow reading of it, for the mechanics that must not mistake a
  // season for a hardship (family_state.h, FamilyRow::satiety_year_mean).
  float total = 0.0F;
  std::uint32_t counted = 0;
  for (const ResidentRow& resident : current.residents.rows) {
    if (resident.family.value == id.value) {
      total += resident.satiety;
      ++counted;
    }
  }
  if (counted > 0) {
    const float today = total / static_cast<float>(counted);
    family.satiety_year_mean +=
        (today - family.satiety_year_mean) / static_cast<float>(kDaysPerYear);
  }
}

Metric SatietyComponent(const FoodConfig& config,
                        const WorldState& current,
                        std::uint32_t family_item) {
  const FamilyId id = current.families.row_ids[family_item];
  const FamilyRow& family = current.families.rows[family_item];
  float total = 0.0F;
  std::uint32_t counted = 0;
  for (const ResidentRow& resident : current.residents.rows) {
    if (resident.family.value == id.value) {
      total += resident.satiety;
      ++counted;
    }
  }
  if (counted == 0) {
    return family.component_satiety;  // an empty row keeps its last value
  }
  const float mean = total / static_cast<float>(counted);
  std::uint32_t categories = 0;
  for (std::uint32_t bit = 0; bit < static_cast<std::uint32_t>(FoodCategory::kCount); ++bit) {
    if ((family.food_variety_mask & (1U << bit)) != 0U) {
      ++categories;
    }
  }
  const float norm = config.satiety.categories_norm_by_epoch[EpochIndex(current.epoch)];
  const float missing = norm - static_cast<float>(categories);
  const float short_by = missing > 0.0F ? missing : 0.0F;
  const float ceiling = kMetricMax - (config.satiety.missing_category_penalty * short_by);
  return ClampMetric(mean < ceiling ? mean : ceiling);
}

}  // namespace core
