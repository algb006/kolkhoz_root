// Internal to core_labor: the parsed labor configuration.
//
// Values mirror tables/labor.csv plus the norm columns of crops.csv,
// farming.csv and livestock.csv; the defaults here equal the canonical
// table contents so that a table set without them (unit tests, early runs)
// behaves like the shipped one. A present-but-malformed table is an error,
// never a silent fallback (parsing in labor_system.cpp).
//
// Norm convention: table columns keep REAL man-days (the human-readable
// numbers of the balance docs); parsing divides by kRealDaysPerGameDay
// once, so every value in this struct is already GAME man-days. The
// division lives in exactly one place.

#ifndef CORE_LABOR_LABOR_CONFIG_H_
#define CORE_LABOR_LABOR_CONFIG_H_

#include <array>
#include <cstdint>
#include <vector>

#include "core_common/labor_state.h"

namespace core {

/// The mandatory norm conversion (root rules §9): real man-days / 7 =
/// game man-days. Structural, not balance.
inline constexpr float kRealDaysPerGameDay = 7.0F;

/// Per-work-kind pay and hardness (tables/labor.csv, one row per kind).
struct WorkKindRates {
  /// Trudodni per delivered norm man-day (labor-payment design §2:
  /// 0.5 / 1.0 / 1.5 / 2.0 by grade). Epoch-I manual work is grade 1.0.
  float trudodni_rate = 1.0F;

  /// Rest drained per delivered norm man-day, in metric points; harvest
  /// and plowing are harder than barn care. ASSUMPTION until playtests.
  float rest_drain_per_norm_day = 8.0F;
};

/// Per-crop field-work norms in GAME man-days per hectare (crops.csv
/// columns sow_days_per_ha / harvest_days_per_ha, real in the table).
/// Indexed by CropId row; plowing and harrowing are crop-independent
/// (farming design §5: one plowing norm for any land) and sit in
/// LaborConfig directly.
struct CropWorkNorms {
  float sow_days_per_ha = 0.0F;

  float harvest_days_per_ha = 0.0F;
};

/// The labor configuration: everything the subsystem knows outside state.
struct LaborConfig {
  // -- норм sources (game man-days after parsing; see header comment) ------
  /// Plowing norm per hectare, any land, any year (farming.csv).
  float plow_days_per_ha = 1.4F;  // 10 real / 7

  /// Harrowing norm per hectare (farming.csv).
  float harrow_days_per_ha = 0.4F;  // ~3 real / 7

  /// Per-crop sowing and harvest norms; sized to the crops table. The
  /// grain anchor: plow 10 + harrow 3 + sow 3 + harvest 8 = 24 real
  /// man-days/ha (49-simulations §2, the corrected norms).
  std::vector<CropWorkNorms> crops;

  /// Yearly care norm per adult head, by livestock kind row
  /// (livestock.csv care_days_per_year, real in the table; the cow anchor
  /// is 32 real man-days a year). GAME man-days here.
  std::vector<float> care_days_per_year;

  // -- rates by work kind (labor.csv) --------------------------------------
  std::array<WorkKindRates, kWorkKindCount> rates = {};

  // -- the day (labor.csv; time design §6-§7) ------------------------------
  /// Hours of work behind one norm man-day.
  float standard_day_hours = 10.0F;

  /// One-way commute limit in game hours (~2h, time design §7; the exact
  /// value is a polish parameter).
  float travel_limit_hours = 2.0F;

  /// Straight-line to path-distance factor: roads are deferred in phase 1,
  /// so travel = euclidean x this / speed. ASSUMPTION.
  float path_factor = 1.3F;

  /// Real walking speed, km/h (transport.csv pedestrian row); the game
  /// speed is this / kClockScale.
  float walk_speed_kmh = 5.0F;

  /// Sleep hours per day, for the family's household_hours arithmetic.
  float sleep_hours = 8.0F;

  // -- rest and the walk-off (labor.csv; unit rules §8, metrics §11) -------
  /// Below this rest a worker stops and goes home — his own decision.
  float rest_walkoff_threshold = 15.0F;

  /// Rest recovered by a full day off, metric points.
  float rest_recovery_day_off = 20.0F;

  /// Rest recovered by a workday evening, metric points.
  float rest_recovery_evening = 4.0F;

  /// How much stamina+sportiness soften the drain: at 100 combined the
  /// drain is multiplied by (1 - this). ASSUMPTION.
  float stamina_drain_relief = 0.4F;

  // -- efficiency factors (labor.csv; education design §6) -----------------
  /// Multipliers by education stage for SIMPLE work: illiterate 0.85,
  /// primary 1.0, the rest 1.05 (diplomas mean little in the field).
  std::array<float, 5> education_factor = {0.85F, 1.0F, 1.05F, 1.05F, 1.05F};

  /// Self-education adds up to this at metric 100 (education §6: x1.10).
  float self_education_max_bonus = 0.10F;

  // -- placement (labor.csv; society design §1) ----------------------------
  /// Accountant placement quality 0-3. Campaign default 0: the start has
  /// no accountant, the chairman places naively. Rises when the post is
  /// filled (a later stage wires that; the knob exists now).
  std::uint8_t placement_level = 0;
};

}  // namespace core

#endif  // CORE_LABOR_LABOR_CONFIG_H_
