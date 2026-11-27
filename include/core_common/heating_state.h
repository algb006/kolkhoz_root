/// @file
/// @brief #2261 the village's heating state (heating.md): whether the start's houses got their
///        firewood, and the burn's probe tallies (not saved).
/// @threading SINGLE_THREADED
/// Written by the residents' decisions sub-step (core_residents/heating.h), once a day.
#ifndef CORE_COMMON_HEATING_STATE_H_
#define CORE_COMMON_HEATING_STATE_H_

#include <cstdint>

namespace core {

/// @brief The heating's world record.
struct HeatingState {
  /// 1 once the start's inhabited houses got heat_start_buffer_m3 (the first day of a game).
  /// SAVED (save 237): a load must not seed them a second time.
  std::uint8_t seeded = 0;

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
};

}  // namespace core

#endif  // CORE_COMMON_HEATING_STATE_H_
