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
/// temperature, daylight, precipitation (stage 2 of the plan). Everything
/// else in the step reads calendar and weather as frozen.

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
/// @param tables Balance tables (weather by season); non-owning, must
///               outlive the returned object. Daylight needs no table: it
///               follows the solar curve at the fixed campaign latitude —
///               structural, computed in code (stage 2 of the plan).
/// Implemented in core_time (stage 2 of the plan; a STUB that only advances
/// the calendar arrives with task O0).
std::unique_ptr<ITimeSystem> CreateTimeSystem(const ITableSet& tables);

}  // namespace core

#endif  // CORE_TIME_TIME_SYSTEM_H_
