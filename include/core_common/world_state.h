/// @file
/// @brief WorldState — the complete state of the simulated world.
/// @threading PARALLEL_READONLY
/// The double-buffer discipline of the step cycle governs all access: the
/// previous-step WorldState is read-only for every phase, the current one is
/// written by the phase that owns each block (single-threaded phases) or by
/// workers over disjoint row ranges (parallel phases). The step-cycle
/// contract (stage 1, task F2) formalizes the swap; this header only fixes
/// what the state is.
///
/// Design rules of this struct (the state model, manual/52-state-model.md):
///   * WorldState is a value: copying it snapshots the whole world. That is
///     what double buffering, saving and headless comparison runs rely on.
///     Plain data and std::vector only — no pointers, no handles to anything
///     outside the state.
///   * Everything the simulation computes from scratch each step — worker
///     productivity, family standing, "living signals" — is NOT stored here.
///     If it can be derived, it is derived (architecture, §4).
///   * The struct grows by plan stages: stage 2 fills weather, stage 3 adds
///     resident and family tables, stage 4 land and herds, stage 5 labor.
///     Adding a member is the expected, cheap extension (architecture, §7ж);
///     reshaping existing members is the expensive event.

#ifndef CORE_COMMON_WORLD_STATE_H_
#define CORE_COMMON_WORLD_STATE_H_

#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/quantities.h"
#include "core_common/random.h"

namespace core {

/// @brief Game epoch. Reaching the next one is the campaign's arc:
/// 80 residents at start, ~500 by Epoch II, ~1500 by Epoch III.
enum class Epoch : std::uint8_t {
  kOne = 1,
  kTwo = 2,
  kThree = 3,
};

/// @brief Precipitation on the current day.
enum class Precipitation : std::uint8_t {
  kNone = 0,
  kRain,
  kSnow,
};

/// @brief Weather of the current day.
/// Written by the time-and-weather phase (phase 1, single-threaded), frozen
/// for the rest of the step. Daylight bounds the working day (time design,
/// §6); temperature drives heating and the cold metric.
/// @note STUB: the exact field set is confirmed at stage 2 (weather task);
/// these three are the ones other systems already depend on by design.
struct WeatherState {
  float air_temperature_celsius = 10.0f;

  float daylight_hours = 12.0f;

  Precipitation precipitation = Precipitation::kNone;
};

/// @brief The chairman's standing. He is an abstract figure without a body or
/// personal metrics (design: chairman), but his reputations are world state.
/// @note STUB until the relevant systems arrive (phase 1 does not simulate
/// reputation): fields exist so that saves and interfaces are final, values
/// stay at their neutral defaults.
struct ChairmanState {
  /// Reputation with the district committee, 0..100. The chairman's main
  /// metric (metrics design, §3).
  Metric raikom_reputation = 50.0f;

  /// Authority with the villagers, 0..100. Aggregate; decides re-election in
  /// Epoch III (metrics design, §6).
  Metric authority = 50.0f;

  /// Reputation in the shadow world, 0..100. Exists only with the role lines;
  /// stays 0 until that system exists.
  Metric shadow_reputation = 0.0f;
};

/// @brief The yearly delivery plan, reduced to numbers.
/// Phase 1 of the project models the district as "the plan is just a number
/// per resource" (plan, §11): no mechanics, only the target and how much has
/// been delivered against it this economic year.
/// @note STUB: real district mechanics are a later phase; the shape is final.
struct PlanState {
  /// What the district expects this year, by resource. Dense by ResourceId.
  ResourceAmounts due;

  /// What has been delivered against `due` so far this year.
  ResourceAmounts delivered;
};

/// @brief The complete state of the simulated world at one step.
/// Two instances exist at run time — read buffer and write buffer — and swap
/// at the end of each step. A save is this struct serialized; a headless run
/// is this struct advanced 10 000 times and compared.
///
/// Growth plan (do not restructure, only append):
///   stage 3:  StateTable<ResidentId, ...> residents;
///             StateTable<FamilyId, ...> families;
///   stage 4:  StateTable<FieldId, ...> fields;
///             StateTable<UnitId, ...> units;
///             StateTable<HerdId, ...> herds;
///   stage 5+: labor assignments live inside resident rows, not here.
struct WorldState {
  CalendarState calendar;

  WeatherState weather;

  Epoch epoch = Epoch::kOne;

  /// Campaign seed: fixed at world creation, never changes, drives every
  /// derived counter-style random draw. Same seed, same commands — same world.
  std::uint64_t world_seed = 0;

  /// The one sequential RNG of the world (random.h): advanced only in
  /// single-threaded phases; parallel code uses counter hashes instead.
  RngState rng;

  ChairmanState chairman;

  PlanState plan;
};

}  // namespace core

#endif  // CORE_COMMON_WORLD_STATE_H_
