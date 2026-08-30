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

}  // namespace

void AccumulateVitals(WorldState& current) {
  VitalsState& vitals = current.vitals;
  // The year turns before today is counted: today belongs to the new year.
  if (current.calendar.day > 0 && current.calendar.day % kDaysPerYear == 0 &&
      vitals.satiety_running_days > 0) {
    PushYear(vitals, vitals.satiety_running_sum / static_cast<float>(vitals.satiety_running_days));
    vitals.satiety_running_sum = 0.0F;
    vitals.satiety_running_days = 0;
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
