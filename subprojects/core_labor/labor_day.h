/// @file
/// @brief The day arithmetic of core_labor: the working hour, the road, a
/// worker's efficiency and what work costs him in rest. The solar window
/// itself is shared with the boundary and lives in core_common/day_window.h.
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
#include "core_common/day_window.h"  // DayWindow / SolarWindow: shared with the boundary.
#include "core_common/geometry.h"
#include "core_common/labor_state.h"
#include "core_common/resident_state.h"
#include "core_common/world_state.h"
#include "labor_config.h"

namespace core {

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

/// @brief The fed factor of a worker's output (register 218; metrics §8,
/// «Голодный работает хуже»): 1 at personal satiety `satiety_full` and
/// above, linear down to the floor at `satiety_floor` and below. The floor
/// is `satiety_factor_floor_first_year` in the campaign's first year and
/// `satiety_factor_floor` after it.
float FedFactor(const EfficiencyFactors& factors, Metric satiety, bool first_year);

/// @brief The sober factor (register 218, the human's word of 2026-09-18):
/// 1 up to `alcoholism_from`, linear down to `alcoholism_factor_floor` at
/// `alcoholism_to` and above. The metric lives on men only, so a woman's
/// nought costs nothing — by the metric, not by a test of sex here.
float SoberFactor(const EfficiencyFactors& factors, Metric alcoholism);

/// @brief Whether `work` is one of the works alcoholism does not cut (the
/// human's word of 2026-09-18, metrics §8, «Где алкоголизм не режет»): of
/// the three, only «добыча в лесу для колхоза» is in the core — felling and
/// hauling off a timber stand. NETTING IN THE PONDS (register 219) is not
/// built yet, and fishing with a rod is not a work kind; neither has
/// anything to spare here until it exists. The lake ARTEL is NOT on the list
/// (boss, epoch1-next seq 54): it is an ordinary producing unit, and a
/// drinking artel fisher works worse like anyone.
bool AlcoholSparesWork(const WorkAssignment& work);

/// @brief The age at which output starts to decline: the settlement's life
/// expectancy less labor's own margin (decision 105). A derivative, never a
/// stored field — one fact, one home, and the home of life expectancy is
/// WorldState::vitals.
float AgingFromYears(const LaborConfig& config, const WorldState& world);

/// @brief A worker's output as a multiple of the reference worker's day:
/// the product of the age, health, rest, mood, education, skill, fed and
/// sober factors. Always >= 0.
/// @param aging_from_years From AgingFromYears above; passed in rather than
///        read here, so that this stays a pure function of its arguments.
/// @param first_year The campaign's first calendar year: the fed factor's
///        floor is the gentler one (FedFactor).
/// @param drink_spared The work is one of the three the human spared from
///        alcoholism (metrics §8, «Где алкоголизм не режет»): the sober
///        factor is 1 whatever the metric. See AlcoholSparesWork.
float ResidentEfficiency(const LaborConfig& config,
                         const ResidentRow& resident,
                         float age_years,
                         float aging_from_years,
                         bool first_year,
                         bool drink_spared);

/// @brief Rest lost for `norm_days` of delivered work of `kind`, in metric
/// points. Stamina and sportiness soften it; they never raise output.
float RestDrain(const LaborConfig& config,
                const ResidentRow& resident,
                WorkKind kind,
                float norm_days);

// IsDayOff moved to core_common/calendar.h on 2026-09-15 (host's request).

}  // namespace core

#endif  // CORE_LABOR_LABOR_DAY_H_
