/// @file
/// @brief One home for the question two runs ask: does a plough or a harrow
/// wait with nobody on it while a horse rides for what does not spoil
/// (district_lot — the district's lot, 0.36.18; timber_years — a stand's
/// logs, a dig's load and the lot, 0.36.19). Each kept a copy of its own
/// until 0.37.103, and the copies had drifted: one asked the frost, the other
/// the rain, and both read the placement before the top-up.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHEN TO READ, WHICH IS THE WHOLE POINT (manual/75-logistics.md §10): a day
/// of twenty-four steps ends on the NEXT day's hour 0 — the morning's plan
/// made, production's daily block run after it, the hour-1 top-up not yet.
/// `world` here must be a day's hour 1 or later, and `at_hour_0` the fields
/// of that same day's hour 0. Read at hour 0 a field production opened that
/// dawn stands uncrewed whatever the core does about it.

#ifndef TESTS_RUN_COMMON_WAITING_PLOUGH_H_
#define TESTS_RUN_COMMON_WAITING_PLOUGH_H_

#include <cstdint>
#include <vector>

#include "core_common/ids.h"
#include "core_common/labor_state.h"
#include "core_common/land_state.h"
#include "core_common/rain_stops_work.h"
#include "core_common/world_state.h"

namespace run {

/// @brief Fields in a horse work (ploughing, harrowing) with work left and
///        nobody of that work on them, on a day neither the rain nor the
///        frost stops it.
/// @param at_hour_0, ids_at_hour_0 The fields as the day's hour 0 left them,
///        after production's daily block: a field counts only if it stood in
///        this phase then, so one that changed phase during the day's work —
///        its crew moved it on — is not a plough waiting.
/// @param world The same day, hour 1 or later.
///
/// A ZYAB ON A FROSTY DAY IS NOT A PLOUGH WAITING (0.37.18): the accountant
/// does not offer the autumn furrow below nought, so it stands uncrewed by
/// the rule and a horse on the logs takes nothing from it. Seed 1929, day
/// 285, −4.5 °C: 34 man-days counted before the exemption.
/// CREWED BY ITS OWN WORK: a field may be ploughed and carted at once, and a
/// carter on its load does not plough it (static review of 0.36.18).
inline std::uint32_t HorseFieldsWaiting(const std::vector<core::FieldRow>& at_hour_0,
                                        const std::vector<core::FieldId>& ids_at_hour_0,
                                        const core::WorldState& world) {
  std::uint32_t waiting = 0;
  for (std::uint32_t row = 0; row < world.fields.rows.size(); ++row) {
    const core::FieldRow& field = world.fields.rows[row];
    const bool plough = field.phase == core::FieldPhase::kPlowing;
    const bool harrow = field.phase == core::FieldPhase::kHarrowing;
    if ((!plough && !harrow) || !(field.work_days_remaining > 0.0F)) {
      continue;
    }
    const bool in_that_phase_at_hour_0 =
        row < at_hour_0.size() && row < ids_at_hour_0.size() &&
        ids_at_hour_0[row].value == world.fields.row_ids[row].value &&
        at_hour_0[row].phase == field.phase;
    if (!in_that_phase_at_hour_0) {
      continue;
    }
    const core::WorkKind kind = plough ? core::WorkKind::kPlowing : core::WorkKind::kHarrowing;
    if (core::RainStopsWork(world.weather.precipitation, kind) ||
        core::FrostStopsFieldWork(field, world.weather.air_temperature_celsius)) {
      continue;
    }
    bool crewed = false;
    for (const core::ResidentRow& person : world.residents.rows) {
      crewed = crewed || (person.work.field.value == world.fields.row_ids[row].value &&
                          person.work.kind == kind);
    }
    waiting += crewed ? 0U : 1U;
  }
  return waiting;
}

/// @brief Carters on a horse at a load that does not spoil: a stand's logs, a
///        dig's load, the district's lot.
inline std::uint32_t RidersOfWindowlessCarts(const core::WorldState& world) {
  std::uint32_t carts = 0;
  for (const core::ResidentRow& person : world.residents.rows) {
    const bool timber_load = person.work.stand.value != core::kInvalidEntityIdValue ||
                             person.work.extraction_site.value != core::kInvalidEntityIdValue ||
                             person.work.limit_delivery.value != core::kInvalidEntityIdValue;
    carts +=
        person.work.kind == core::WorkKind::kHauling && person.work.rides_horse != 0 && timber_load
            ? 1U
            : 0U;
  }
  return carts;
}

}  // namespace run

#endif  // TESTS_RUN_COMMON_WAITING_PLOUGH_H_
