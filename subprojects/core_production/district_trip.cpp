// The chairman's trip to the district (district_trip.h).

#include "district_trip.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/chairman_away.h"
#include "core_common/emit_event.h"
#include "core_common/quantities.h"
#include "stock_ops.h"

namespace core {
namespace {

Tick TickAt(SimDay day, std::uint32_t hour) {
  return (static_cast<Tick>(day) * kTicksPerDay) + hour;
}

/// A calendar month of the campaign, comparable across years: the year's
/// twelve and the month.
std::uint32_t MonthIndex(SimDay day) {
  const Date date = DateFromDay(day);
  return (static_cast<std::uint32_t>(date.year) * kMonthsPerYear) +
         static_cast<std::uint32_t>(date.month);
}

void ClearTrip(ChairmanState& chairman) {
  chairman.away_from_tick = 0;
  chairman.away_until_tick = 0;
  chairman.away_summoned = 0;
}

/// The crop a plan position is owed in — the first with that produce and a
/// yield, as the plan's own figure was laid out (district_plan.cpp).
const CropDef* CropOfPosition(const ProductionConfig& config, ResourceId produce) {
  for (const CropDef& crop : config.crops) {
    if (crop.resource.value == produce.value && crop.yield_kg_per_ha > 0.0F) {
      return &crop;
    }
  }
  return nullptr;
}

/// The position ±plan_trade_percent: its grams and, for the milk, the cart's
/// daily share with it.
void MovePosition(const ProductionConfig& config,
                  WorldState& current,
                  ResourceId position,
                  std::int64_t direction) {
  const double factor =
      1.0 + (static_cast<double>(direction) *
             static_cast<double>(config.district_trip.plan_trade_percent) / 100.0);
  Grams& due = current.plan.due[position.value];
  due = static_cast<Grams>(std::llround(static_cast<double>(due) * factor));
  if (position.value == config.milk_resource.value) {
    current.plan.milk_daily_share = static_cast<Grams>(
        std::llround(static_cast<double>(current.plan.milk_daily_share) * factor));
    // THE DEBT IS NOT RESCALED, and that is a choice: it is owed for days
    // already past, at the share those days had. The bargain moves the days
    // to come (the share) and the figure they add up to.
  }
}

/// The position replaced by `crop` on the same hectares: the hectares the
/// figure stood for at the old crop's yield, the tonnes the new crop gives
/// on them (boss seq 206, 4 — «у района площадь, у колхоза урожай»).
bool ReplacePosition(const ProductionConfig& config,
                     WorldState& current,
                     ResourceId position,
                     const CropDef& crop) {
  const CropDef* const was = CropOfPosition(config, position);
  if (was == nullptr || crop.yield_kg_per_ha <= 0.0F || crop.resource.value == kInvalidDefIdValue ||
      crop.resource.value == position.value || !(config.plan_grain_share > 0.0F)) {
    return false;
  }
  const double kg = static_cast<double>(current.plan.due[position.value]) /
                    static_cast<double>(kGramsPerKilogram);
  const double hectares = kg / (static_cast<double>(was->yield_kg_per_ha) *
                                static_cast<double>(config.plan_grain_share));
  const auto grams = static_cast<Grams>(std::llround(
      hectares * static_cast<double>(crop.yield_kg_per_ha) *
      static_cast<double>(config.plan_grain_share) * static_cast<double>(kGramsPerKilogram)));
  current.plan.due[position.value] = 0;
  AddToStock(current.plan.due, crop.resource, grams);
  return true;
}

/// His departure at `away_from_tick`: a blizzard cancels his own trip and
/// moves a summons a day; the mud brings him back the next morning.
void Depart(const ProductionConfig& config, WorldState& current) {
  ChairmanState& chairman = current.chairman;
  const SimDay day = current.calendar.day;
  if (WeatherHoldsDeparture(current)) {
    if (chairman.away_summoned != 0) {
      chairman.summon_day += 1;
      chairman.away_from_tick += kTicksPerDay;
      chairman.away_until_tick += kTicksPerDay;
      EmitEvent(current, EventKind::kSummonPostponed, EventSeverity::kNotable).amount =
          static_cast<std::int64_t>(chairman.summon_day);
    } else {
      ClearTrip(chairman);
      EmitEvent(current, EventKind::kTripCancelled, EventSeverity::kNotable);
    }
    return;
  }
  if (current.weather.mud) {
    chairman.away_until_tick = TickAt(day + 1, config.district_trip.depart_hour);
  }
  if (chairman.away_summoned == 0) {
    chairman.last_trip_day = day + 1;
  }
  EmitEvent(current, EventKind::kTripDeparted, EventSeverity::kInterrupting).amount =
      static_cast<std::int64_t>(chairman.away_until_tick);
}

void Return(const ProductionConfig& config, WorldState& current) {
  ChairmanState& chairman = current.chairman;
  const bool summons_ended = chairman.away_summoned != 0;
  if (summons_ended) {
    chairman.summon_letter_day = 0;
    chairman.summon_day = 0;
    chairman.summon_cause = static_cast<std::uint8_t>(SummonCause::kNone);
  }
  ClearTrip(chairman);
  EmitEvent(current, EventKind::kTripReturned, EventSeverity::kNotable);
  // THE PENCIL THAT WAITED (boss, boss-core-epoch1-2 seq 8): once the
  // summons that stood is over, the deferred one calls him if he is still
  // on the pencil; risen above the line, the mark goes silently. Only a
  // summons ends here — a trip of his own clears no summons, so a pending
  // mark waits for the summons' own return.
  if (summons_ended && chairman.pencil_pending != 0) {
    chairman.pencil_pending = 0;
    if (chairman.raikom_reputation <= config.district_trip.plan_trade_min_reputation) {
      SummonChairman(config, current, SummonCause::kOnThePencil);
    }
  }
}

}  // namespace

OrderRefusal OrderTripToDistrict(const ProductionConfig& config, WorldState& current) {
  ChairmanState& chairman = current.chairman;
  if (chairman.away_from_tick != 0 || chairman.summon_day != 0) {
    return OrderRefusal::kChairmanAway;  // a trip booked or under way, or a summons
  }
  const std::uint32_t depart = config.district_trip.depart_hour;
  const SimDay today = current.calendar.day;
  const SimDay day = HourFromTick(current.calendar.tick) < depart ? today : today + 1;
  if (chairman.last_trip_day != 0 && MonthIndex(chairman.last_trip_day - 1) == MonthIndex(day)) {
    return OrderRefusal::kTripThisMonth;
  }
  chairman.away_from_tick = TickAt(day, depart);
  chairman.away_until_tick = TickAt(day, config.district_trip.return_hour);
  chairman.away_summoned = 0;
  return OrderRefusal::kNone;
}

OrderRefusal OrderTradePlan(const ProductionConfig& config,
                            WorldState& current,
                            const OrderRow& order) {
  if (!ChairmanAway(current)) {
    return OrderRefusal::kNotEligible;  // the bargain is made in the district
  }
  // The year counts from 1 (calendar.h, Date), so 0 stays «never».
  const Date date = current.calendar.date;
  const std::uint16_t year_mark = date.year;
  // From the letter (January; the first morning in the first year) to the
  // end of March — boss seq 210, 213.
  if (current.plan.announced == 0 || date.month > Month::kMarch ||
      current.chairman.plan_traded_year == year_mark) {
    return OrderRefusal::kTradeClosed;
  }
  ChairmanState& chairman = current.chairman;
  const DistrictTripCatalog& trip = config.district_trip;
  if (chairman.raikom_reputation < trip.plan_trade_min_reputation) {
    return OrderRefusal::kReputationTooLow;
  }
  const ResourceId position = order.resource;
  if (position.value >= current.plan.due.size() || current.plan.due[position.value] <= 0) {
    return OrderRefusal::kNoSuchSubject;
  }
  float cost = trip.plan_trade_percent_rep_cost;
  if (order.rotation_year0.value != kInvalidDefIdValue) {
    if (order.rotation_year0.value >= config.crops.size() ||
        !ReplacePosition(config, current, position, config.crops[order.rotation_year0.value])) {
      return OrderRefusal::kNoSuchSubject;
    }
    cost = trip.plan_trade_swap_rep_cost;
  } else {
    MovePosition(config, current, position, order.amount);
  }
  chairman.raikom_reputation = std::max(0.0F, chairman.raikom_reputation - cost);
  chairman.plan_traded_year = year_mark;
  SimEvent& traded = EmitEvent(current, EventKind::kPlanTraded, EventSeverity::kNotable);
  traded.resource = position;
  traded.amount = current.plan.due[position.value];
  return OrderRefusal::kNone;
}

void RunDistrictTrip(const ProductionConfig& config, WorldState& current) {
  ChairmanState& chairman = current.chairman;
  const Tick now = current.calendar.tick;
  const SimDay day = current.calendar.day;
  if (HourFromTick(now) == 0) {
    if (chairman.summon_letter_day != 0 && day == chairman.summon_letter_day) {
      EmitEvent(current, EventKind::kSummonLetter, EventSeverity::kInterrupting).amount =
          static_cast<std::int64_t>(chairman.summon_day);
    }
    // The summons' own morning: a trip of his own booked for today gives way
    // to it and does not count.
    if (chairman.summon_day != 0 && day == chairman.summon_day && chairman.away_summoned == 0) {
      chairman.away_from_tick = TickAt(day, config.district_trip.depart_hour);
      chairman.away_until_tick = TickAt(day, config.district_trip.return_hour);
      chairman.away_summoned = 1;
    }
  }
  if (chairman.away_from_tick != 0 && now == chairman.away_from_tick) {
    Depart(config, current);
  } else if (chairman.away_until_tick != 0 && now == chairman.away_until_tick &&
             now > chairman.away_from_tick) {
    Return(config, current);
  }
}

void SummonChairman(const ProductionConfig& config, WorldState& current, SummonCause cause) {
  ChairmanState& chairman = current.chairman;
  if (chairman.summon_day != 0) {
    return;  // one summons at a time; the next cause waits for its own
  }
  const DistrictTripCatalog& trip = config.district_trip;
  chairman.summon_letter_day = current.calendar.day + trip.summon_letter_delay_days;
  chairman.summon_day = chairman.summon_letter_day + trip.summon_after_letter_days;
  chairman.summon_cause = static_cast<std::uint8_t>(cause);
}

void SummonIfTheYearFailed(const ProductionConfig& config, WorldState& current) {
  if (current.plan.last_verdict == PlanVerdict::kFailed) {
    SummonChairman(config, current, SummonCause::kFailedYear);
  }
}

void SummonOnThePencil(const ProductionConfig& config,
                       const WorldState& previous,
                       WorldState& current) {
  const float line = config.district_trip.plan_trade_min_reputation;
  if (previous.chairman.raikom_reputation > line && current.chairman.raikom_reputation <= line) {
    // Another summons standing: the crossing waits for its return (Return),
    // once — a summons is one at a time (boss seq 8). A PENCIL summons
    // standing already answers it: a plan bargained on the return tick, after
    // Return fired the old mark, would otherwise mark a second in a row.
    // AND ONLY UNDER A SUMMONS THAT STOOD BEFORE THIS TICK (boss seq 10): a
    // summons born this tick was born of the same event that crossed — the
    // seizure with its audit, the verdict with its failed year — and that
    // event is already asked by its own summons.
    if (current.chairman.summon_day != 0) {
      // THE SAME summons as a tick ago — not merely some summons: a return
      // that cleared one and an audit that opened another in one tick
      // would otherwise pass for "stood before" (the static loop).
      const bool stood_before = previous.chairman.summon_day == current.chairman.summon_day &&
                                previous.chairman.summon_cause == current.chairman.summon_cause;
      if (stood_before &&
          current.chairman.summon_cause != static_cast<std::uint8_t>(SummonCause::kOnThePencil)) {
        current.chairman.pencil_pending = 1;
      }
      return;
    }
    SummonChairman(config, current, SummonCause::kOnThePencil);
  }
}

bool AwayToday(const WorldState& world) {
  const ChairmanState& chairman = world.chairman;
  const Tick dawn = TickAt(world.calendar.day, 0);
  const Tick dusk = dawn + kTicksPerDay;
  // 0 IS "NEVER" FOR BOTH HALVES (0.36.23; boss-core-epoch1-resume [54]): the
  // still_away half below always excluded it, this one did not, and on day 0
  // — dawn at tick 0 — the default read as leaving today, and a visit due that
  // day moved to day 1.
  const bool leaves_today = chairman.away_from_tick != 0 && chairman.away_from_tick >= dawn &&
                            chairman.away_from_tick < dusk;
  const bool still_away = chairman.away_from_tick != 0 && chairman.away_from_tick < dawn &&
                          chairman.away_until_tick > dawn;
  const bool summoned_today = chairman.summon_day != 0 && chairman.summon_day == world.calendar.day;
  // A DEPARTURE THE WEATHER HOLDS IS NO DAY AWAY (the static loop of 23
  // September): the day's weather is known at its turn, and a blizzard then
  // cancels his own trip or moves the summons — the visit sent off at 0:00
  // found him home all day. A trip already under way is not held.
  if (WeatherHoldsDeparture(world)) {
    return still_away;
  }
  return leaves_today || still_away || summoned_today;
}

}  // namespace core
