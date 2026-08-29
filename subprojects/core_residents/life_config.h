// Internal to core_residents: the parsed demography configuration.
//
// Values mirror tables/life.csv, tables/demography.csv and
// tables/satisfaction.csv; the defaults here equal the canonical table
// contents so that a table set without these tables (unit tests, early
// runs) behaves like the shipped one. A present-but-malformed table is an
// error, never a silent fallback (parsing in residents_system.cpp).
//
// Stage 6 adds the vitals block (life expectancy, decision 105) and the
// birth-conditions block (decision 106); their keys live in life.csv too.

#ifndef CORE_RESIDENTS_LIFE_CONFIG_H_
#define CORE_RESIDENTS_LIFE_CONFIG_H_

#include <array>
#include <cstdint>

#include "core_common/ids.h"

namespace core {

/// Life expectancy and its factors (decision 105; manual/66-food-model.md
/// §7). LE = base + medicine + nutrition + living + working conditions,
/// recomputed once a year from 3-year factor means. Phase 1: only
/// nutrition is alive; medicine and living stay 0, working conditions is a
/// constant knob.
struct VitalsConfig {
  float base_years = 60.0F;

  /// Settlement mean satiety that maps to a zero nutrition contribution,
  /// and the slope of the mapping: contribution = clamp((satiety - neutral)
  /// x slope, -4, +4).
  float satiety_neutral = 70.0F;

  float satiety_to_years_slope = 0.1333F;  ///< ASSUMPTION: +-4 over +-30.

  float nutrition_years_min = -4.0F;

  float nutrition_years_max = 4.0F;

  /// Phase-1 constants of the dormant factors, years.
  float medicine_years = 0.0F;  ///< STUB source: medicine (phase 2).

  float living_years = 0.0F;  ///< STUB source: housing comfort (phase 2).

  float working_conditions_years = 0.0F;  ///< ASSUMPTION: constant, range -2..+2.
};

// The LE derivatives live with their consumers: the aging margin (LE - 20,
// decision 105) is labor's knob (labor.csv aging_margin_years replaces
// aging_from_years), the last-birth-median formula stays a fertility STUB.

/// Birth conditions (decision 106): satisfaction scales the birth rate,
/// hunger and the mother's health stop new pregnancies outright.
struct BirthConditionsConfig {
  /// Band upper bounds over family satisfaction and the multiplier of each
  /// band (design: 0.75 / 0.9 / 1.0 / 1.1 / 1.15). Bounds are ASSUMPTION;
  /// the multipliers are canon.
  std::array<float, 4> satisfaction_bounds = {30.0F, 45.0F, 60.0F, 75.0F};

  std::array<float, 5> multipliers = {0.75F, 0.9F, 1.0F, 1.1F, 1.15F};

  /// No new pregnancies while family satiety is below this (canon 40).
  float satiety_stop = 40.0F;

  /// No new pregnancies while the woman's health is below this (canon 40).
  float mother_health_stop = 40.0F;
};

/// Per-epoch demography parameters (demography design §4).
struct EpochDemography {
  float children_per_family = 6.5F;

  float child_mortality_percent = 28.0F;

  float outflow_percent_per_year = 0.0F;
};

/// Per-epoch weights of the satisfaction components (metrics design §7).
struct SatisfactionWeights {
  float satiety = 45.0F;

  float common_cause = 20.0F;

  float needs = 25.0F;

  float rest = 10.0F;
};

/// The biological clock and its thresholds (tables/life.csv). Ages are
/// biological years; life_speedup maps them to game years.
struct LifeConfig {
  /// unit_types.csv "house" row: what the wedding STUB builds (invalid in a
  /// table-less world — the unit is appended with an invalid type then).
  UnitTypeId house_type;

  float life_speedup = 4.0F;

  float adult_age_years = 16.0F;

  float marriage_age_years = 18.0F;

  float fertility_from_years = 18.0F;

  float fertility_to_years = 40.0F;

  float mortality_age_mid_years = 45.0F;

  float mortality_age_old_years = 60.0F;

  float mortality_young_percent_per_year = 0.3F;

  float mortality_mid_percent_per_year = 1.5F;

  float mortality_old_percent_per_year = 20.0F;

  float migration_per_year = 8.0F;

  std::uint32_t epoch2_population = 500;

  std::uint32_t epoch3_population = 1200;

  float marriage_chance_percent_per_day = 25.0F;

  float sex_balance_gain = 0.3F;

  std::array<EpochDemography, 3> epochs = {{
      {.children_per_family = 6.5F,
       .child_mortality_percent = 28.0F,
       .outflow_percent_per_year = 0.0F},
      {.children_per_family = 4.0F,
       .child_mortality_percent = 12.0F,
       .outflow_percent_per_year = 0.0F},
      {.children_per_family = 2.2F,
       .child_mortality_percent = 4.0F,
       .outflow_percent_per_year = 1.5F},
  }};

  std::array<SatisfactionWeights, 3> weights = {{
      {.satiety = 45.0F, .common_cause = 20.0F, .needs = 25.0F, .rest = 10.0F},
      {.satiety = 30.0F, .common_cause = 20.0F, .needs = 30.0F, .rest = 20.0F},
      {.satiety = 15.0F, .common_cause = 15.0F, .needs = 40.0F, .rest = 30.0F},
  }};

  VitalsConfig vitals;  ///< Stage 6, decision 105.

  BirthConditionsConfig birth_conditions;  ///< Stage 6, decision 106.
};

}  // namespace core

#endif  // CORE_RESIDENTS_LIFE_CONFIG_H_
