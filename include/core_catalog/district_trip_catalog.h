/// @file
/// @brief The chairman's trip to the district, in numbers
/// (econ/manual/proposals/district-trip.md; boss seq 206).
/// @threading PARALLEL_READONLY
/// Parsed once at assembly on the sim thread; read-only afterwards.
///
/// WHY IN THE CATALOGUE, as the district's visits are: production runs the
/// trip, and the runs' chairman that orders it reads the same figures. The
/// rules are production's (core_production/district_trip.h).

#ifndef CORE_CATALOG_DISTRICT_TRIP_CATALOG_H_
#define CORE_CATALOG_DISTRICT_TRIP_CATALOG_H_

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "core_tables/tables.h"

namespace core {

/// @brief The trip's figures. Defaults are boss's and econ's of seq 206,
/// kept for a world with no tables.
struct DistrictTripCatalog {
  /// The hour he leaves (`trip_depart_hour`): 8 — «Время выезда одно — 8:00».
  std::uint8_t depart_hour = 8;

  /// The hour he is back the same day (`trip_return_hour`): 20, STUB — «к
  /// вечеру». In the mud he is back at `depart_hour` the next morning.
  std::uint8_t return_hour = 20;

  /// Days from a summons' cause to the district's letter
  /// (`summon_letter_delay_days`): 1, STUB — «с ближайшей почтой»; the core
  /// knows no post days (boss seq 206, 5).
  std::uint32_t summon_letter_delay_days = 1;

  /// Days from the letter to the summons (`summon_after_letter_days`): 2.
  std::uint32_t summon_after_letter_days = 2;

  /// A bargained position moves by this share (`plan_trade_percent`): 10.
  float plan_trade_percent = 10.0F;

  /// raikom_reputation a ±`plan_trade_percent` bargain costs
  /// (`plan_trade_percent_rep_cost`): 5, STUB (econ's).
  float plan_trade_percent_rep_cost = 5.0F;

  /// raikom_reputation a crop replacement costs (`plan_trade_swap_rep_cost`):
  /// 8, STUB (econ's).
  float plan_trade_swap_rep_cost = 8.0F;

  /// Below this raikom_reputation the district does not bargain at all
  /// (`plan_trade_min_reputation`): 20 — «на карандаше». The same figure
  /// calls him «на ковёр» (SummonCause::kOnThePencil).
  float plan_trade_min_reputation = 20.0F;
};

/// @brief The world_params.csv keys this catalogue reads, for the assembly's
/// declared-readers check (core_world/world.cpp).
std::span<const std::string_view> DistrictTripWorldParamKeys();

/// @brief Reads the knobs of world_params.csv. A missing table or key keeps
/// the default.
/// @return false with `error` naming the key for a value out of range.
bool ParseDistrictTripCatalog(const ITableSet& tables,
                              DistrictTripCatalog& catalog,
                              std::string& error);

}  // namespace core

#endif  // CORE_CATALOG_DISTRICT_TRIP_CATALOG_H_
