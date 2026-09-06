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
#include <span>
#include <string_view>

#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "core_tables/stub_tables.h"

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

  /// @brief What one day, past or future, will be CALLED and how it will
  /// blow — the two names the seam carries (world_state.h).
  ///
  /// THE FORECAST IS A QUERY, NOT A RECORD, and that follows from the
  /// generator: weather is a pure function of (world_seed, day), so a day
  /// that has not happened is evaluated exactly like one that has. Nothing
  /// is cached and nothing is stored; asking for tomorrow costs what asking
  /// for today costs.
  ///
  /// IT ANSWERS WITH THE NAME AND NOT WITH THE PRECIPITATION since
  /// 2026-09-05, and the reason is the quest layer rather than the panel:
  /// a quest may order the weather of a named day, and an ordered day must
  /// enter the three-day forecast like any other — the forecast is a promise
  /// to the player, and a promise does not distinguish who chose the storm.
  /// A quest orders one of the eight NAMES; "rain" and "thunderstorm" are
  /// two of them and one Precipitation, so precipitation could not carry the
  /// order back out.
  ///
  /// @param world_seed The campaign seed — the same one WorldState carries.
  /// @param day        Any day, including days ahead of the clock.
  /// @return The day's phenomenon and wind band. A system built without a
  ///         weather table answers from its STUB seasons, the same ones the
  ///         phase would use.
  virtual DayForecast WeatherOn(std::uint64_t world_seed, SimDay day) const = 0;
};

/// @brief Creates the time subsystem.
/// @param tables Balance tables; non-owning, must outlive the returned
///               object. Reads `weather` (per-season temperature and
///               precipitation parameters) and `weather_params` (the naming
///               knobs). Daylight needs no table: it follows the solar
///               curve at the fixed campaign latitude — structural,
///               computed in code.
/// @param stubs  WHAT TO DO WHEN THERE IS NO WEATHER TABLE. There is no
///               default value on purpose; see below.
/// @return nullptr when a present weather table is malformed (missing
///         season row or column, non-numeric cell) — logged, never patched
///         over silently — and nullptr when the table is ABSENT and the
///         caller did not say kAllowed.

std::unique_ptr<ITimeSystem> CreateTimeSystem(const ITableSet& tables, StubTables stubs);

/// @brief The world_params.csv keys this module reads.
///
/// EXPOSED SO THAT NOBODY HAS TO JUDGE THE CORE FROM INSIDE A MODULE. The
/// table's `reader` column says which rows the core is expected to read, and
/// checking it needs the union of every module's keys — a module knows only
/// its own. Until 2026-09-06 the check lived in this module and refused a key
/// it simply had not heard of; it now lives at the assembly, which unions
/// these lists (core_catalog/table_value.h).
///
/// @return A view of a static array; valid for the life of the program.
/// @note Wiring-time, sim thread. A pure read of nothing.
std::span<const std::string_view> TimeWorldParamKeys();

}  // namespace core

#endif  // CORE_TIME_TIME_SYSTEM_H_
