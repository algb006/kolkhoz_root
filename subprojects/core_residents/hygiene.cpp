/// @file
/// @brief One day of personal cleanliness (health design §3) — RunHygiene,
/// declared in demography.h, which runs it beside the deaths and the births.
/// @threading SINGLE_THREADED
/// The sequential decisions phase (3), inside the demography day.
///
/// MOVED OUT OF demography.cpp IN 0.37.156 AS IT STOOD, for the file's size
/// (the rule of a thousand lines): the wedding's new household forms grew
/// that file, and the cleanliness is its own matter with its own knobs.

#include <algorithm>
#include <cstdint>

#include "core_common/emit_event.h"
#include "core_common/resident_state.h"
#include "core_common/unit_state.h"
#include "demography.h"

namespace core {
namespace {

/// Whether today's work is the sort that takes the cleanliness off a man.
/// «Ферма, стройка, поле» of health design §3; the tannery it also names has
/// no unit in this core.
bool DirtyWorkToday(WorkKind kind) {
  switch (kind) {
    case WorkKind::kHerdCare:
    case WorkKind::kConstruction:
    case WorkKind::kPlowing:
    case WorkKind::kHarrowing:
    case WorkKind::kSowing:
    case WorkKind::kHarvest:
    case WorkKind::kFelling:
    case WorkKind::kPlanting:  // earth and saplings: «поле» (save 82)
    case WorkKind::kRoadWork:  // a road's bed is «стройка» (delivery 7a)
      return true;
    default:
      return false;
  }
}

}  // namespace

/// One day of personal cleanliness (health design §3). Declared in
/// demography.h — see there for why it stands beside the day rather than
/// inside it.
///
/// IT FALLS BY ITSELF AND RISES FROM ONE THING, which is the stub and is
/// named in the config beside every knob: the design gives four risers — the
/// bathhouse, a yard's own, clean water nearby, soap and a change of linen —
/// and this core has the machinery for none of the other three. So a STANDING
/// bathhouse gives its day to everybody, «кто именно и как часто ходит» being
/// the part that is missing.
///
/// THE EVENT IS A TRANSITION AND NOT A STATE, as event_state.h requires of
/// every event: it is raised on the day a resident CROSSES the threshold
/// downwards, so there is no second field saying "ill" and no flood of the
/// same news every morning. `first_hygiene_disease` is not this core's word —
/// the seam hears every crossing and the host takes the first, the way it
/// takes `first_store_issue` from `distribution_issued`.
void RunHygiene(const LifeConfig& config, WorldState& current) {
  bool bathhouse = false;
  if (config.bathhouse_type.value != kInvalidDefIdValue) {
    bathhouse = std::any_of(
        current.units.rows.begin(), current.units.rows.end(), [&config](const UnitRow& unit) {
          return unit.type.value == config.bathhouse_type.value && unit.level >= 1 &&
                 unit.dead == 0;
        });
  }
  // THE AFTERNOON AND NOT THE DAY'S MEAN, which is the same day the weather
  // calls hot (time_system.cpp: `mean + swing >= hot_afternoon_c`, raising
  // kHotAfternoon). Until 2026-09-17 this line compared the MEAN against a
  // literal 25 and the surcharge never fired once in sixty days across nine
  // villages — measured, not suspected. The swing lives in the state for
  // exactly this: its own note says production used to keep a copy of the
  // season amplitudes to compute the afternoon, "one number with two homes".
  const float afternoon =
      current.weather.air_temperature_celsius + current.weather.temperature_swing_celsius;
  const bool hot = afternoon >= config.hot_afternoon_celsius;
  for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
    ResidentRow& person = current.residents.rows[row];
    const float before = person.hygiene;
    float fall = config.hygiene_fall_per_day;
    if (DirtyWorkToday(person.work.kind)) {
      fall *= config.hygiene_fall_dirty_work_factor;
    }
    if (hot) {
      fall += config.hygiene_fall_heat_extra;
    }
    const float rise = bathhouse ? config.hygiene_rise_bath_per_day : 0.0F;
    person.hygiene = std::clamp(person.hygiene - fall + rise, kMetricMin, kMetricMax);
    // The crossing, and only downwards: a man who was above the line
    // yesterday and is below it today has caught what the filth gives.
    if (before >= config.hygiene_disease_threshold &&
        person.hygiene < config.hygiene_disease_threshold) {
      SimEvent& caught = EmitEvent(current, EventKind::kHygieneDisease, EventSeverity::kNotable);
      caught.resident = current.residents.row_ids[row];
      caught.family = person.family;
    }
  }
}

}  // namespace core
