// The day arithmetic of core_labor (labor_day.h). Deliberately small and
// pure: everything here is a formula the balance may retune, and nothing
// here knows the world.

#include "labor_day.h"

#include <cmath>
#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/geometry.h"
#include "core_common/labor_state.h"
#include "core_common/quantities.h"
#include "core_common/resident_state.h"
#include "labor_config.h"

namespace core {
namespace {

/// @brief A metric as a deviation from the reference worker's value: 1.0 at
/// the pivot, `slope` per 100 metric points away from it. Never negative.
float PivotFactor(Metric metric, float pivot, float slope) {
  float clamped = metric < 0.0F ? 0.0F : metric;
  clamped = clamped > 100.0F ? 100.0F : clamped;
  const float factor = 1.0F + (slope * (clamped - pivot) / 100.0F);
  return factor > 0.0F ? factor : 0.0F;
}

/// @brief What fatigue does to output — the steps of decision 107, not a
/// slope: full work above the first threshold, -10% below it, -25% below
/// the second. The reference worker sits above both, so rest costs him
/// nothing until he is actually tired.
float RestFactor(const EfficiencyFactors& factors, Metric rest) {
  if (rest >= factors.rest_step_tired) {
    return 1.0F;
  }
  return rest >= factors.rest_step_spent ? factors.rest_factor_tired : factors.rest_factor_spent;
}

/// @brief The age curve of life-cycle §1: full strength on the plateau,
/// a slow decline afterwards. Everyone below working age is filtered out
/// before this is called, so the growing part of the curve (7-18) has no
/// effect in phase 1 and is deliberately not modelled.
float AgeFactor(const EfficiencyFactors& factors, float age_years) {
  if (age_years <= factors.aging_from_years) {
    return 1.0F;
  }
  const float lost = (age_years - factors.aging_from_years) * factors.age_decline_per_year;
  const float factor = 1.0F - lost;
  return factor < factors.age_decline_floor ? factors.age_decline_floor : factor;
}

}  // namespace

float HoursPerKm(const LaborConfig& config, WorkKind kind) {
  const float real_speed = IsHorseWork(kind) ? config.harness_speed_kmh : config.walk_speed_kmh;
  const float game_speed_kmh = real_speed / static_cast<float>(kClockScale);
  if (!(game_speed_kmh > 0.0F)) {
    return 0.0F;  // A malformed speed cannot pass parsing; be harmless anyway.
  }
  return config.path_factor / game_speed_kmh;
}

float TravelHours(const Vec2& from, const Vec2& to, float hours_per_km) {
  const float dx_km = (from.x - to.x) / 1000.0F;
  const float dy_km = (from.y - to.y) / 1000.0F;
  return std::sqrt((dx_km * dx_km) + (dy_km * dy_km)) * hours_per_km;
}

float BiologicalAgeYears(const LaborConfig& config, std::int32_t birth_day, SimDay day) {
  const float game_years = static_cast<float>(static_cast<std::int32_t>(day) - birth_day) /
                           static_cast<float>(kDaysPerYear);
  return game_years * config.life_speedup;
}

float FieldSkillBlend(const LaborConfig& config, const ResidentRow& resident) {
  const SkillBlend& weights = config.skill;
  const float blended = (weights.earned_weight * resident.skill_agriculture_earned) +
                        (weights.stamina_weight * resident.stamina) +
                        (weights.schooled_weight * resident.skill_agriculture_schooled);
  return blended > 100.0F ? 100.0F : blended;
}

float ResidentEfficiency(const LaborConfig& config, const ResidentRow& resident, float age_years) {
  const EfficiencyFactors& factors = config.efficiency;
  const auto stage = static_cast<std::uint32_t>(resident.education_stage);
  const float education =
      stage < config.education_factor.size() ? config.education_factor[stage] : 1.0F;
  const float self_taught =
      1.0F + (config.self_education_max_bonus * (resident.self_education / 100.0F));
  const float efficiency =
      AgeFactor(factors, age_years) *
      PivotFactor(resident.health, factors.health_pivot, factors.health_slope) *
      RestFactor(factors, resident.rest) *
      PivotFactor(resident.mood, factors.mood_pivot, factors.mood_slope) * education * self_taught *
      PivotFactor(FieldSkillBlend(config, resident), factors.skill_pivot, factors.skill_slope);
  return efficiency > 0.0F ? efficiency : 0.0F;
}

float RestDrain(const LaborConfig& config,
                const ResidentRow& resident,
                WorkKind kind,
                float norm_days) {
  const auto index = static_cast<std::uint32_t>(kind);
  if (index >= config.rates.size() || norm_days <= 0.0F) {
    return 0.0F;
  }
  // Endurance is the whole return on stamina and sport: the strong do not
  // out-produce anyone, they simply last (life-cycle §1).
  const float toughness = (resident.stamina + resident.sportiness) / 200.0F;
  const float relief = 1.0F - (config.stamina_drain_relief * toughness);
  return norm_days * config.rates[index].rest_drain_per_norm_day * (relief > 0.0F ? relief : 0.0F);
}

}  // namespace core
