/// @file
/// @brief ITimeSystem — the boundary of the time-and-weather subsystem.
/// @threading SINGLE_THREADED
/// The subsystem is phase 1 of the step and runs sequentially on the sim
/// thread; the accessor and the factory are called from the sim thread during
/// wiring. Worker threads never touch this module.
///
/// Subsystem law (state model, manual/52-state-model.md): implementations
/// hold configuration only — every fact about the simulated world lives in
/// WorldState. There is nothing to reset on ISimulation::ResetWorld.
///
/// What the phase does each step: advances tick and day, refreshes the
/// calendar caches (date, weekday, season) and writes the day's weather —
/// temperature and precipitation from the per-season weather table, daylight
/// from the solar curve at the fixed campaign latitude (structural, in
/// code). Weather is a pure function of (world_seed, day): counter-hashed,
/// never drawn from the sequential RNG. Everything else in the step reads
/// calendar and weather as frozen.

#ifndef CORE_TIME_TIME_SYSTEM_H_
#define CORE_TIME_TIME_SYSTEM_H_

#include <memory>

#include "core_sim/step.h"

namespace core {

class ITableSet;  // Defined in core_tables (stage 1, task F5).

/// @brief The time-and-weather subsystem: owner of step slot 1.
class ITimeSystem {
 public:
  virtual ~ITimeSystem() = default;

  /// @brief The implementation of the time-and-weather slot (phase 1).
  /// The reference is valid for the lifetime of the system object; wiring
  /// stores it in StepPhaseSet::time_and_weather.
  virtual ISequentialPhase& TimeAndWeatherPhase() = 0;
};

/// @brief Creates the time subsystem.
/// @param tables Balance tables; non-owning, must outlive the returned
///               object. Reads `weather` (per-season temperature and
///               precipitation parameters); a set without that table gets
///               documented STUB defaults. Daylight needs no table: it
///               follows the solar curve at the fixed campaign latitude —
///               structural, computed in code.
/// @return nullptr when a present weather table is malformed (missing
///         season row or column, non-numeric cell) — logged, never patched
///         over silently.
std::unique_ptr<ITimeSystem> CreateTimeSystem(const ITableSet& tables);

}  // namespace core

#endif  // CORE_TIME_TIME_SYSTEM_H_
