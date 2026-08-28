// Internal to core_residents: the parsed demography configuration.
//
// Values mirror tables/life.csv, tables/demography.csv and
// tables/satisfaction.csv; the defaults here equal the canonical table
// contents so that a table set without these tables (unit tests, early
// runs) behaves like the shipped one. A present-but-malformed table is an
// error, never a silent fallback (parsing in residents_system.cpp).

#ifndef CORE_RESIDENTS_LIFE_CONFIG_H_
#define CORE_RESIDENTS_LIFE_CONFIG_H_

#include <array>
#include <cstdint>

namespace core {

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
};

}  // namespace core

#endif  // CORE_RESIDENTS_LIFE_CONFIG_H_
