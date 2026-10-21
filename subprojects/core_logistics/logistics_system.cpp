// The groom's logistics (core_logistics/logistics_system.h). Routing stage B,
// B2: the tasks and their levels; B3: the plan of the day; B5: the re-plan;
// B7: the chairman's doors to the tasks. The alarm (B8) is a STUB here, its
// signature final: it comes with its delivery.

#include "core_logistics/logistics_system.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/calendar.h"
#include "core_common/day_off.h"
#include "core_common/day_window.h"
#include "core_common/emit_event.h"
#include "core_common/event_state.h"
#include "core_common/horse_yard_road.h"
#include "core_common/labor_state.h"
#include "core_common/logistics_rules.h"
#include "core_common/logistics_state.h"
#include "core_common/order_state.h"
#include "core_common/site_supply_floor.h"
#include "core_common/state_table_ops.h"
#include "core_common/work_seam.h"
#include "core_common/world_state.h"
#include "core_log/log.h"
#include "core_tables/required_tables.h"
#include "leg_loads.h"
#include "logistics_config.h"
#include "logistics_plan.h"
#include "logistics_tasks.h"
#include "trip_pricing.h"

namespace core {
namespace {

/// The carts there are against the carts the plan planned: a carter on a horse
/// (a goods cart) or a driver of a people's cart, by id and kind. A carrier on
/// foot is not asked — his chain is his placement's load and the near ones.
bool CartsChanged(const WorldState& world, const GroomPlan& plan);

/// Whether some goods cart of the plan could take a new load at `now`: it
/// stands idle (no leg under way and none ahead of it) or is on an EMPTY leg
/// (a ride or a trip's way back). A cart on its loaded leg, or between a ride
/// that brought it to a load and the loaded leg that leaves it (loading), is
/// held: a re-plan could change nothing of its day (#1056 C4; boss, the
/// #1056 thread after [29]).
bool SomeCartFree(const GroomPlan& plan, Tick now) {
  for (const CartPlan& cart : plan.carts) {
    if (cart.on_foot || cart.people_cart) {
      continue;
    }
    bool held = false;
    for (std::size_t index = 0; index < cart.legs.size(); ++index) {
      const CartLeg& leg = cart.legs[index];
      const bool under_way = leg.depart <= now && now < leg.arrive;
      // Loading: arrived by the previous leg, the loaded leg not yet gone.
      const bool loading =
          leg.loaded != 0 && now < leg.depart && index > 0 && cart.legs[index - 1].arrive <= now;
      if ((under_way && leg.loaded != 0) || loading) {
        held = true;
        break;
      }
    }
    if (!held) {
      return true;
    }
  }
  return false;
}

bool CartsChanged(const WorldState& world, const GroomPlan& plan) {
  std::size_t there = 0;
  for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
    const WorkAssignment& work = world.residents.rows[row].work;
    const bool goods = work.kind == WorkKind::kHauling && work.rides_horse != 0;
    const bool people = TakesThePeoplesCart(work.kind) && work.rides_horse != 0;
    if (!goods && !people) {
      continue;
    }
    ++there;
    const ResidentId driver = world.residents.row_ids[row];
    const bool planned = std::ranges::any_of(plan.carts, [&](const CartPlan& cart) {
      return cart.driver.value == driver.value && !cart.on_foot && cart.people_cart == people;
    });
    if (!planned) {
      return true;
    }
  }
  const auto planned_carts = static_cast<std::size_t>(
      std::ranges::count_if(plan.carts, [](const CartPlan& cart) { return !cart.on_foot; }));
  return planned_carts != there;
}

/// «Дольше часа» (transport §12): the hours of working light a task of level
/// 0 may stand unserved before the lamp lights.
constexpr std::uint16_t kLateAfterHours = 1;

/// Somebody stands on the task's load now.
bool Served(const WorldState& world, const LogisticsTaskRow& task) {
  return std::ranges::any_of(world.residents.rows, [&task](const ResidentRow& person) {
    return WorkServesTask(person.work, task);
  });
}

/// THE LOAD'S CARTING IS DONE FOR TODAY: its seam (work_seam.h, the carting
/// still owed — what the carters drain hour by hour and FollowThePlan leaves
/// when empty) stands at nought. The grams land only at the day's last tick
/// (SettleHauling), so the task stays at its level until then with nobody on
/// it, and nobody is needed. An empty seam is also a load with no room to go
/// to: no carter can serve that either, and its lamps are the store's
/// (kHarvestWaitingOnField, kStoreFull).
/// UNTIL 0.37.194 the lamp's clock asked Served alone and counted those hours:
/// village 1932, year 4, a hay heap carted by 10 o'clock waited «9 hours» to
/// 19 (boss, the logistics thread [125]-[129]: the clock measured the book,
/// not the carting).
/// AND A LOAD THAT IS GONE IS CARTED (0.37.200, stage V1): since the hour's
/// carting reaches the store in its hour, a heap can be carted to nothing at
/// three in the afternoon, and its task stays till tomorrow's hour 0, when
/// SyncTasks takes it off. The seam of a load that is no more has no address
/// (WorkSeamOf answers nothing), and the clock counted every hour after: 141
/// of the 162 lamp-hours of V1's first pair stood over an empty field.
bool CartedToday(const WorldState& world, const LogisticsTaskRow& task) {
  const float* const seam = WorkSeamOf(world, HaulingWorkOf(task));
  return seam == nullptr || !(*seam > 0.0F);
}

/// Whether the lamp's clock runs on `task`: a task of level 0, AND A RICK'S
/// SUPPLY AT ANY LEVEL - the wide alarm, «буферы падают ниже порогов» (#1056
/// C1.2; alarm_state.h, kLogisticsLate): such a task stands only while its
/// rick is below its threshold, so its wait is a buffer under its threshold
/// that the carts do not mend. Not paused, either.
bool LampClockRuns(const LogisticsTaskRow& task) {
  return !task.paused && (task.level == LogisticsLevel::kUrgent ||
                          task.load_kind == LogisticsLoadKind::kRickSupply);
}

/// kLogisticsLate's condition: a task of level 0, not paused, its clock of
/// unserved working light at kLateAfterHours or more (LogisticsTaskRow::
/// unserved_light_hours, kept by SayLateLoads every hour). Until 0.37.192 the
/// clock was the tick it entered level 0, and the lamp lit at night over a
/// load carted all day.
bool WaitsLate(const LogisticsTaskRow& task) {
  return LampClockRuns(task) && task.unserved_light_hours >= kLateAfterHours;
}

void SettleTaskOrder(OrderRow& order, OrderRefusal refusal) {
  order.status = refusal == OrderRefusal::kNone ? OrderStatus::kDone : OrderStatus::kRefused;
  order.refusal = refusal;
}

/// kSetLogisticsLevel (order_state.h): the task takes the level as its own —
/// its base, so the morning's ageing (AgeAndRaise) counts from it and a trip
/// returns the task to it — and as its level now, the ageing from today.
/// Raised to level 0 it is placed ahead of every window at the next
/// placement and its time at level 0 starts now (urgent_since, B8's lamp);
/// the plan is stale and, for level 0, urgent (GroomPlan's two flags — this
/// door is the writer they had none of). A threat the morning reads (a heap
/// under rain, a hungry herd) still raises the task above the player's
/// level: the player's level is a floor for it, not a lid.
/// The range before anything else: a pending row loaded from a save comes
/// past the boundary's shape.
OrderRefusal SetTaskLevel(WorldState& current, const OrderRow& order) {
  if (order.logistics_level >= LogisticsLevel::kLogisticsLevelCount) {
    return OrderRefusal::kRuleForbids;
  }
  const std::uint32_t row = FindRow(current.logistics_tasks, order.logistics_task);
  if (row == kNoRow) {
    return OrderRefusal::kNoSuchSubject;
  }
  LogisticsTaskRow& task = current.logistics_tasks.rows[row];
  // «ВАЖНЕЕ ПОЛЕВЫХ РАБОТ» IS A WORKS' SUPPLY'S ALONE (#734; order_state.h):
  // the switch on any other load is refused, not dropped - a heap's carting
  // has its level for that, and a switch taken and not acted on would be a
  // lever with no rod.
  const bool over_fields = order.enable != 0;
  if (over_fields && task.load_kind != LogisticsLoadKind::kSiteSupply) {
    return OrderRefusal::kRuleForbids;
  }
  if (task.base_level == order.logistics_level && task.level == order.logistics_level &&
      task.over_fields == over_fields) {
    return OrderRefusal::kRuleForbids;
  }
  task.over_fields = over_fields;
  const bool raised_to_urgent =
      order.logistics_level == LogisticsLevel::kUrgent && task.level != LogisticsLevel::kUrgent;
  task.base_level = order.logistics_level;
  task.level = order.logistics_level;
  task.urgent_by = order.logistics_level == LogisticsLevel::kUrgent ? LogisticsUrgentBy::kPlayer
                                                                    : LogisticsUrgentBy::kNone;
  task.aged_from_day = current.calendar.day;
  if (raised_to_urgent) {
    task.urgent_since = current.calendar.tick;
    current.groom_plan.urgent_pending = true;
  }
  current.groom_plan.stale = true;
  return OrderRefusal::kNone;
}

/// kPauseLogisticsTask (order_state.h): `enable` 1 pauses — the plan leaves
/// the task out (OpenTasks) and the placement offers no carting of its load
/// (labor_system.cpp, DropPausedLoads), from the next re-plan and the next
/// placement; a carter on it now finishes the hour's carting and is not sent
/// back. `enable` 0 goes on, its ageing counted from today.
OrderRefusal PauseTask(WorldState& current, const OrderRow& order) {
  if (order.enable > 1) {
    return OrderRefusal::kRuleForbids;
  }
  const std::uint32_t row = FindRow(current.logistics_tasks, order.logistics_task);
  if (row == kNoRow) {
    return OrderRefusal::kNoSuchSubject;
  }
  LogisticsTaskRow& task = current.logistics_tasks.rows[row];
  const bool pause = order.enable != 0;
  if (task.paused == pause) {
    return OrderRefusal::kRuleForbids;
  }
  task.paused = pause;
  if (!pause) {
    task.aged_from_day = current.calendar.day;
  }
  current.groom_plan.stale = true;
  return OrderRefusal::kNone;
}

class LogisticsSystem final : public ILogisticsSystem {
 public:
  explicit LogisticsSystem(LogisticsConfig config) : config_(std::move(config)) {}

  void RunTasks(const WorldState& /*previous*/, WorldState& current) override {
    // ONCE A DAY, AFTER THE MORNING'S PLACEMENT (core_world: the decisions
    // slot calls this after the labour sub-step): hour 0, so the carters
    // placed this morning mark their loads served.
    if (HourFromTick(current.calendar.tick) != 0 || !Stabled(current)) {
      return;
    }
    const SimDay today = current.calendar.day;
    TaskDayCount count;
    SyncTasks(config_, current, today, count);
    MarkServed(current, today, count);
    AgeAndRaise(config_, current, today, current.calendar.tick, count);
  }

  LogisticsTally BuildPlan(WorldState& current) override {
    // THE PLAN OF THE DAY (B3; logistics_plan.h): over the carts the
    // morning's placement and the top-up put on a horse.
    LogisticsTally tally;
    if (!Stabled(current)) {
      GroomPlan none;  // today's, and empty: no groom, no cart
      none.day = current.calendar.day;
      current.groom_plan = none;
      return tally;
    }
    current.groom_plan = BuildGroomPlan(config_, current, tally);
    // AND THE PRICE FROM THE DRAWING (#1056 C3; trip_pricing.h): under the
    // planned model the morning's trips become each cart task's seam - and so
    // do a re-plan's while nothing is carted yet today (Replan, (ii')). A
    // re-plan AFTER the carting began does not price, a deviation from the
    // contract named: it draws from loads already partly carted while the
    // facts are last evening's, so its trips would price what is gone; the
    // last price before sunrise stands to the evening, which writes the facts
    // and the fallback anew.
    PriceTasksFromPlan(config_, current, PriceMode::kWrite);
    // AND THE EVENING (F3 (B); logistics_plan.h): who rides home on which
    // leg, and every morning rider's own way home.
    SeatEveningRiders(config_, current, current.groom_plan, tally);
    return tally;
  }

  bool Replan(WorldState& current) override {
    // THE RE-PLAN BY EVENT (B5; transport §11; boss, the logistics thread
    // [61]-[62]): once a game hour after the plan's own hour, the plan is
    // stale when the carts it planned are not the carts there are — a driver
    // who lost his horse or his work, a cart the hour's top-up or a door gave
    // since; the rest of the day is then planned again from where each cart
    // is (BuildGroomPlan, `earlier`). The tasks themselves change once a day,
    // at hour 0, before the plan.
    //
    // AND WHEN A DOOR OF THE CHAIRMAN'S CHANGED A TASK (B7, ReadTaskOrders):
    // its level or its pause makes the plan stale, and a task raised to
    // level 0 sets `urgent_pending` too — the flag's only writer. It was
    // written by nothing from 0.37.184 to 0.37.187 (found re-reading 184): a
    // load raised to level 0 by the morning is placed by the hour-1 top-up
    // (labor_system.cpp, TopUpDay), and mid-day no task changes level but by
    // this door.
    GroomPlan& plan = current.groom_plan;
    if (plan.day != current.calendar.day || HourFromTick(current.calendar.tick) < 2 ||
        !Stabled(current)) {
      return false;
    }
    // THE DAY's PRICE IS THE PLAN THE DAY CARTS BY (#1056 C3 (ii');
    // core-boss-oats-b-pair [82]-[83]): every re-plan BEFORE the carting
    // begins prices again (below); at the hour it begins the day's columns are
    // booked once, from the plan as it then stands. Until (ii') only the
    // morning's plan priced, and on most harvest mornings the hour-1 top-up
    // gave carts that re-planned the day at hour 2, unpriced.
    const std::uint32_t hour = HourFromTick(current.calendar.tick);
    const std::uint32_t sunrise = SunriseHour(current.weather.daylight_hours);
    if (hour == sunrise) {
      PriceTasksFromPlan(config_, current, PriceMode::kBook);
    }
    plan.stale = plan.stale || CartsChanged(current, plan);
    // AND A HEAP REAPED IN THE DAY (#1056 C4, writer (3)): its task is made
    // now and the plan drawn again with it. Until C4 tasks changed at hour 0
    // alone, and a heap reaped at noon waited for tomorrow's plan. The road
    // index rebuilt in the day (writer (2)) sets `stale` where it is rebuilt
    // (road_laying.cpp). Still one re-plan an hour.
    // Scanned only when a heap was born since the last scan (world_state.h,
    // heap_born): the full scan every re-plan hour cost 1.05 s of the canon's
    // 9.5 s step-clock rise.
    if (current.heap_born) {
      current.heap_born = false;
      // The heap's task is made whatever the carts do (the next plan has it);
      // the day is re-planned for it only while some cart could take it -
      // idle or on an empty leg (SomeCartFree). Every re-plan for a new heap
      // with every cart loading or loaded changed nothing, and cost the
      // slot's ~14 ms (the #1056 thread [29]).
      if (AppendNewHeapTasks(config_, current, current.calendar.day) > 0 &&
          SomeCartFree(plan, current.calendar.tick)) {
        plan.stale = true;
      }
    }
    if (!plan.stale && !plan.urgent_pending) {
      return false;
    }
    LogisticsTally tally;
    const GroomPlan earlier = plan;
    plan = BuildGroomPlan(config_, current, tally, &earlier);
    if (hour < sunrise) {
      PriceTasksFromPlan(config_, current, PriceMode::kWrite);  // nothing carted yet today
    }
    // The evening is re-seated from the new legs (F3 (B)). AN APPROXIMATION,
    // NAMED: a worker's hours away were booked at his first hour with the
    // morning plan's way home; a re-plan moves his end of day, not that book.
    SeatEveningRiders(config_, current, plan, tally);
    return true;
  }

  void ReadTaskOrders(WorldState& current) override {
    // THE CHAIRMAN'S DOORS TO THE TASKS (B7; transport §12, «Вмешательство
    // председателя»; order_state.h): each settles in the step it is read, and
    // each makes the plan stale — re-planned at the next hour (Replan).
    for (OrderRow& order : current.orders.rows) {
      if (order.status != OrderStatus::kPending) {
        continue;
      }
      if (order.kind == OrderKind::kSetLogisticsLevel) {
        SettleTaskOrder(order, SetTaskLevel(current, order));
      } else if (order.kind == OrderKind::kPauseLogisticsTask) {
        SettleTaskOrder(order, PauseTask(current, order));
      }
    }
  }

  void CollectAlarms(const WorldState& state, std::vector<Alarm>& out) const override {
    // «ЛОГИСТИКА НЕ УСПЕВАЕТ» (B8; alarm_state.h, kLogisticsLate).
    // ON A DAY OFF THE MOVE IS THE CHAIRMAN'S: declare the day working
    // (0.37.190; AlarmAdvice::kDeclareDayWorking) — named only while the
    // door would take it today, one question with the door
    // (TodayMayBeDeclaredWorking).
    const AlarmAdvice advice =
        TodayMayBeDeclaredWorking(state) ? AlarmAdvice::kDeclareDayWorking : AlarmAdvice::kNone;
    // AND WHERE THE HORSES ARE (stage В6; registry №310): one breakdown an
    // hour, the same for every late load of it (work_seam.h, HorsesByUse) -
    // ON A WORKING DAY ONLY. On a day off nobody is placed and every horse
    // reads free: the cause is the day off, and its line is the day off and
    // the advice (boss, the В6 thread; transport §12 «Выходной»). Measured
    // before: 125 of 137 sampled late lamps fell on the calendar's Sunday.
    const bool day_off = IsDayOffIn(state, state.calendar.day);
    std::optional<std::array<std::uint16_t, kCartUseCount>> horses;
    for (std::uint32_t row = 0; row < state.logistics_tasks.rows.size(); ++row) {
      const LogisticsTaskRow& task = state.logistics_tasks.rows[row];
      if (!WaitsLate(task)) {
        continue;
      }
      if (!horses) {
        horses = day_off ? std::array<std::uint16_t, kCartUseCount>{}
                         : HorsesByUse(state, DraughtHorsesOf(state, config_.horse_kind));
      }
      Alarm alarm;
      alarm.kind = AlarmKind::kLogisticsLate;
      alarm.logistics_task = state.logistics_tasks.row_ids[row];
      alarm.amount = static_cast<std::int64_t>(task.unserved_light_hours);
      alarm.advice = advice;
      alarm.carts_by_use = *horses;
      out.push_back(alarm);
    }
    CollectSupplyWaits(state, out);
  }

  /// «ПОДВОЗ НА СТРОЙКУ ЖДЁТ» (#734; alarm_state.h, kSiteSupplyWaits): on the
  /// floor's own trigger (core_common/site_supply_floor.h, OnSupplyFloor) and
  /// by its own conditions - a draught horse in the settlement (the floor
  /// gives carters with horses and nothing else), the task not paused. The
  /// advice names the door only while the door would take it: the switch not
  /// set yet (SetTaskLevel refuses a repeat).
  void CollectSupplyWaits(const WorldState& state, std::vector<Alarm>& out) const {
    if (!SettlementHasCarts(state, config_.horse_kind)) {
      return;
    }
    for (std::uint32_t row = 0; row < state.logistics_tasks.rows.size(); ++row) {
      const LogisticsTaskRow& task = state.logistics_tasks.rows[row];
      if (task.load_kind != LogisticsLoadKind::kSiteSupply || task.paused) {
        continue;
      }
      const std::uint32_t unit_row = FindRow(state.units, task.unit);
      // THE MOWING'S DAYS ARE NOT THE WORKS' WAIT (stage В6 part 3; save 165):
      // lit on the lamp's days, not the floor's (site_supply_floor.h).
      if (unit_row == kNoRow ||
          !SupplyWaitLampLit(config_.supply_floor, state.units.rows[unit_row].construction)) {
        continue;
      }
      Alarm alarm;
      alarm.kind = AlarmKind::kSiteSupplyWaits;
      alarm.unit = task.unit;
      alarm.logistics_task = state.logistics_tasks.row_ids[row];
      alarm.amount = SupplyLampDays(state.units.rows[unit_row].construction);
      alarm.lamp = 1;
      alarm.days_to_loss = DaysToLossOf(std::numeric_limits<std::int64_t>::max());
      alarm.advice = task.over_fields ? AlarmAdvice::kNone : AlarmAdvice::kSupplyOverFields;
      out.push_back(alarm);
    }
  }

  // THE FORM OF A LEG'S LOAD (stage В, F2): the form of the resource its
  // task carries (LegResource). A leg carrying several at once — a works'
  // front leg at a store, a district lot, a store being emptied — is drawn by
  // the one of most grams until the mixing (F3) loads them by form.
  CargoForm CargoFormOfLeg(const WorldState& completed,
                           std::uint32_t cart,
                           std::uint32_t leg) const override {
    const GroomPlan& plan = completed.groom_plan;
    if (plan.day != completed.calendar.day || cart >= plan.carts.size() ||
        leg >= plan.carts[cart].legs.size()) {
      return CargoForm::kNone;
    }
    if (plan.carts[cart].people_cart) {
      return CargoForm::kPeople;
    }
    return LegCargoForm(config_, completed, plan.carts[cart].legs[leg]);
  }

  // THE PORTERS ON THE MAP (#671; leg_loads.h).
  void CollectPorterLegs(const WorldState& completed, std::vector<PorterLeg>& out) const override {
    core::CollectPorterLegs(config_, completed, out);
  }

  void SayLateLoads(WorldState& current) const override {
    // THE LAMP'S CLOCK, AN HOUR ON (0.37.192; LogisticsTaskRow::
    // unserved_light_hours): an hour of working light with nobody on a task
    // of level 0 adds one; an hour served, or carted for today (CartedToday;
    // 0.37.194), or the task below level 0 or paused, sets it to nought; a
    // night hour leaves it as it is.
    // AND ITS FIRST HOUR, SAID (B8; event_state.h, kUrgentLoadWaits): the hour
    // the clock reaches kLateAfterHours — once for each wait, a new wait after
    // a served hour said again.
    const std::uint32_t hour = HourFromTick(current.calendar.tick);
    const bool light = hour >= SunriseHour(current.weather.daylight_hours) &&
                       hour < SunsetHour(current.weather.daylight_hours);
    for (std::uint32_t row = 0; row < current.logistics_tasks.rows.size(); ++row) {
      LogisticsTaskRow& task = current.logistics_tasks.rows[row];
      if (!LampClockRuns(task) || Served(current, task) || CartedToday(current, task)) {
        task.unserved_light_hours = 0;
        continue;
      }
      if (!light) {
        continue;
      }
      if (task.unserved_light_hours < std::numeric_limits<std::uint16_t>::max()) {
        ++task.unserved_light_hours;
      }
      // The interrupting word is level 0's alone: a buffer under its
      // threshold lights the lamp and stops no fast-forward (#1056 C1.2).
      if (task.unserved_light_hours != kLateAfterHours || task.level != LogisticsLevel::kUrgent) {
        continue;
      }
      const std::int64_t packed =
          static_cast<std::int64_t>(current.logistics_tasks.row_ids[row].value) |
          (static_cast<std::int64_t>(task.load_kind) << 32);
      SimEvent& event =
          EmitEvent(current, EventKind::kUrgentLoadWaits, EventSeverity::kInterrupting);
      event.amount = packed;
      // AND WHY NOBODY CAME, ON A DAY OFF (task 427, item 3; event_state.h,
      // kDayOffHeldUrgentLoad): the allocator offers no carting that day.
      // The milk leg is herd care, worked on a day off (#1056 C1.1): a day
      // off holds nothing of it.
      if (IsDayOffIn(current, current.calendar.day) &&
          task.load_kind != LogisticsLoadKind::kMilkLeg) {
        SayDayOffHeld(current, current.logistics_tasks.rows[row], packed);
      }
    }
  }

  /// kDayOffHeldUrgentLoad for `task`: where its load waits and what it is.
  void SayDayOffHeld(WorldState& current, const LogisticsTaskRow& task, std::int64_t packed) const {
    SimEvent& held = EmitEvent(current, EventKind::kDayOffHeldUrgentLoad, EventSeverity::kNotable);
    held.amount = packed;
    switch (task.load_kind) {
      case LogisticsLoadKind::kFieldHeap: {
        held.field = task.field;
        const std::uint32_t field_row = FindRow(current.fields, task.field);
        held.resource =
            field_row != kNoRow ? current.fields.rows[field_row].reaped_resource : ResourceId{};
        break;
      }
      case LogisticsLoadKind::kStandLogs:
        held.stand = task.stand;
        held.resource = config_.log_resource;
        break;
      case LogisticsLoadKind::kSiteDig: {
        const std::uint32_t site_row = FindRow(current.extraction_sites, task.extraction_site);
        held.resource =
            site_row != kNoRow ? current.extraction_sites.rows[site_row].resource : ResourceId{};
        break;
      }
      case LogisticsLoadKind::kDistrictLot: {
        const std::uint32_t lot_row = FindRow(current.limit_deliveries, task.limit_delivery);
        held.lot = lot_row != kNoRow ? current.limit_deliveries.rows[lot_row].lot : LimitLotId{};
        break;
      }
      case LogisticsLoadKind::kStoreTransfer:
        held.unit = task.unit;
        break;
      // A works' supply (routing stage В2): its materials wait at their
      // senders, one a leg, and the task is the works' own — the word names
      // the works that waits for them.
      case LogisticsLoadKind::kSiteSupply:
      case LogisticsLoadKind::kRickSupply:  // the rick that waits for its hay
        held.unit = task.unit;
        break;
      case LogisticsLoadKind::kMilkLeg:  // never held by a day off (above)
        held.herd = task.herd;
        break;
      case LogisticsLoadKind::kLogisticsLoadKindCount:
        break;
    }
  }

 private:
  /// @brief Whether the horse yard stands (horse_yard_road.h): until it does
  ///        there is no groom and no logistics at all — the loads go with
  ///        their work orders (transport design §12, «До конного двора нет и
  ///        конюха — логистики нет вовсе»; the Fable review of 11 October
  ///        2026, item 6). The morning's passengers ask the same question
  ///        (cart_passengers.cpp). A yard lost later freezes the tasks where
  ///        they stand: none is born, none ages, none is planned.
  bool Stabled(const WorldState& world) const {
    Vec2 yard;
    return HorseYardPositionOf(world, config_.horse_kind, yard);
  }

  LogisticsConfig config_;
};

}  // namespace

std::unique_ptr<ILogisticsSystem> CreateLogisticsSystem(const ITableSet& tables, StubTables stubs) {
  LogisticsConfig config;
  std::string error;
  // Every table the config reads, refused by name under the strict word
  // (required_tables.h). 0.37.176 took the stubs and asked nothing: a set
  // without logistics.csv assembled on the defaults (unit_core_world, red).
  if (!RequireTables(tables,
                     stubs,
                     "logistics",
                     {"logistics", "resources", "transport", "labor", "world_params", "livestock"},
                     &error) ||
      !ParseLogisticsConfig(tables, config, error)) {
    LogError("logistics: " + error);
    return nullptr;
  }
  return std::make_unique<LogisticsSystem>(std::move(config));
}

}  // namespace core
