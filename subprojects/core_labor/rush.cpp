// The avral and the cancelled day off (core_labor/rush.h).

#include "rush.h"

#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/day_off.h"
#include "core_common/family_state.h"
#include "core_common/ids.h"
#include "core_common/land_state.h"
#include "core_common/order_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/work_seam.h"

namespace core {
namespace {

/// How far ahead a cancelled day off is looked for: two weeks. The week has
/// one day off and a holiday can take one, so the next weekly day off always
/// falls inside it.
constexpr SimDay kDayOffSearchDays = 2 * kDaysPerWeek;

/// The step standing on this assignment's work, 0 for none. A field's step
/// counts only while the field still stands in the phase it was declared in
/// and the man does that phase's work: a cart hauling off the field is not
/// the reaping the avral was declared on.
std::uint8_t RushStepOf(const WorldState& world, const WorkAssignment& work) {
  if (work.kind == WorkKind::kConstruction) {
    const std::uint32_t row = FindRow(world.units, work.unit);
    return row == kNoRow ? 0U : world.units.rows[row].construction.rush_step;
  }
  if (work.field.value == kInvalidEntityIdValue) {
    return 0U;
  }
  const std::uint32_t row = FindRow(world.fields, work.field);
  if (row == kNoRow) {
    return 0U;
  }
  const FieldRow& field = world.fields.rows[row];
  const bool on_its_work = field.phase == field.rush_phase && KindOfPhase(field.phase) == work.kind;
  return on_its_work ? field.rush_step : 0U;
}

/// Whether today is the calendar's day off the chairman cancelled — worked.
bool IsCancelledDayOff(const WorldState& world) {
  const SimDay today = world.calendar.day;
  return world.chairman.cancelled_day_off != 0 && world.chairman.cancelled_day_off == today &&
         IsRestDay(today, world.calendar.day_zero_weekday, world.epoch);
}

void Settle(OrderRow& order, OrderRefusal refusal) {
  order.status = refusal == OrderRefusal::kNone ? OrderStatus::kDone : OrderStatus::kRefused;
  order.refusal = refusal;
}

OrderRefusal DeclareRush(WorldState& current, const OrderRow& order) {
  const auto step = static_cast<std::uint8_t>(order.amount);
  if (order.field.value != kInvalidEntityIdValue) {
    const std::uint32_t row = FindRow(current.fields, order.field);
    if (row == kNoRow) {
      return OrderRefusal::kNoSuchSubject;
    }
    FieldRow& field = current.fields.rows[row];
    // Lifting needs no work standing: an avral may be taken back at any
    // moment (unit rules §7), even one the field has already outgrown.
    if (step != 0 &&
        (KindOfPhase(field.phase) == WorkKind::kNone || !(field.work_days_remaining > 0.0F))) {
      return OrderRefusal::kRuleForbids;
    }
    field.rush_step = step;
    field.rush_phase = step == 0 ? FieldPhase::kIdle : field.phase;
    return OrderRefusal::kNone;
  }
  const std::uint32_t row = FindRow(current.units, order.unit);
  if (row == kNoRow) {
    return OrderRefusal::kNoSuchSubject;
  }
  ConstructionState& site = current.units.rows[row].construction;
  if (step != 0 && !(site.labor_days_remaining > 0.0F)) {
    return OrderRefusal::kRuleForbids;
  }
  site.rush_step = step;
  return OrderRefusal::kNone;
}

OrderRefusal CancelDayOff(WorldState& current) {
  const SimDay today = current.calendar.day;
  ChairmanState& chairman = current.chairman;
  if (chairman.cancelled_day_off != 0 && chairman.cancelled_day_off > today) {
    return OrderRefusal::kRuleForbids;  // one order, one day
  }
  for (SimDay day = today + 1; day <= today + kDayOffSearchDays; ++day) {
    const Weekday weekday = WeekdayFromDay(day, current.calendar.day_zero_weekday);
    // THE WEEK'S DAY OFF, AND NEVER A HOLIDAY (time §9: «Праздничный день
    // рабочим объявить нельзя… Игра просто не даёт такой возможности»).
    if (IsDayOff(weekday, current.epoch) &&
        HolidayOn(day, current.calendar.day_zero_weekday, current.epoch) == Holiday::kNone) {
      chairman.cancelled_day_off = day;
      return OrderRefusal::kNone;
    }
  }
  return OrderRefusal::kRuleForbids;
}

}  // namespace

void ReadRushOrders(WorldState& current) {
  for (OrderRow& order : current.orders.rows) {
    if (order.status != OrderStatus::kPending) {
      continue;
    }
    if (order.kind == OrderKind::kDeclareRush) {
      Settle(order, DeclareRush(current, order));
    } else if (order.kind == OrderKind::kCancelDayOff) {
      Settle(order, CancelDayOff(current));
    }
  }
}

float RushBoost(const LaborConfig& config, const WorldState& world, const WorkAssignment& work) {
  return static_cast<float>(RushStepOf(world, work)) * config.rush_step_percent / 100.0F;
}

void StandDownRushes(WorldState& current) {
  for (FieldRow& field : current.fields.rows) {
    if (field.rush_step != 0 && field.phase != field.rush_phase) {
      field.rush_step = 0;
      field.rush_phase = FieldPhase::kIdle;
    }
  }
  if (IsCancelledDayOff(current)) {
    current.chairman.days_off_cancelled_in_a_row =
        static_cast<std::uint8_t>(current.chairman.days_off_cancelled_in_a_row + 1U);
  }
  if (IsFirstDayOfSeason(current.calendar.day)) {
    for (FamilyRow& family : current.families.rows) {
      family.overwork_penalty = 0.0F;
    }
  }
}

void BookRushAtPay(const LaborConfig& config, WorldState& current, ResidentRow& resident) {
  if (!(resident.work.worked_norm_days_today > 0.0F)) {
    return;
  }
  float penalty = config.rush_satisfaction_per_step_day *
                  static_cast<float>(RushStepOf(current, resident.work));
  if (IsCancelledDayOff(current)) {
    const float series_rest = config.day_off_cancel_rest_per_series *
                              static_cast<float>(current.chairman.days_off_cancelled_in_a_row);
    resident.rest = resident.rest > series_rest ? resident.rest - series_rest : 0.0F;
    penalty += config.day_off_cancel_satisfaction;
  }
  const std::uint32_t family_row = FindRow(current.families, resident.family);
  if (family_row != kNoRow && penalty > 0.0F) {
    current.families.rows[family_row].overwork_penalty += penalty;
  }
}

void CloseRushDay(WorldState& current) {
  if (IsCancelledDayOff(current)) {
    current.chairman.cancelled_day_off = 0;  // lived, and spent
  } else if (IsDayOffIn(current, current.calendar.day)) {
    current.chairman.days_off_cancelled_in_a_row = 0;  // a day off taken breaks the series
  }
}

}  // namespace core
