/// @file
/// @brief #2261 residents' cold and firewood (heating.md; needs.md §1; econ's
///        residents-cold-2261-2026-11-27, boss's design.db rows of 27 Nov 2026, sort 410): the
///        numbers of the burn, the units' fuel buffer and its refill, the indoor temperature, the
///        residents' cold metric, the deadwood and the cooking - and the reader of them.
/// @threading SINGLE_THREADED
/// Read once at assembly; the config is copied into the residents system.
///
/// STAGE 0 (this file): THE READER ONLY. The core reads the 38 world_params rows and
/// unit_types' heat_size_factor so the table export can land (the assembly refuses a `core` row
/// nobody reads); nothing burns, nobody gets cold yet - ResidentRow::cold stays the STUB of
/// residents_system.cpp until stage 4. Build order (boss): burn → buffer/refill → supply → cold.
#ifndef CORE_RESIDENTS_HEATING_H_
#define CORE_RESIDENTS_HEATING_H_

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_tables/tables.h"

namespace core {

/// @brief The heating's numbers (world_params, all econ's STUB; units in each comment).
struct HeatingConfig {
  // -- the burn (heating.md §3) --
  float heat_threshold_c = 10.0F;        ///< no heating above it, °C
  float burn_m3_per_degree_day = 0.04F;  ///< a level-1 old house, not insulated
  float burn_dilapidated_factor = 1.4F;  ///< wear at or above dilapidated_wear_pct
  float burn_dugout_factor = 1.4F;       ///< STUB (the dugout's type)
  float burn_level_factor = 0.85F;       ///< each level above the first
  float burn_insulated_factor = 0.75F;   ///< UnitRow::insulated
  float coal_m3_per_t = 4.0F;            ///< firewood m³ a tonne of coal stands for
  float dilapidated_wear_pct = 60.0F;    ///< the wear that makes a house dilapidated
  // -- the buffer at each heated unit (§1) --
  float heat_buffer_m3 = 8.0F;
  float heat_refill_below_m3 = 3.0F;
  float heat_start_buffer_m3 = 2.0F;
  float heat_empty_output_penalty_pct = 25.0F;  ///< a cold unit's work, %
  // -- the indoor temperature (needs.md §1) --
  float heat_indoor_warm_offset_c = 25.0F;  ///< heated: outdoor + this, capped
  float heat_indoor_warm_cap_c = 20.0F;
  float heat_indoor_cold_offset_c = 5.0F;  ///< not heated: outdoor + this
  float heat_close_below_c = 15.0F;        ///< school, kindergarten, police post close
  // -- the residents' cold metric (needs.md §1; econ's smallest form) --
  float cold_scale_max = 100.0F;
  float cold_gain_night_home = 8.0F;          ///< a night in a cold home
  float cold_gain_hour_per_10c_frost = 1.0F;  ///< an hour outdoors per 10 °C of frost
  float cold_loss_night_warm = 20.0F;         ///< a night in a warm home
  float cold_cold_at = 30.0F;                 ///< «Холодно»
  float cold_frozen_at = 70.0F;               ///< «Замёрз»
  float cold_work_penalty_pct = 15.0F;
  float cold_mood_penalty = 5.0F;
  float cold_sickness_pct_day = 0.5F;
  float cold_sickness_frozen_pct_day = 2.0F;
  float cold_hardy_offset = 15.0F;
  float cold_weak_offset = -15.0F;
  float cold_era1_offset = 10.0F;
  float cold_clothes_both_factor = 0.5F;
  float cold_clothes_one_factor = 0.75F;
  float cold_bath_relief = 15.0F;
  float cold_hot_meal_relief = 3.0F;
  // -- the deadwood and the cooking (§2, §4) --
  float deadwood_m3_per_trip = 0.1F;
  float deadwood_full_within_m = 1000.0F;
  float deadwood_zero_at_m = 3000.0F;
  float cook_home_m3_per_resident_day = 0.005F;
  float cook_meal_m3 = 0.002F;
  /// unit_types.csv `heat_size_factor` by type row (empty → 1.0): a non-house heated type burns
  /// this many houses' worth.
  std::vector<float> heat_size_factor;
};

/// @brief The world_params keys this file reads, for the assembly's declared-readers check.
std::span<const std::string_view> HeatingWorldParamKeys();

/// @brief Reads the 38 knobs (each optional, its default kept) and unit_types' heat_size_factor.
/// @return false with `error` naming the key for a value out of its range.
bool ParseHeatingConfig(const ITableSet& tables, HeatingConfig& config, std::string& error);

}  // namespace core

#endif  // CORE_RESIDENTS_HEATING_H_
