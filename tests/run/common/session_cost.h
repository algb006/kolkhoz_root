// The game's cost of a day, measured THROUGH THE RELEASE'S DOOR (#700, boss 12 Oct 2026: «an
// instrument enters through the same door as the release»). thirty_years drives the simulation
// without a session, and the session refreshes the alarms, the porters, the stock lights and the
// forecast after EVERY step: at year 27 of the canon construction's alarms alone cost ~0.13 s a
// collection, twenty-four a day in the game - a cost class no run saw. Here a session is built over
// a copy of a world and stepped whole game days, each timed.
#ifndef KOLKHOZ_TESTS_RUN_COMMON_SESSION_COST_H_
#define KOLKHOZ_TESTS_RUN_COMMON_SESSION_COST_H_

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core_boundary/session.h"
#include "core_common/calendar.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"
#include "table_override.h"

namespace run {

struct SessionDayCost {
  /// False when the session could not be built - then nothing was measured.
  bool measured = false;
  /// The costliest of the days stepped, seconds of wall time for its 24 session steps.
  double worst_day_seconds = 0.0;
  /// Their mean.
  double mean_day_seconds = 0.0;
  /// THE REFRESH BY ITS PARTS (7 Nov 2026: #1573's session day cost 2.20 s
  /// against main's 1.68 s with the steps alike, and core's first named cause
  /// was wrong): each of the session's collections asked a day's 24 times
  /// over the state as given, seconds; and the alarm kinds lit on it.
  double alarms_seconds = 0.0;
  double porters_seconds = 0.0;
  double stock_lights_seconds = 0.0;
  double weather_seconds = 0.0;
  std::string alarm_kinds;
  /// The alarms' 24 collections over the SAME state with #1573's three rows
  /// set off (table_override.h), and how many of the rows the tables had.
  double alarms_rows_off_seconds = 0.0;
  std::size_t rows_overridden = 0;
};

/// @brief Steps `days` game days of a session over a copy of `state` and times each.
inline SessionDayCost SessionDaySeconds(const core::ITableSet& tables,
                                        std::uint64_t seed,
                                        const core::WorldState& state,
                                        std::uint32_t days) {
  SessionDayCost cost;
  core::StandardSimulationConfig config;
  config.tables = &tables;
  config.world_seed = seed;
  config.worker_count = 1;
  config.stub_tables = core::StubTables::kRefused;
  std::unique_ptr<core::ISimulation> simulation = core::CreateStandardSimulation(config);
  if (simulation == nullptr) {
    return cost;
  }
  simulation->ResetWorld(state);
  {
    const auto timed = [](auto&& collect) {
      const auto began = std::chrono::steady_clock::now();
      for (std::uint32_t tick = 0; tick < core::kTicksPerDay; ++tick) {
        collect();
      }
      return std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    };
    std::vector<core::Alarm> alarms;
    cost.alarms_seconds = timed([&] {
      alarms.clear();
      simulation->CollectAlarms(alarms);
    });
    std::vector<core::PorterLeg> legs;
    cost.porters_seconds = timed([&] {
      legs.clear();
      simulation->CollectPorterLegs(legs);
    });
    std::vector<core::StockForecast> lights;
    cost.stock_lights_seconds = timed([&] {
      lights.clear();
      simulation->CollectStockForecast(lights);
    });
    std::array<core::DayForecast, 3> weather{};
    cost.weather_seconds = timed([&] { simulation->CollectWeatherForecast(weather); });
    for (const core::Alarm& alarm : alarms) {
      cost.alarm_kinds +=
          (cost.alarm_kinds.empty() ? "" : ",") + std::to_string(static_cast<int>(alarm.kind));
    }
    // THE SAME WORLD WITH #1573's ROWS OFF (the ladder before #1573): the
    // world's cost and the code's told apart. A tree without the rows
    // overrides none and says so.
    const OverriddenTableSet rows_off(tables,
                                      "farming",
                                      {{"hay_feed_before_heads_points", "0"},
                                       {"hay_handover_min_short_days", "0"},
                                       {"hay_handover_dose_share", "1.0"}});
    cost.rows_overridden = rows_off.Overridden();
    core::StandardSimulationConfig off_config = config;
    off_config.tables = &rows_off;
    std::unique_ptr<core::ISimulation> off = core::CreateStandardSimulation(off_config);
    if (off != nullptr) {
      off->ResetWorld(state);
      cost.alarms_rows_off_seconds = timed([&] {
        alarms.clear();
        off->CollectAlarms(alarms);
      });
    }
  }
  core::SessionConfig session_config;
  session_config.tables = &tables;
  session_config.simulation = std::move(simulation);
  const std::unique_ptr<core::ISession> session = core::CreateSession(std::move(session_config));
  if (session == nullptr || days == 0) {
    return cost;
  }
  double sum = 0.0;
  for (std::uint32_t day = 0; day < days; ++day) {
    const auto began = std::chrono::steady_clock::now();
    for (std::uint32_t tick = 0; tick < core::kTicksPerDay; ++tick) {
      session->AdvanceStep();
    }
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    sum += seconds;
    cost.worst_day_seconds = seconds > cost.worst_day_seconds ? seconds : cost.worst_day_seconds;
  }
  cost.mean_day_seconds = sum / static_cast<double>(days);
  cost.measured = true;
  return cost;
}

}  // namespace run

#endif  // KOLKHOZ_TESTS_RUN_COMMON_SESSION_COST_H_
