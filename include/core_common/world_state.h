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
///     resident and family tables, stage 4 land and herds, stage 5 labor,
///     stage 7 the ledger of yearly flows — the one block nothing reads;
///     project phase 2 the order book and the step's event outbox, the
///     two blocks the boundary writes and reads (manual/70-boundary.md).
///     Adding a member is the expected, cheap extension (architecture, §7ж);
///     reshaping existing members is the expensive event.

#ifndef CORE_COMMON_WORLD_STATE_H_
#define CORE_COMMON_WORLD_STATE_H_

#include <array>
#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/event_state.h"
#include "core_common/family_state.h"
#include "core_common/herd_state.h"
#include "core_common/land_state.h"
#include "core_common/ledger_state.h"
#include "core_common/order_state.h"
#include "core_common/quantities.h"
#include "core_common/random.h"
#include "core_common/resident_state.h"
#include "core_common/unit_state.h"

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

  /// 0/1: the kolkhoz horses have been gathered off the private yards into
  /// the kolkhoz yard — the ONE-TIME turn of the start canon (livestock
  /// design §5: the "at the horse" mark is set at the founding, lifted by
  /// the groom, and never comes back, whatever happens to the yard later).
  /// Set by the herd day the morning after a groom is appointed; read by the
  /// labor placement, which stops locking householders to horse work the
  /// moment it is set. A milestone of the campaign, which is why it sits
  /// with the chairman's numbers and not on any herd (task A7).
  std::uint8_t horses_stabled = 0;
};

/// @brief Settlement-wide vital statistics (design decision 105).
/// Life expectancy = 60 + medicine(0..+8) + nutrition(-4..+4) + living
/// conditions(0..+4) + working conditions(-2..+2), factors averaged over
/// 3 years, recomputed once a year. Phase 1 keeps medicine and living
/// conditions at zero and working conditions constant; only nutrition is
/// alive (stage 6). Its derivatives — the aging threshold (LE - 20) and the
/// last-birth median — are computed from the value, never stored.
/// Written only in the sequential demography sub-step.
struct VitalsState {
  /// Current life expectancy, biological years. Starts at the canonical 60.
  float life_expectancy_years = 60.0F;

  /// Mean settlement satiety of each of the last 3 finished years, oldest
  /// first — the nutrition factor's 3-year window. Neutral 70 start: the
  /// mapping knob turns 70 into a zero nutrition contribution.
  std::array<float, 3> satiety_year_means = {70.0F, 70.0F, 70.0F};

  /// Running mean accumulation of the CURRENT year: daily settlement mean
  /// satiety summed, and the number of days summed. Folded into
  /// satiety_year_means at the year turn, then reset.
  float satiety_running_sum = 0.0F;

  std::uint32_t satiety_running_days = 0;
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
///   stage 5+: labor assignments live inside resident rows, not here.
struct WorldState {
  CalendarState calendar;

  WeatherState weather;

  Epoch epoch = Epoch::kOne;

  /// Every person of the settlement (stage 3). Row layout: resident_state.h.
  ResidentTable residents;

  /// Every household (stage 3). Row layout: family_state.h.
  FamilyTable families;

  /// Every field (stage 4). Row layout: land_state.h.
  FieldTable fields;

  /// Every unit (stage 4; construction itself is deferred). unit_state.h.
  UnitTable units;

  /// Every herd (stage 4; sizes static until feeding). herd_state.h.
  HerdTable herds;

  /// Campaign seed: fixed at world creation, never changes, drives every
  /// derived counter-style random draw. Same seed, same commands — same world.
  std::uint64_t world_seed = 0;

  /// The one sequential RNG of the world (random.h): advanced only in
  /// single-threaded phases; parallel code uses counter hashes instead.
  RngState rng;

  ChairmanState chairman;

  PlanState plan;

  /// Life expectancy and its factor window (stage 6, decision 105).
  VitalsState vitals;

  /// The accountant's yearly book of flows (stage 7). Nothing in the
  /// simulation reads it; the run report does. ledger_state.h.
  LedgerState ledger;

  /// The chairman's order book (project phase 2, the boundary): commands
  /// that came across the boundary, waiting for or being executed by the
  /// subsystem whose rules apply. Appended only by the step engine before
  /// phase 1; SAVED — a waiting order survives a load. order_state.h.
  OrderTable orders;

  /// This step's outbox (project phase 2, the boundary): what happened,
  /// for the presentation. Cleared by the step engine after the copy,
  /// appended by sequential code only, NOT SAVED. event_state.h.
  StepEventLog step_events;
};

}  // namespace core

#endif  // CORE_COMMON_WORLD_STATE_H_
