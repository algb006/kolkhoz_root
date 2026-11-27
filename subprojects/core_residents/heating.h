/// @file
/// @brief #2261 residents' cold and firewood (heating.md; needs.md §1; econ's
///        residents-cold-2261-2026-11-27, boss's design.db rows of 27 Nov 2026, sort 410): the
///        numbers of the burn, the units' fuel buffer and its refill, the indoor temperature, the
///        residents' cold metric, the deadwood and the cooking - and the reader of them.
/// @threading SINGLE_THREADED
/// Read once at assembly; the config is copied into the residents system.
///
/// STAGE 0: the reader of the 42 world_params rows and unit_types' heat_size_factor /
/// has_heating. STAGE 1: the burn from each heated unit's buffer (RunHeatingBurn) - nobody gets
/// cold yet, ResidentRow::cold stays the STUB of residents_system.cpp until stage 4. Build order
/// (boss): burn → buffer/refill → supply → cold.
#ifndef CORE_RESIDENTS_HEATING_H_
#define CORE_RESIDENTS_HEATING_H_

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_tables/tables.h"

namespace core {

struct UnitRow;
struct WorldState;

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
  /// The chance of falling ill on reaching «Замёрз», once an episode (econ: replaces the daily
  /// frozen rate), and the sickness's split among the cold diseases.
  float cold_frozen_sick_chance_pct = 90.0F;
  float cold_sick_chill_pct = 50.0F;
  float cold_sick_flu_pct = 35.0F;
  float cold_sick_angina_pct = 15.0F;
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
  /// The bath day's extra burn at the bathhouse, m³.
  float bath_day_extra_m3 = 0.4F;
  /// unit_types.csv `heat_size_factor` by type row (empty → 1.0): a non-house heated type burns
  /// this many houses' worth.
  std::vector<float> heat_size_factor;
  /// unit_types.csv `has_heating` by type row (1 = a stove stands in it).
  std::vector<std::uint8_t> has_heating;
  /// unit_types.csv `class` == housing by type row (a dwelling burns only while inhabited).
  std::vector<std::uint8_t> is_housing;
};

/// @brief One heated unit's burn today, m³ (heating.md §3): the degrees below the threshold ×
///        the base norm × the size factor × dilapidated (wear ≥ dilapidated_wear_pct) × the
///        level's factor per level above the first × insulated. 0 at or above the threshold.
/// @note The dugout's ×1.4 is not applied: no dugout type exists (STUB).
float HeatingBurnToday(const HeatingConfig& config, const UnitRow& unit, float outdoor_mean_c);

/// @brief #2261 stage 1, once a day: the start's inhabited houses get heat_start_buffer_m3 on the
///        first call (HeatingState::seeded), then every standing heated unit burns its day from
///        its buffer (UnitRow::heat_buffer_m3), never below 0, and the probe tallies count the
///        wanted and uncovered days.
/// @note A dwelling (class housing) burns only while some family names it its house; any other
///       heated unit burns while it stands (staffing joins with stage 2's refill). Nothing refills,
///       nothing reads the buffer yet - the world moves by the buffer alone.
void RunHeatingBurn(const HeatingConfig& config, WorldState& world);

/// @brief The world_params keys this file reads, for the assembly's declared-readers check.
std::span<const std::string_view> HeatingWorldParamKeys();

/// @brief Reads the 42 knobs (each optional, its default kept) and unit_types' heat_size_factor.
/// @return false with `error` naming the key for a value out of its range.
bool ParseHeatingConfig(const ITableSet& tables, HeatingConfig& config, std::string& error);

}  // namespace core

#endif  // CORE_RESIDENTS_HEATING_H_
