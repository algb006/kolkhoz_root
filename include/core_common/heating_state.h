/// @file
/// @brief #2261 the village's heating state (heating.md): whether the start's houses got their
///        firewood, and the burn's probe tallies (not saved).
/// @threading SINGLE_THREADED
/// Written by the residents' decisions sub-step (core_residents/heating.h), once a day.
#ifndef CORE_COMMON_HEATING_STATE_H_
#define CORE_COMMON_HEATING_STATE_H_

#include <cstdint>

#include "core_common/quantities.h"

namespace core {

/// @brief The heating's world record.
struct HeatingState {
  /// 1 once the start's inhabited houses got heat_start_buffer_m3 (the first day of a game).
  /// SAVED (save 237): a load must not seed them a second time.
  std::uint8_t seeded = 0;

  /// #2261 stage 3b, THE SPLITTING'S DEMAND (written each day by the residents' heating, read
  /// by the firewood yard's recipe the next day): the village's burn over the coming month and
  /// the coming two, m³, at the season means of weather.csv and the heated units standing and
  /// lived in today. SAVED (save 237): the yard's next day reads it.
  float need_1m_m3 = 0.0F;
  float need_2m_m3 = 0.0F;

  /// #2261: FUEL LOGS, grams - logs a fuel felling (kMarkFuelFelling) laid down and the firewood
  /// yard has not split yet. The yard splits up to these whatever the building sites owe; the
  /// logs themselves lie with every other log (a builder may still take them - then the yard
  /// finds fewer than this, and reads the smaller). SAVED (save 237).
  Grams fuel_log_grams = 0;

  // -- THE PROBE'S TALLIES (HEAT2261; cumulative from the game's start, NOT SAVED - a run
  // reads them, a game never does) --
  /// Heated unit-days below the threshold (a day a heated unit wanted fuel).
  std::uint64_t probe_want_unit_days = 0;
  /// Of those, the days its buffer could not cover the whole burn.
  std::uint64_t probe_empty_unit_days = 0;
  /// The same two for houses only.
  std::uint64_t probe_want_house_days = 0;
  std::uint64_t probe_empty_house_days = 0;
  /// m³ burnt, and m³ wanted and not there.
  double probe_burnt_m3 = 0.0;
  double probe_short_m3 = 0.0;
  /// Stage 2's refills: how many, m³ brought, and how many came short of the buffer.
  std::uint64_t probe_refills = 0;
  std::uint64_t probe_refill_short = 0;
  double probe_refill_m3 = 0.0;
  /// Stage 3b: days the splitting worked, and of them days the floor (under a month's burn)
  /// let it take the builders' logs; m³ split.
  std::uint64_t probe_split_days = 0;
  std::uint64_t probe_split_floor_days = 0;
  double probe_split_m3 = 0.0;
  /// Stage 3c: m³ of deadwood brought into the houses.
  double probe_deadwood_m3 = 0.0;
  /// Fuel felling: m³ felled under fuel marks.
  double probe_fuel_felled_m3 = 0.0;
  /// Stage 4 (econ's C1/C2): resident-days on a cold day (outdoor under the threshold), of
  /// them days in «Холодно» and in «Замёрз», and the cold's health hits.
  std::uint64_t probe_cold_season_days = 0;
  std::uint64_t probe_cold_stage_days = 0;
  std::uint64_t probe_frozen_stage_days = 0;
  std::uint64_t probe_health_hits = 0;
};

}  // namespace core

#endif  // CORE_COMMON_HEATING_STATE_H_
