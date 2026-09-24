// The district MTS's column in the simulation (district_limit.h, RunMtsColumn;
// the contract is limit_state.h, MtsColumnState).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "core_common/calendar.h"
#include "core_common/day_off.h"
#include "core_common/emit_event.h"
#include "core_common/haul.h"
#include "core_common/rain_stops_work.h"
#include "core_common/state_table_ops.h"
#include "core_common/work_seam.h"
#include "district_limit.h"
#include "field_haul.h"
#include "field_work.h"
#include "stock_ops.h"

namespace core {

UnitId StandingCamp(const ProductionConfig& config, const WorldState& world) {
  if (config.field_camp_type.value == kInvalidDefIdValue) {
    return UnitId{};
  }
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    const UnitRow& unit = world.units.rows[row];
    if (unit.type.value == config.field_camp_type.value && unit.level >= 1) {
      return world.units.row_ids[row];
    }
  }
  return UnitId{};
}

namespace {

/// Below it a hectare's remainder is float dust, not land left to work.
constexpr float kHectareDust = 1.0e-3F;

/// Each spring phase opens the next at once when its work is nothing: three
/// steps take a field from the plough to the seed in the ground.
constexpr int kSpringChainSteps = 3;

bool IsSpringLot(const LimitCatalog& limit, LimitLotId lot) {
  return lot.value != kInvalidDefIdValue && lot.value == limit.mts_spring_lot.value;
}

/// The field is in the part of its year the column's season works.
bool InSeasonChain(const FieldRow& field, bool spring) {
  if (field.kind != LandKind::kArable) {
    return false;  // the column works the arable; the meadow is mown by hand
  }
  if (spring) {
    return field.phase == FieldPhase::kPlowing || field.phase == FieldPhase::kHarrowing ||
           field.phase == FieldPhase::kSowing;
  }
  return field.phase == FieldPhase::kHarvest;
}

bool MonthInWindow(std::uint8_t month, std::uint8_t from, std::uint8_t to) {
  return month >= from && month <= to;
}

/// Carries `share` of the load lying on the field to the stores, and writes
/// the rest's price back into the carrying seam as SettleHauling would, so
/// the evening's settlement counts no carrier for what the column moved.
void CarryShare(const ProductionConfig& config, WorldState& current, FieldRow& field, float share) {
  if (field.reaped_grams <= 0 || field.reaped_resource.value == kInvalidDefIdValue) {
    return;
  }
  const Grams offered =
      GramsFromFloat(static_cast<float>(field.reaped_grams) * std::clamp(share, 0.0F, 1.0F));
  const Grams moved = DeliverToStores(current, config, field.reaped_resource, offered);
  field.reaped_grams -= moved;
  if (field.reaped_grams <= 0) {
    field.reaped_grams = 0;
    field.reaped_resource = ResourceId{};
    field.haul_days_remaining = 0.0F;
  } else {
    const Grams room = ReceivableRoom(config, current, field.reaped_resource);
    field.haul_days_remaining = HaulDaysFor(room < field.reaped_grams ? room : field.reaped_grams,
                                            FieldHaulRate(config, current, field),
                                            config.standard_day_hours);
  }
  field.haul_days_written = field.haul_days_remaining;
}

/// The crew owes only the hectares the column has not worked: the current
/// phase's demand is held at that share of the whole phase.
void HoldCrewShare(const ProductionConfig& config, const WorldState& current, FieldRow& field) {
  const float worked = field.area_ga > 0.0F ? current.mts_column.field_ha / field.area_ga : 1.0F;
  const float owed =
      std::clamp(1.0F - worked, 0.0F, 1.0F) * PhaseWorkDays(config, current, field, field.phase);
  field.work_days_remaining = std::min(field.work_days_remaining, owed);
}

void ForgetField(MtsColumnState& column) {
  column.field = FieldId{};
  column.field_ha = 0.0F;
}

/// Every tick: the field the column has begun keeps its crew to the share the
/// column left. A field that has left the season's work is forgotten; in the
/// autumn the column, while it is still here, carries its share of the load.
void KeepColumnField(const ProductionConfig& config, WorldState& current) {
  MtsColumnState& column = current.mts_column;
  if (column.field.value == kInvalidEntityIdValue) {
    return;
  }
  const std::uint32_t row = FindRow(current.fields, column.field);
  if (row == kNoRow) {
    ForgetField(column);
    return;
  }
  FieldRow& field = current.fields.rows[row];
  const bool spring = IsSpringLot(config.limit, column.lot);
  if (InSeasonChain(field, spring)) {
    // NOT IN THE RAIN for a phase the rain stops: holding the crew to the
    // column's share would zero a sowing the drill has not done, and the seed
    // would go in at the first dry hour as if it had (FinishColumnField).
    if (!RainStopsWork(current.weather.precipitation, KindOfPhase(field.phase))) {
      HoldCrewShare(config, current, field);
    }
    return;
  }
  if (!spring && column.phase == MtsColumnPhase::kWorking && field.area_ga > 0.0F) {
    CarryShare(config, current, field, column.field_ha / field.area_ga);
  }
  ForgetField(column);
}

/// The column has worked every hectare of the field: spring puts the seed in
/// as far as the sowing term allows, autumn reaps and carries it all.
/// @return true when the rain stopped the chain at a phase it stops — the
///         drill at the sowing — so that the caller keeps the field.
bool FinishColumnField(const ProductionConfig& config,
                       WorldState& current,
                       FieldRow& field,
                       bool spring) {
  // THE DRILL STOPS IN THE RAIN, not only the seed (boss, seq 12: «сев
  // стоит, кто бы его ни вёл»; rain_stops_work.h). The chain halts at the
  // sowing it has just opened, the sowing stays owed, and the caller KEEPS
  // the field: its hectares are worked, so the next dry day finishes it with
  // none, and the queue never takes it up afresh. Twice before this shape:
  // halted and FORGOTTEN, the queue came back and booked the hectares again
  // (the static loop of 23 September); zeroed and waiting for the seed, the
  // drill had done its work in the rain on a field whose sowing opened that
  // day and refused it on one that was already sowing.
  for (int step = 0; step < kSpringChainSteps && InSeasonChain(field, spring); ++step) {
    if (RainStopsWork(current.weather.precipitation, KindOfPhase(field.phase))) {
      return true;
    }
    const FieldPhase before = field.phase;
    field.work_days_remaining = 0.0F;
    AdvanceFinishedField(config, current, field);
    if (field.phase == before) {
      // A harrowed field before its sowing term waits, as a crew's would; the
      // seed then goes in by hand (STUB: the column's sowing is not banked).
      break;
    }
  }
  if (InSeasonChain(field, spring)) {
    field.work_days_remaining = 0.0F;  // whatever opened last, the column did
  }
  if (!spring) {
    CarryShare(config, current, field, 1.0F);
  }
  return false;
}

/// The next field by the brigade's queue: of the fields owed the season's
/// work today, the nearest the camp. STUB: the queue is distance alone — the
/// windows and deadlines of the brigade's ordering are labor's, and the
/// column does not read them (boss, parcel 449, "ближайшие к стану").
std::uint32_t NextColumnField(const WorldState& current, Vec2 camp, bool spring) {
  std::uint32_t best = kNoRow;
  float best_distance = std::numeric_limits<float>::max();
  for (std::uint32_t row = 0; row < current.fields.rows.size(); ++row) {
    const FieldRow& field = current.fields.rows[row];
    if (!InSeasonChain(field, spring) || field.work_days_remaining <= 0.0F ||
        field.area_ga <= 0.0F) {
      continue;
    }
    const float dx = field.center.x - camp.x;
    const float dy = field.center.y - camp.y;
    const float distance = (dx * dx) + (dy * dy);
    if (distance < best_distance) {
      best_distance = distance;
      best = row;
    }
  }
  return best;
}

void WorkColumnDay(const ProductionConfig& config, WorldState& current) {
  MtsColumnState& column = current.mts_column;
  const bool spring = IsSpringLot(config.limit, column.lot);
  const std::uint32_t camp_row = FindRow(current.units, column.camp);
  const Vec2 camp = camp_row != kNoRow ? current.units.rows[camp_row].position : Vec2{};
  float budget = std::min(config.limit.mts_column_ha_per_work_day,
                          config.limit.mts_column_ha_limit - column.worked_ha);
  while (budget > kHectareDust) {
    std::uint32_t row = column.field.value != kInvalidEntityIdValue
                            ? FindRow(current.fields, column.field)
                            : kNoRow;
    if (row != kNoRow && (!InSeasonChain(current.fields.rows[row], spring) ||
                          current.fields.rows[row].work_days_remaining <= 0.0F)) {
      row = kNoRow;  // begun, but nothing the column can do on it today
    }
    if (row == kNoRow) {
      row = NextColumnField(current, camp, spring);
      if (row == kNoRow) {
        return;  // no field is owed the season's work today
      }
      column.field = current.fields.row_ids[row];
      column.field_ha = 0.0F;
    }
    FieldRow& field = current.fields.rows[row];
    // RAIN STOPS THE COMBINE AND THE DRILL (farming design §5, «Дождь
    // останавливает комбайн, как и косца»; rain_stops_work.h), and the
    // column's day ends at the first field whose work the rain stops. Not
    // "skip it and take the next": the next field is chosen by the same
    // queue and would be this one again, with its hectares counted from
    // nought. The ploughs of the spring lose the rest of that day with it —
    // a named simplification, the column's day being one budget.
    if (RainStopsWork(current.weather.precipitation, KindOfPhase(field.phase))) {
      return;
    }
    const float hectares = std::min(budget, field.area_ga - column.field_ha);
    column.field_ha += hectares;
    column.worked_ha += hectares;
    budget -= hectares;
    if (column.field_ha >= field.area_ga - kHectareDust) {
      if (FinishColumnField(config, current, field, spring)) {
        return;  // the rain stopped the drill: the field is kept, the day is over
      }
      ForgetField(column);
    } else {
      HoldCrewShare(config, current, field);
    }
  }
}

void LeaveColumn(WorldState& current) {
  current.mts_column.phase = MtsColumnPhase::kGone;
  // kNotable, AS THE CONTRACT SAYS IT IS. All three of the column's events
  // are documented kNotable in event_state.h and all three were emitted at
  // the default kRoutine — the argument was simply left off, and the default
  // is the quietest severity there is. A severity is not decoration: it is
  // what decides whether the player is told at all, so an event announcing
  // that the district's tractors came, left or never came must not arrive in
  // the world as routine.
  SimEvent& left = EmitEvent(current, EventKind::kMtsColumnLeft, EventSeverity::kNotable);
  left.unit = current.mts_column.camp;
  left.amount = std::llround(current.mts_column.worked_ha);
}

}  // namespace

void RunMtsColumn(const ProductionConfig& config, WorldState& current) {
  KeepColumnField(config, current);
  MtsColumnState& column = current.mts_column;
  if (HourFromTick(current.calendar.tick) + 1U < kTicksPerDay) {
    return;  // the column's day is settled at its last tick, after the crews
  }
  const bool spring = IsSpringLot(config.limit, column.lot);
  const std::uint8_t from =
      spring ? config.limit.mts_spring_from_month : config.limit.mts_autumn_from_month;
  const std::uint8_t to =
      spring ? config.limit.mts_spring_to_month : config.limit.mts_autumn_to_month;
  const auto month = static_cast<std::uint8_t>(current.calendar.date.month);
  if (column.phase == MtsColumnPhase::kOnTheRoad) {
    if (month > to) {
      // The window closed with the column still out: no camp stood for it.
      column.phase = MtsColumnPhase::kNotArrived;
      EmitEvent(current, EventKind::kMtsColumnNotArrived, EventSeverity::kNotable);
      return;
    }
    if (current.calendar.day < column.arrive_day || !MonthInWindow(month, from, to)) {
      return;
    }
    const UnitId camp = StandingCamp(config, current);
    if (camp.value == kInvalidEntityIdValue) {
      return;  // waits at the district for a camp until the window's end
    }
    column.phase = MtsColumnPhase::kWorking;
    column.camp = camp;
    SimEvent& arrived = EmitEvent(current, EventKind::kMtsColumnArrived, EventSeverity::kNotable);
    arrived.unit = camp;
    return;
  }
  if (column.phase != MtsColumnPhase::kWorking) {
    return;
  }
  if (!MonthInWindow(month, from, to)) {
    LeaveColumn(current);
    return;
  }
  if (!IsDayOffIn(current, current.calendar.day)) {
    WorkColumnDay(config, current);
  }
  if (column.worked_ha >= config.limit.mts_column_ha_limit - kHectareDust) {
    LeaveColumn(current);
  }
}

}  // namespace core
