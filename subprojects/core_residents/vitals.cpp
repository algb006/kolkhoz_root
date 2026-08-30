// The vitals window (vitals.h): daily accumulation of the settlement's mean
// satiety and its yearly fold into the three-year window decision 105 reads.

#include "vitals.h"

#include <cstdint>

#include "core_common/calendar.h"

namespace core {
namespace {

/// @brief Mean satiety over every resident; the count is returned separately
/// so an empty settlement can be told from a satiety of zero.
float SettlementMeanSatiety(const WorldState& world, std::uint32_t& counted) {
  float total = 0.0F;
  counted = 0;
  for (const ResidentRow& resident : world.residents.rows) {
    total += resident.satiety;
    ++counted;
  }
  return counted == 0 ? 0.0F : total / static_cast<float>(counted);
}

/// @brief Shifts the three-year window one year on: the oldest year drops
/// out, the finished one enters last.
void PushYear(VitalsState& vitals, float year_mean) {
  for (std::uint32_t index = 1; index < vitals.satiety_year_means.size(); ++index) {
    vitals.satiety_year_means[index - 1] = vitals.satiety_year_means[index];
  }
  vitals.satiety_year_means.back() = year_mean;
}

/// Life expectancy, once a year (decision 105):
///   LE = base + medicine + nutrition + living + working conditions,
/// every factor averaged over three years. Phase 1 keeps medicine and living
/// at zero and working conditions a constant, so only nutrition moves — and
/// nutrition is the settlement's own mean satiety, mapped around a neutral
/// point and clamped to the band the design gives it.
///
/// The three-year window is not smoothing for its own sake: it is what makes
/// one bad winter cost years of life slowly rather than at once, and what
/// makes recovery just as slow.
void RecomputeLifeExpectancy(const VitalsConfig& config, VitalsState& vitals) {
  float window = 0.0F;
  for (const float year_mean : vitals.satiety_year_means) {
    window += year_mean;
  }
  window /= static_cast<float>(vitals.satiety_year_means.size());
  float nutrition = (window - config.satiety_neutral) * config.satiety_to_years_slope;
  nutrition = nutrition < config.nutrition_years_min ? config.nutrition_years_min : nutrition;
  nutrition = nutrition > config.nutrition_years_max ? config.nutrition_years_max : nutrition;
  vitals.life_expectancy_years = config.base_years + config.medicine_years + nutrition +
                                 config.living_years + config.working_conditions_years;
}

}  // namespace

void AccumulateVitals(const LifeConfig& config, WorldState& current) {
  VitalsState& vitals = current.vitals;
  // The year turns before today is counted: today belongs to the new year.
  if (current.calendar.day > 0 && current.calendar.day % kDaysPerYear == 0) {
    if (vitals.satiety_running_days > 0) {
      PushYear(vitals,
               vitals.satiety_running_sum / static_cast<float>(vitals.satiety_running_days));
      vitals.satiety_running_sum = 0.0F;
      vitals.satiety_running_days = 0;
    }
    RecomputeLifeExpectancy(config.vitals, vitals);
  }
  std::uint32_t counted = 0;
  const float mean = SettlementMeanSatiety(current, counted);
  if (counted == 0) {
    return;
  }
  vitals.satiety_running_sum += mean;
  vitals.satiety_running_days += 1;
}

}  // namespace core
