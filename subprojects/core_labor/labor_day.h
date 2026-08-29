/// @file
/// @brief The day arithmetic of core_labor: the solar window, the road, a
/// worker's efficiency and what work costs him in rest.
/// @threading SINGLE_THREADED
/// Pure functions of their arguments, called from the labor sub-step of the
/// decisions slot on the sim thread. No world state, no tables, no RNG — the
/// unit tests call them with plain structs.
///
/// Design sources: time design §6 (the workday by the sun, the road home
/// counted in), §7 (the commute limit), education §6 and §11 (what
/// education and professionalism are worth on simple field work), life-cycle
/// §1 (the age curve; stamina and sportiness slow fatigue instead of raising
/// output), metrics §11 (rest is the fatigue metric, seen from the other
/// side). The formulas themselves are the core's own decision,
/// manual/65-labor-model.md §5.

#ifndef CORE_LABOR_LABOR_DAY_H_
#define CORE_LABOR_LABOR_DAY_H_

#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/geometry.h"
#include "core_common/labor_state.h"
#include "core_common/resident_state.h"
#include "core_common/world_state.h"  // Epoch: the day off widens in Epoch III.
#include "labor_config.h"

namespace core {

/// @brief Today's daylight, as hours from midnight. The core's clock has no
/// timezone, so the window is centred on noon: what matters is its length
/// and that everyone shares it.
struct DayWindow {
  float sunrise = 6.0F;

  float sunset = 18.0F;
};

constexpr DayWindow SolarWindow(float daylight_hours) {
  const float half = daylight_hours * 0.5F;
  return DayWindow{.sunrise = 12.0F - half, .sunset = 12.0F + half};
}

/// @brief How much of the tick's hour [hour, hour + 1) lies inside
/// [from, to]. 0 when the hour is outside it entirely.
constexpr float HoursInside(std::uint32_t hour, float from, float to) {
  const auto tick_start = static_cast<float>(hour);
  const float tick_end = tick_start + 1.0F;
  const float begin = from > tick_start ? from : tick_start;
  const float end = to < tick_end ? to : tick_end;
  return end > begin ? end - begin : 0.0F;
}

/// @brief Game hours per kilometre of straight-line distance for a job of
/// this kind: the path factor over the effective speed of the unified
/// chronometer (real km/h divided by kClockScale). A harnessed order rides
/// out with the horse and covers the distance faster — the plowman does not
/// walk (decision 103, boss parcel of 2026-08-29); every other order goes on
/// foot. Walking 5 km/h with factor 1.3 gives ~3.12 h/km — the design's
/// ~800 m of path inside two game hours.
float HoursPerKm(const LaborConfig& config, WorkKind kind);

/// @brief One-way travel between two map points, in game hours.
float TravelHours(const Vec2& from, const Vec2& to, float hours_per_km);

/// @brief Biological age in years (the clock runs life_speedup times faster
/// than the calendar; demography design §2). May be negative for a resident
/// born after `day`, which never happens in a well-formed world.
float BiologicalAgeYears(const LaborConfig& config, std::int32_t birth_day, SimDay day);

/// @brief The "skill" that Epoch-I field work rewards, 0-100: practice
/// first, bodily stamina next, the diploma part last (education §6 —
/// on plowing and mowing it is strength that decides, not schooling).
float FieldSkillBlend(const LaborConfig& config, const ResidentRow& resident);

/// @brief Norm man-days this resident delivers over one standard working
/// day: the product of the age, health, rest, mood, education and skill
/// factors. Always >= 0.
/// @note Satiety and alcoholism belong in this product by design
/// (life-cycle §1) and are left out on purpose while they are STUB
/// constants — a constant factor would only rescale the balance.
float ResidentEfficiency(const LaborConfig& config, const ResidentRow& resident, float age_years);

/// @brief Rest lost for `norm_days` of delivered work of `kind`, in metric
/// points. Stamina and sportiness soften it; they never raise output.
float RestDrain(const LaborConfig& config,
                const ResidentRow& resident,
                WorkKind kind,
                float norm_days);

/// @brief Is this a day off? Sunday in Epochs I-II, Saturday joins it in
/// Epoch III (time design §12). Field work stops; the barn does not
/// (manual/65-labor-model.md §5).
constexpr bool IsDayOff(Weekday weekday, Epoch epoch) {
  if (weekday == Weekday::kSunday) {
    return true;
  }
  return weekday == Weekday::kSaturday && epoch == Epoch::kThree;
}

}  // namespace core

#endif  // CORE_LABOR_LABOR_DAY_H_
