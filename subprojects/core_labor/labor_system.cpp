// Implementation of the core_labor boundary
// (include/core_labor/labor_system.h). Stage 5: the working day itself —
// the morning placement, the hourly grind inside the daylight window, the
// fatigue walk-off and the day's close with trudodni.
//
// The day, in the order it happens:
//   hour 0        the year's account burns on New Year, barn care is
//                 refilled, jobs and workers are collected and the
//                 accountant places them (assignment.cpp);
//   every hour    whoever is inside his own window (daylight minus his
//                 road, both ends) delivers norm-days into the job's seam
//                 and loses rest; below the critical rest he goes home and
//                 is paid for what he did;
//   hour 23       the rest are paid, rest recovers, assignments are
//                 cleared. (Household hours moved to core_residents with
//                 the plot factors — stage 6, task O2.)
//
// Nothing here calls production: the two seams in the state carry the whole
// contract (FieldRow::work_days_remaining, HerdRow::care_days_remaining —
// manual/65-labor-model.md §2).

#include "core_labor/labor_system.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "assignment.h"
#include "cart_passengers.h"
#include "core_common/alarm_state.h"
#include "core_common/away_in_district.h"
#include "core_common/calendar.h"
#include "core_common/cold_housing.h"
#include "core_common/day_off.h"
#include "core_common/emit_event.h"
#include "core_common/event_state.h"
#include "core_common/family_state.h"
#include "core_common/geometry.h"
#include "core_common/haul.h"
#include "core_common/hauled_book.h"
#include "core_common/herd_state.h"
#include "core_common/home_reach.h"
#include "core_common/horse_yard_road.h"
#include "core_common/ids.h"
#include "core_common/labor_state.h"
#include "core_common/land_state.h"
#include "core_common/ledger_state.h"
#include "core_common/load_resource.h"
#include "core_common/logistics_rules.h"
#include "core_common/logistics_state.h"
#include "core_common/module_rules.h"
#include "core_common/quantities.h"
#include "core_common/rain_stops_work.h"
#include "core_common/reaping_pace.h"
#include "core_common/reaping_road.h"
#include "core_common/resident_state.h"
#include "core_common/site_supply_floor.h"
#include "core_common/site_supply_queue.h"
#include "core_common/state_table.h"
#include "core_common/state_table_ops.h"
#include "core_common/work_seam.h"
#include "core_common/world_state.h"
#include "core_log/log.h"
#include "core_tables/required_tables.h"
#include "core_tables/tables.h"
#include "labor_config.h"
#include "labor_day.h"
#include "placement_rank.h"
#include "posts.h"
#include "rush.h"
#include "work_orders.h"

namespace core {
namespace {

/// @brief The window as a PAIR: open with days left, or closed with the days
/// since (core_common/deadline.h).
///
/// THIS RETURNED A BARE COUNT UNTIL 2026-09-12, and the count answered two
/// questions with the same value: 0 meant both "the window closes today" and
/// "the window closed three months ago". The queue ranks the smallest first,
/// so hopeless work outranked work that still mattered — measured on a
/// village handed all its land at once: it ploughed ground it could no longer
/// sow, never cut the hay, and lost its last draught horse in the fifth year.
/// The counterfactual, the same line answering 254, brought that run back to
/// fifty-two horses. Neither number was the repair: "closed" and "due today"
/// want different KINDS of answer, and a number cannot carry a kind.
/// BOTH COUNTS ARE WITHIN THE YEAR, and neither wraps: the day is taken
/// modulo the year, so a window that shut in May reads as 200-odd days
/// overdue in December and starts again from the new year. That is right for
/// a ranking that only ever compares jobs of the same day, and it is written
/// down because "overdue by N" invites being read as a history.
///
/// The open count is 0 on the window's LAST day — "closes today", the one
/// honest zero — and the overdue count is 1 on the first day after, never 0:
/// the two kinds carry numbers on different scales and share no value.
Deadline WindowOf(const CalendarState& calendar, std::uint8_t month_end) {
  const std::uint32_t day_of_year = calendar.day % kDaysPerYear;
  const std::uint32_t window_end = (static_cast<std::uint32_t>(month_end) + 1U) * kDaysPerMonth;
  if (window_end <= day_of_year) {
    return DeadlineOverdue(static_cast<std::int32_t>(day_of_year - window_end) + 1);
  }
  return DeadlineInDays(static_cast<std::int32_t>(window_end - day_of_year - 1U));
}

/// Days left until `last_day` of the year inclusive, or overdue past it: the
/// deadline of a job whose edge is a day rather than a month's end.
Deadline DueByDay(const CalendarState& calendar, std::int32_t last_day) {
  const auto day_of_year = static_cast<std::int32_t>(calendar.day % kDaysPerYear);
  if (last_day < day_of_year) {
    return DeadlineOverdue(day_of_year - last_day);
  }
  return DeadlineInDays(last_day - day_of_year);
}

/// The two places the labour day is measured between live in
/// core_common/work_seam.h since 2026-09-05: the resident's activity needs
/// the same answers, and a second copy would drift.
bool HomePosition(const WorldState& world, FamilyId family, Vec2& home) {
  return HomePositionOf(world, family, home);
}

/// The resident who drives the people's cart a placed hand rides
/// (PlanDayAssignments, rides_cart_with: a candidate index), or an invalid
/// id — on foot, on his own horse, or on a cart that counts no seats.
ResidentId PeoplesCartDriver(const WorldState& world,
                             const std::vector<AssignmentCandidate>& candidates,
                             std::uint32_t driver_index) {
  if (driver_index >= candidates.size()) {
    return ResidentId{};
  }
  return world.residents.row_ids[candidates[driver_index].resident_row];
}

/// THE ACTIVITY'S THRESHOLDS FROM THE OWNER OF MOST OF THEM (resident_
/// activity.h, ActivityRules: «the caller fills this from the configs that
/// own the numbers»): the working age, the walk-off rest, the road rates,
/// the speed of life, sleep, the posts' shifts — labour's. The runs filled
/// their own copies (activity_census, idle_curve: the road rates, the speed
/// of life, the shifts). NOT labour's and left at ActivityRules' defaults,
/// as in every run: the top of the working age (70), the health lines of
/// fitness and treatment (20, 10 — core_residents'), the school band (the
/// plot table's), the meal hour.
ActivityRules ActivityRulesOfConfig(const LaborConfig& config) {
  ActivityRules rules;
  rules.work_from_bio_years = config.adult_age_years;
  rules.life_speedup = config.life_speedup;
  rules.walkoff_rest = config.rest_walkoff_threshold;
  rules.sleep_hours = config.sleep_hours;
  // Real km/h against game hours: the clock runs faster (the runs' rate).
  if (config.walk_speed_kmh > 0.0F) {
    rules.walk_hours_per_km = static_cast<float>(kClockScale) / config.walk_speed_kmh;
  }
  if (config.harness_speed_kmh > 0.0F) {
    rules.harness_hours_per_km = static_cast<float>(kClockScale) / config.harness_speed_kmh;
  }
  if (config.loaded_speed_kmh > 0.0F) {
    rules.people_cart_hours_per_km = static_cast<float>(kClockScale) / config.loaded_speed_kmh;
  }
  rules.horse_kind = config.horse_kind;
  rules.post_shift.reserve(config.professions.size());
  for (const ProfessionDef& profession : config.professions) {
    rules.post_shift.push_back(profession.shift);
  }
  return rules;
}

class LaborSystem final : public ILaborSystem {
 public:
  explicit LaborSystem(LaborConfig config)
      : config_(std::move(config)), activity_rules_(ActivityRulesOfConfig(config_)) {}

  void RunAssignmentDecisions(const WorldState& /*previous*/, WorldState& current) override {
    const std::uint32_t hour = HourFromTick(current.calendar.tick);
    if (hour == 0) {
      StartDay(current);
    }
    // TODAY'S DAY OFF DECLARED WORKING (B8's door; rush.cpp, CancelDayOff,
    // `enable` 1): the order was read at the end of the last hour, the hour-0
    // placement passed on a day off and placed nobody, and it is not given
    // back (no second StartDay: the morning's housekeeping and the standing
    // orders stay the morning's). The day starts here, by the top-up's
    // placement of everybody idle on everything uncrewed. At hour 1 the
    // top-up below does it anyway.
    // Owed only on the declared day itself: an order read at the day's last
    // hour declares a day that is over by the next hour, and that morning's
    // placement is the new day's own.
    const bool placement_owed = current.chairman.place_after_declaring != 0 &&
                                current.chairman.cancelled_day_off == current.calendar.day;
    if (hour != 0 && hour != 1 && placement_owed) {
      PlaceIdleHoldersOnModules(current);
      TopUpDay(current);
    }
    if (hour != 0 || !placement_owed) {
      current.chairman.place_after_declaring = 0;
    }
    if (hour == 1) {
      PlaceIdleHoldersOnModules(current);
      TopUpDay(current);
      // THE GOODS CARTS TAKE PASSENGERS ON THEIR FIRST LEG (routing stage A,
      // 0.37.164; cart_passengers.h): the day's orders stand, nobody has set
      // out. The year's book counts the seats and the waits.
      const PassengerTally tally = SeatCartPassengers(config_, current);
      YearLedger& book = current.ledger.current;
      book.cart_passengers += tally.seated;
      book.cart_passengers_no_seat += tally.no_seat;
      book.cart_passengers_wait_refused += tally.waited_too_long;
      book.cart_wait_hours += tally.wait_hours;
      book.cart_wait_worst_hours = std::max(book.cart_wait_worst_hours, tally.worst_wait_hours);
      book.cart_hours_saved += tally.hours_saved;
      book.waits_made[static_cast<std::size_t>(WaitKind::kPassengerAwaitsCart)] += tally.waits_made;
    }
    RunHour(current, hour);
    // After the hour: a work the hour ended takes its wait with it.
    ClearPassengerWaits(current, hour + 1U >= kTicksPerDay);
    // The night posts go on at sunset (posts.h).
    AnnounceNightShifts(config_, current);
    if (hour + 1U >= kTicksPerDay) {
      CloseDay(current);
      ApplyPostOrders(current);
      VacatePostsWithNoUnit(current);
    }
    // Reading comes LAST, and that is the whole of the "first close of the
    // day AFTER it was accepted" rule (manual/74-posts.md §3): an order that
    // arrives on the day's last tick is validated here, behind the close it
    // just missed, and waits a full day for the next one. Put the read first
    // and an order issued at midnight would take effect the same midnight —
    // a man's post changed while his day was still being paid out.
    ReadPostOrders(current);
    // The two work verbs read here for the same reason posts do: an order
    // arriving today takes effect from the NEXT working day (time design
    // §11), and reading LAST in the tick is what makes that true without a
    // second flag to remember the day by.
    ReadWorkOrders(config_, current);
    // The avral and the cancelled day off, read LAST for the same reason
    // (rush.h): declared today, they act from the next hour or the next day.
    ReadRushOrders(current);
  }

  bool CanBeOrdered(const WorldState& state, ResidentId resident) const override {
    const std::uint32_t row = FindRow(state.residents, resident);
    return row != kNoRow && Employable(state, state.residents.rows[row]);
  }

  WorkforceCount CountWorkforce(const WorldState& state) const override {
    WorkforceCount count;
    for (std::uint32_t row = 0; row < state.residents.rows.size(); ++row) {
      const ResidentRow& resident = state.residents.rows[row];
      if (!Employable(state, resident)) {
        continue;
      }
      ++count.employable;
      // IDLE IS kIdle — no order, of working age and fit, IN WORKING HOURS
      // (boss-core-epoch1-queue-2026-09-29 [28]–[30]): the count read "no
      // order at this moment", so at midnight, the day's assignments
      // cleared, it was the whole workforce — «Работают 0 · Без дела 50» in
      // the office at 00:00 (ue). One answer with the office's list, which
      // filters by ActivityOf. A man with an order is never kIdle: asked
      // only of the unordered, so the road to a workplace is not priced for a
      // count the layer asks every frame (static review of 0.37.20).
      if (resident.work.kind != WorkKind::kNone) {
        continue;
      }
      count.idle +=
          ActivityOfResident(state, row, activity_rules_).activity == ResidentActivity::kIdle ? 1U
                                                                                              : 0U;
    }
    return count;
  }

  std::optional<ResidentActivityState> ActivityOf(const WorldState& state,
                                                  ResidentId resident) const override {
    const std::uint32_t row = FindRow(state.residents, resident);
    if (row == kNoRow) {
      return std::nullopt;
    }
    return ActivityOfResident(state, row, activity_rules_);
  }

  std::vector<WorkbookLine> OfficeWorkbook(const WorldState& state) const override {
    std::vector<WorkbookLine> lines;
    lines.reserve(state.residents.rows.size());
    for (std::uint32_t row = 0; row < state.residents.rows.size(); ++row) {
      const ResidentRow& resident = state.residents.rows[row];
      WorkbookLine line;
      line.resident = state.residents.row_ids[row];
      line.family = resident.family;
      line.age_years = BiologicalAgeYears(config_, resident.birth_day, state.calendar.day);
      line.can_be_ordered = Employable(state, resident);
      line.work = resident.work.kind;
      line.field = resident.work.field;
      line.unit = resident.work.unit;
      line.idle = resident.idle_reason;
      lines.push_back(line);
    }
    return lines;
  }

  bool FellingCanBeManned(const WorldState& state, Vec2 place) const override {
    return FellingCanBeMannedToday(state, place, ReachRuleOf(), DraughtHorses(state));
  }

  void CollectAlarms(const WorldState& state, std::vector<Alarm>& out) const override {
    // Once the team is in, the question is closed for the campaign: the flag
    // is the milestone, not the yard's current staffing (world_state.h).
    if (state.chairman.horses_stabled != 0 || config_.groom_post.value == kInvalidDefIdValue) {
      return;
    }
    const std::int64_t waiting = HorsesAtPrivateYards(state);
    if (waiting <= 0) {
      return;  // nothing stands at the yards: nothing to be freed
    }
    for (std::uint32_t row = 0; row < state.units.rows.size(); ++row) {
      const UnitRow& unit = state.units.rows[row];
      const UnitId id = state.units.row_ids[row];
      if (FindStaffSlot(config_, unit.type, unit.level, config_.groom_post) == nullptr) {
        continue;
      }
      if (HasGroom(state, id)) {
        // Appointed and the horses not moved yet: that is the one idle
        // morning of manual/74-posts.md §5, and it is SILENT. The player did
        // everything right and is not told off for the slot order.
        continue;
      }
      Alarm alarm;
      alarm.kind = AlarmKind::kYardWithoutGroom;
      alarm.unit = id;
      alarm.amount = waiting;
      out.push_back(alarm);
    }
  }

 private:
  // -- the order book (task A7; manual/74-posts.md §3) ---------------------

  /// Validates every kAppoint and kDismiss the engine appended this step.
  /// A post order NEVER settles in the step it is read: it goes to
  /// kAccepted — visible, and cancellable right up to the moment it takes
  /// effect — or straight to kRefused, which is an answer and needs no
  /// waiting.
  void ReadPostOrders(WorldState& current) const {
    for (std::uint32_t row = 0; row < current.orders.rows.size(); ++row) {
      OrderRow& order = current.orders.rows[row];
      if (order.status != OrderStatus::kPending ||
          (order.kind != OrderKind::kAppoint && order.kind != OrderKind::kDismiss)) {
        continue;
      }
      OrderRefusal refusal = order.kind == OrderKind::kAppoint
                                 ? CheckAppointment(config_, current, order)
                                 : CheckDismissal(current, order);
      if (refusal == OrderRefusal::kNone && HasWaitingPostOrder(current, order.resident, row)) {
        refusal = OrderRefusal::kConflictsWithActive;
      }
      // And the same conflict when the other side is still UNREAD. The post
      // verbs are read before the work verbs, so a kAssignWork from this very
      // batch is kPending here and invisible to CheckAppointment — which made
      // the appointment win every same-batch race, however late it was given.
      // The row index is what tells first from second: only a work order
      // BELOW this one arrived before it (task A8 delivery cycle).
      if (refusal == OrderRefusal::kNone && order.kind == OrderKind::kAppoint &&
          WorkOrderCameFirst(current, order.resident, row)) {
        refusal = OrderRefusal::kConflictsWithActive;
      }
      if (refusal != OrderRefusal::kNone) {
        order.status = OrderStatus::kRefused;
        order.refusal = refusal;
        continue;
      }
      order.status = OrderStatus::kAccepted;
    }
  }

  /// The day is over and the men are paid: now posts may change (time design
  /// §11). Everything is checked a SECOND time here, because a day is long
  /// enough for the unit to be demolished, the man to die, or the last free
  /// slot to be taken by another order accepted the same morning — and of
  /// two orders for one place, the one applied first is the one that gets it.
  void ApplyPostOrders(WorldState& current) const {
    for (std::uint32_t row = 0; row < current.orders.rows.size(); ++row) {
      OrderRow& order = current.orders.rows[row];
      if (order.status != OrderStatus::kAccepted ||
          (order.kind != OrderKind::kAppoint && order.kind != OrderKind::kDismiss)) {
        continue;
      }
      const OrderRefusal refusal = order.kind == OrderKind::kAppoint
                                       ? CheckAppointment(config_, current, order)
                                       : CheckDismissal(current, order);
      if (refusal != OrderRefusal::kNone) {
        order.status = OrderStatus::kRefused;
        order.refusal = refusal;
        continue;
      }
      const std::uint32_t resident_row = FindRow(current.residents, order.resident);
      ResidentRow& resident = current.residents.rows[resident_row];
      const bool appointing = order.kind == OrderKind::kAppoint;
      SimEvent& event = EmitEvent(current,
                                  appointing ? EventKind::kAppointed : EventKind::kDismissed,
                                  EventSeverity::kNotable);
      event.resident = order.resident;
      if (appointing) {
        // A man who already holds a post is MOVED, not doubled: one work a
        // day means one place (time design §11).
        resident.post.profession = order.profession;
        resident.post.unit = order.unit;
        event.unit = order.unit;
        event.amount = static_cast<std::int64_t>(order.profession.value);
      } else {
        event.unit = resident.post.unit;
        event.amount = static_cast<std::int64_t>(resident.post.profession.value);
        resident.post = PostAssignment{};
      }
      order.status = OrderStatus::kDone;
      order.refusal = OrderRefusal::kNone;
    }
  }

  /// @brief Whether anybody holds the groom's post at this unit.
  bool HasGroom(const WorldState& world, UnitId unit) const {
    for (const ResidentRow& resident : world.residents.rows) {
      if (resident.post.profession.value == config_.groom_post.value &&
          resident.post.unit.value == unit.value) {
        return true;
      }
    }
    return false;
  }

  /// @brief Kolkhoz horses still standing at private yards, all ages: what
  /// the yard is waiting for, and what the alarm counts.
  std::int64_t HorsesAtPrivateYards(const WorldState& world) const {
    if (config_.horse_kind.value == kInvalidDefIdValue) {
      return 0;
    }
    std::int64_t heads = 0;
    for (const HerdRow& herd : world.herds.rows) {
      if (herd.kind.value != config_.horse_kind.value || herd.household_owned != 0 ||
          herd.household.value == kInvalidEntityIdValue) {
        continue;
      }
      heads += static_cast<std::int64_t>(herd.adult_count) +
               static_cast<std::int64_t>(herd.juvenile_count) +
               static_cast<std::int64_t>(herd.newborn_count);
    }
    return heads;
  }

  // -- the morning ---------------------------------------------------------

  // The economic year's burn used to stand here, as a placeholder for the
  // distribution it pays for. That distribution exists since stage 6, and
  // the burn moved with it into the family/kolkhoz exchange
  // (core_residents/family_exchange.cpp): both counters have to burn, and
  // only after the year's last issue has been made against them. Labor runs
  // BEFORE residents in the decisions slot, so burning here would have
  // emptied the account the exchange was about to spend.
  /// Some arable field still owes reaping: yesterday's reaping was short of
  /// hands, not of work (the pace's condition, ledger_state.h).
  static bool ReapingStillOwed(const WorldState& current) {
    return std::ranges::any_of(current.fields.rows, [](const FieldRow& field) {
      return field.kind == LandKind::kArable && field.phase == FieldPhase::kHarvest &&
             field.work_days_remaining > 0.0F;
    });
  }

  void StartDay(WorldState& current) const {
    for (ResidentRow& resident : current.residents.rows) {
      resident.work = WorkAssignment{};
    }
    StandDownRushes(current);
    // The reaping pace rolls over: yesterday's whole day of hand reaping on
    // the arable becomes the pace (ledger_state.h, boss seq 161 Б) — IF it
    // ended with reaping still owed. A day that finished the last field is
    // short of work, not of hands, and would read as a slow village. THE
    // HAND-HOURS OF LIGHT THE DAY OFFERED go with it (0.37.147): the hands
    // that could reap times the day's light — with the norm-days they are the
    // count by fields' rate (reaping_pace.h, ReapingCrewOf). The day's
    // daylight alone went here from save 64 to save 129; the count walks each
    // day under its own light now.
    //
    // AND ONLY A DAY REAPED IN STRENGTH (farming.csv
    // `reaping_in_strength_share`): the reapers' light against the light the
    // hands that could were offered. Three reapers of fifty on the July rye
    // are the queue's choice of a day; read as the village's rate they made
    // the count cry all summer (0.37.91's message, year 2, nine villages of
    // nine).
    YearLedger& book = current.ledger.current;
    // The book's «today» is the day that has just ended: its light.
    const float light =
        current.calendar.day > 0 ? DaylightHoursOfDay(current.calendar.day - 1U) : 0.0F;
    const float offered = book.reaping_today_hands * light;
    if (book.reaping_today > 0.0F && offered > 0.0F && ReapingStillOwed(current) &&
        book.reaping_today_hours >= config_.reaping_in_strength_share * offered) {
      book.reaping_last_day = book.reaping_today;
      book.reaping_last_day_hours = offered;
    }
    // The hands the reaping could have had roll EVERY day (0.37.147): they
    // are the village's strength of a morning, whatever was reaped.
    if (book.reaping_today_hands > 0.0F) {
      book.reaping_last_day_hands = book.reaping_today_hands;
    }
    book.reaping_today = 0.0F;
    book.reaping_today_hours = 0.0F;
    book.reaping_today_hands = 0.0F;
    RefillHerdCare(current);
    AssignPostHolders(current);
    // UB-001 fix: the accountant's placement is a BLOCK, not the body of the
    // function, because the chairman's orders below it must run on days the
    // accountant has nothing to do. A day off, or a winter day with every
    // field idle and no load lying, gives an empty job list — and the two
    // bare early returns that used to stand here skipped the standing
    // orders with it. work_orders.h promises the opposite: a standing order
    // whose target has no work today leaves its man IDLE, not unassigned.
    const std::vector<AssignmentJob> jobs = CollectJobs(current);
    // WHY NOT PLACED (save 102; YearLedger::idle_person_days): the morning's
    // plan only, as the road's blocked job-days. Resting adults are counted
    // by the list itself; with no job at all, everybody on the list is idle
    // for that; on a day off, whatever the barn left.
    const bool day_off_today = IsDayOffIn(current, current.calendar.day);
    // AND ON THE RESIDENT (ResidentRow::idle_reason; the office's workbook):
    // the same reason the book counts, one a person, this morning's. Cleared
    // first: a man placed today, or not asked, carries no reason. Yesterday's
    // reasons are read before they go: they are the memory of the run
    // kHandsIdleWithWorkWaiting is said at the start of (task 427).
    const bool hands_idle_yesterday = std::ranges::any_of(
        current.residents.rows,
        [](const ResidentRow& resident) { return IdleForAJobsShortfall(resident.idle_reason); });
    for (ResidentRow& resident : current.residents.rows) {
      resident.idle_reason = IdleReason::kIdleReasonCount;
    }
    std::vector<std::uint32_t> resting;
    std::vector<AssignmentCandidate> candidates = CollectCandidates(current, &resting);
    book.idle_person_days[static_cast<std::size_t>(IdleReason::kResting)] +=
        static_cast<std::uint32_t>(resting.size());
    for (const std::uint32_t row : resting) {
      current.residents.rows[row].idle_reason = IdleReason::kResting;
    }
    book.candidate_person_days += static_cast<std::uint32_t>(candidates.size());
    const bool rain_holds = RainHoldsFieldWork(current);
    if (jobs.empty()) {
      const IdleReason none = BookedIdleReason(IdleReason::kNoOpenWork, day_off_today, rain_holds);
      book.idle_person_days[static_cast<std::size_t>(none)] +=
          static_cast<std::uint32_t>(candidates.size());
      for (const AssignmentCandidate& candidate : candidates) {
        current.residents.rows[candidate.resident_row].idle_reason = none;
      }
      // No job this morning: no run stands (a day off keeps them as they are).
      TrackUnmannedRuns(current, jobs, {}, {}, day_off_today);
    }
    if (!jobs.empty()) {
      book.offered_job_days += static_cast<std::uint32_t>(jobs.size());
      // Nobody on the list at all (static review of 0.36.32): the plan is not
      // run, and every job with work left went short of hands — booked, not
      // left out of the short count while the offered count took it.
      if (candidates.empty()) {
        for (const AssignmentJob& job : jobs) {
          const auto kind = static_cast<std::size_t>(job.kind);
          if (job.work_days_remaining > 0.0F && kind < book.short_job_days.size()) {
            ++book.short_job_days[kind][static_cast<std::size_t>(JobShortfall::kNoHands)];
          }
        }
        TrackUnmannedRuns(current, jobs, {}, {}, day_off_today);
      }
      if (!candidates.empty()) {
        std::vector<std::uint8_t> rides_horse;
        std::vector<std::uint8_t> road_blocked;
        std::vector<std::uint32_t> rides_cart_with;
        PlacementDiagnosis diagnosis;
        AssignmentParams params = DayParams(current);
        MeasureRoads(current, jobs, candidates, params);
        const std::vector<std::uint32_t> plan = PlanDayAssignments(
            jobs, candidates, params, &rides_horse, &road_blocked, &diagnosis, &rides_cart_with);
        for (std::size_t index = 0; index < jobs.size(); ++index) {
          const auto kind = static_cast<std::size_t>(jobs[index].kind);
          if (diagnosis.shortfall[index] && kind < book.short_job_days.size()) {
            ++book.short_job_days[kind][static_cast<std::size_t>(*diagnosis.shortfall[index])];
          }
        }
        BookLogPairs(jobs, plan, rides_horse, diagnosis, book);
        for (std::size_t index = 0; index < candidates.size(); ++index) {
          if (diagnosis.idle[index]) {
            // A day off answers for the idle — except a contradiction of the
            // plan, which no day off explains (static review of 0.36.32: the
            // override hid kUnexplained on every day off).
            const IdleReason reason =
                BookedIdleReason(*diagnosis.idle[index], day_off_today, rain_holds);
            ++book.idle_person_days[static_cast<std::size_t>(reason)];
            current.residents.rows[candidates[index].resident_row].idle_reason = reason;
          }
        }
        if (!hands_idle_yesterday) {
          SayHandsIdleWithWorkWaiting(current, jobs, candidates, diagnosis);
        }
        TrackUnmannedRuns(current, jobs, plan, diagnosis.shortfall, day_off_today);
        // THE MORNING'S PLAN ONLY: the day's jobs the road stopped, by kind
        // (YearLedger::road_blocked_job_days). The top-up re-plans the same
        // day and would count it twice.
        for (std::size_t index = 0; index < jobs.size(); ++index) {
          const auto kind = static_cast<std::size_t>(jobs[index].kind);
          if (road_blocked[index] != 0U && kind < book.road_blocked_job_days.size()) {
            ++book.road_blocked_job_days[kind];
          }
        }
        // THE HANDS THE REAPING CAN HAVE (YearLedger::reaping_today_hands;
        // 0.37.147): the morning's list less those the plan put on the
        // herds' care — a standing duty of every day. A post's holder is not
        // in the list at all. What the count by fields takes for the crew
        // (core_common/reaping_pace.h): not the hands that reaped — three
        // reapers in July are the queue's choice of a day.
        float free_hands = 0.0F;
        for (std::uint32_t index = 0; index < candidates.size(); ++index) {
          // The care's re-pricing (#709; care_reprice.h): marked whether the
          // second pass placed him or left him free.
          const bool moved_off_care =
              index < diagnosis.moved_off_care.size() && diagnosis.moved_off_care[index] != 0;
          current.residents.rows[candidates[index].resident_row].work.moved_off_care =
              moved_off_care ? 1U : 0U;
          if (plan[index] == kNoJobAssigned) {
            free_hands += 1.0F;
            continue;
          }
          const AssignmentJob& job = jobs[plan[index]];
          free_hands += job.kind == WorkKind::kHerdCare ? 0.0F : 1.0F;
          WorkAssignment& work = current.residents.rows[candidates[index].resident_row].work;
          work.rides_horse = rides_horse[index];
          work.rides_cart_of = PeoplesCartDriver(current, candidates, rides_cart_with[index]);
          work.kind = job.kind;
          work.field = job.field;
          work.herd = job.herd;
          work.unit = job.unit;
          work.stand = job.stand;
          work.extraction_site = job.extraction_site;
          work.limit_delivery = job.limit_delivery;
          work.road_work = job.road_work;
          work.travel_hours = -1.0F;  // a new target: its road is measured anew
        }
        book.reaping_today_hands = free_hands;
      }
    }
    // AND THE CHAIRMAN OVERRULES THE ACCOUNTANT, last and without argument
    // (delegation design §7, management by exception). It is done after the
    // placement rather than by excluding these men from it, and the
    // difference is the point: the accountant plans the day as if they were
    // his, and one man is then taken off that plan. Nothing is revoked —
    // "one overridden placement leaves the delegation as it was".
    //
    // THE REST DAY IS SAID OUT LOUD HERE, and it used to be said by
    // accident. The chairman commands the work, not the calendar: a
    // standing order does not send a man to the field on a Sunday. Before
    // the delivery cycle of task A8 that came out of CollectJobs returning
    // an empty list on a day off and StartDay returning with it — which
    // held only in a village with no barn, because herd care is collected
    // on a day off too (animals eat on Sunday). Where a barn stood, the
    // list was not empty, the return did not happen, and the chairman's man
    // worked every Sunday of his life. One rule must not depend on whether
    // an unrelated one had anything to say.
    ApplyStandingWork(current,
                      current,
                      IsDayOffIn(current, current.calendar.day),
                      config_.rest_walkoff_threshold);
  }

  /// THE WORK THAT OPENED AFTER THE MORNING (boss seq 93/95, option а). The
  /// decisions slot runs labor before production (core_world/world.cpp), so
  /// at hour 0 the accountant places the day and only THEN does production's
  /// daily block open a field's next phase — and until 2026-09-18 that field
  /// waited for the next morning. Traced on seed 1930: the potato opened its
  /// reaping on day 36 with 69 people idle all day, and the snow took the
  /// field on day 42. Every phase the daily block opens paid that day.
  ///
  /// At hour 1 — before sunrise, so no working hour is lost — the idle are
  /// placed on the jobs nobody stands on yet. Only those: a job already
  /// crewed in the morning had its crew sized to its demand, and topping it
  /// up would put surplus hands on it. The morning is not moved, because
  /// production reads the morning's placement in that same hour-0 block (the
  /// team's working share, herd_system.cpp).
  ///
  /// THE MORNING'S PLACEMENT STANDS THE DAY (#633, 0.37.233; time design «Один
  /// работник — одна работа в день»): only the unplaced are placed here, with
  /// only the horses the morning left. From 0.37.101 to 0.37.232 the morning's
  /// windowless placements were let go and placed again whenever a job stood
  /// uncrewed (on seed 1931, day 30, five horses rode for the district's lot
  /// while the harrowing of the rye's fallow, opened that dawn, stood the day
  /// with no horse); that broke the one-job rule, and a job opened after the
  /// morning that the unplaced cannot crew waits for tomorrow's morning plan -
  /// whose ranking is the place to mend it (boss, 12 October 2026).
  /// THE DAY'S ONE WORD ABOUT A STACK MADE URGENT BY THE STORES' HAY
  /// (EventKind::kStackCartUrgent; 0.37.253): the first meadow in row order
  /// whose carting the rule of `hay_cart_urgent_days` put ahead of all work.
  /// Called from the hour-1 top-up alone, so once a village-day; a day off
  /// offers no carting and says nothing.
  ///
  /// ONCE A RICK A DAY since 0.37.255: each rick short by the rule is named
  /// (SimEvent::unit), in the units' row order; a day the rule fired on the
  /// village's sum alone (no rick asked, or the herds bound to none) says the
  /// one word with no unit, as before.
  void SayAStackIsUrgent(WorldState& current, const std::vector<AssignmentJob>& offered) const {
    constexpr int kHayKilogramsShift = 16;
    for (const AssignmentJob& job : offered) {
      if (job.kind != WorkKind::kHauling || job.urgent_by_stores_days == 0) {
        continue;
      }
      const std::uint32_t field_row = FindRow(current.fields, job.field);
      if (field_row == kNoRow) {
        continue;
      }
      const ResourceId hay = current.fields.rows[field_row].reaped_resource;
      Grams in_units = 0;
      for (const UnitRow& unit : current.units.rows) {
        in_units += unit.dead == 0 ? UnreservedOf(unit, hay) : 0;
      }
      std::vector<ShortRickUnit> short_ricks;
      if (config_.short_ricks) {
        short_ricks = config_.short_ricks(current, job.urgent_by_stores_days);
      }
      if (short_ricks.empty()) {
        short_ricks.push_back(ShortRickUnit{});  // the village's sum: no rick named
      }
      for (const ShortRickUnit& rick : short_ricks) {
        SimEvent& event = EmitEvent(current, EventKind::kStackCartUrgent, EventSeverity::kNotable);
        event.field = job.field;
        event.unit = rick.rick;
        event.resource = hay;
        event.amount = static_cast<std::int64_t>(job.urgent_by_stores_days) |
                       ((in_units / kGramsPerKilogram) << kHayKilogramsShift);
      }
      return;
    }
  }

  /// `only_rows` (S1'): when given, the top-up of the hour's released carters
  /// alone - it places only them, and neither releases horse work nor says a
  /// stack is urgent, which are the morning's. `released_from`: the jobs they
  /// were released from, not offered again this hour (the unit's first run
  /// placed the released carter back on his own load, which the plan would
  /// not draw, and he would be released and placed again every hour).
  void TopUpDay(WorldState& current,
                const std::vector<std::uint32_t>* only_rows = nullptr,
                const std::vector<WorkAssignment>* released_from = nullptr) const {
    if (only_rows == nullptr) {
      ReleaseHorselessWork(current);
    }
    const std::vector<AssignmentJob> offered = CollectJobs(current);
    if (only_rows == nullptr) {
      SayAStackIsUrgent(current, offered);
    }
    // road_work is not asked, as it never was here: one road crew answers for
    // every road job of the morning (named in the commit of 0.37.101, not
    // mended by it — the canon lays no road).
    const auto stands_on = [](const WorkAssignment& work, const AssignmentJob& job) {
      return work.kind == job.kind && work.field.value == job.field.value &&
             work.herd.value == job.herd.value && work.unit.value == job.unit.value &&
             work.stand.value == job.stand.value &&
             work.extraction_site.value == job.extraction_site.value &&
             work.limit_delivery.value == job.limit_delivery.value;
    };
    const auto crewed = [&current, &stands_on](const AssignmentJob& job) {
      return std::ranges::any_of(
          current.residents.rows,
          [&job, &stands_on](const ResidentRow& person) { return stands_on(person.work, job); });
    };
    std::vector<AssignmentJob> jobs = offered;
    std::erase_if(jobs, crewed);
    if (released_from != nullptr) {
      std::erase_if(jobs, [released_from, &stands_on](const AssignmentJob& job) {
        return std::ranges::any_of(*released_from, [&job, &stands_on](const WorkAssignment& work) {
          return stands_on(work, job);
        });
      });
    }
    if (jobs.empty()) {
      return;
    }
    // ONE JOB A DAY (#633; time design «Один работник — одна работа в день»;
    // boss, 12 October 2026: «The morning's placement IS the day's one job once
    // it is made … The world, not a better-ranked job, is the only thing that
    // ends a placement»): the top-up places ONLY THE UNPLACED - the morning's
    // idle, and the hands whose job the world ended since (the field done, the
    // rain, the horse gone: ReleaseHorselessWork above, the labour hour's «job
    // gone from under him»). It never re-places a placed hand, windowless or
    // not, set out or not. Until 0.37.233 it LET GO at this hour everyone on
    // a windowless job (0.37.101), the carriers on foot of a cart load
    // (0.37.105) and, with a level-0 load uncrewed, every horse-holder (B5),
    // and placed them again by tier: host's barter trace (seed 2, 0.37.228)
    // saw a builder placed on a works at hour 0 sent to a meadow opened that
    // morning at hour 1, the works standing four more days with 0.43 man-days
    // left. A windowed job or a level-0 load that opens after the morning
    // waits for the unplaced and for tomorrow's morning plan; if the morning
    // ranks it wrong, that is the morning's ranking to mend (boss), not this
    // hour's.
    std::vector<AssignmentCandidate> candidates = CollectCandidates(current);
    std::erase_if(candidates, [&current, only_rows](const AssignmentCandidate& candidate) {
      return current.residents.rows[candidate.resident_row].work.kind != WorkKind::kNone ||
             (only_rows != nullptr &&
              std::ranges::find(*only_rows, candidate.resident_row) == only_rows->end());
    });
    if (candidates.empty()) {
      return;
    }
    // ONLY THE HORSES THE MORNING LEFT (boss, boss-core-topup-horses seq 1).
    // Until 0.34.51 this handed the placement the whole herd again, and a
    // ploughing opened after the morning took horses already in the traces:
    // host measured spring ploughing and harrowing above the herd in a third
    // of the canon's March and April seed-months. The people's carts' horses
    // are in the traces too: until 0.37.233 they were LENT to this queue
    // (0.37.168), since the let-go placed their crews again; with nobody let
    // go a lent horse could only be taken back from a crew that keeps its
    // job, which is the re-placing the one-job rule forbids.
    AssignmentParams params = DayParams(current);
    const std::uint32_t in_traces = HorsesInTraces(current);
    params.draught_horses =
        in_traces < params.draught_horses ? params.draught_horses - in_traces : 0U;
    // THE FLOOR IS THE MORNING'S (#734): its carters were dealt once, by the
    // whole team; a top-up that dealt them again would give the share twice.
    params.supply_floor_carters = 0;
    MeasureRoads(current, jobs, candidates, params);
    std::vector<std::uint8_t> rides_horse;
    std::vector<std::uint32_t> rides_cart_with;
    const std::vector<std::uint32_t> plan = PlanDayAssignments(
        jobs, candidates, params, &rides_horse, nullptr, nullptr, &rides_cart_with);
    for (std::uint32_t index = 0; index < candidates.size(); ++index) {
      if (plan[index] == kNoJobAssigned) {
        continue;  // idle still: the morning booked his reason, and it stands
      }
      const AssignmentJob& job = jobs[plan[index]];
      ResidentRow& placed = current.residents.rows[candidates[index].resident_row];
      // Placed by the top-up: no longer free, and no longer carrying the
      // morning's reason (the workbook; static review of 0.37.0 — seed 1930's
      // 69 potato reapers read "no open work" all day).
      placed.idle_reason = IdleReason::kIdleReasonCount;
      WorkAssignment& work = placed.work;
      work.rides_horse = rides_horse[index];
      work.rides_cart_of = PeoplesCartDriver(current, candidates, rides_cart_with[index]);
      work.kind = job.kind;
      work.field = job.field;
      work.herd = job.herd;
      work.unit = job.unit;
      work.stand = job.stand;
      work.extraction_site = job.extraction_site;
      work.limit_delivery = job.limit_delivery;
      work.road_work = job.road_work;
      work.travel_hours = -1.0F;  // a new target: its road is measured anew
    }
    // A GUARD, not a lever since 0.37.233: the queue was given only the
    // horses the morning left, so nothing is above the herd here unless the
    // world took a horse this hour.
    ReleaseHorselessWork(current);
  }

  /// @brief Fills AssignmentParams::road_km for these jobs and candidates, and
  ///        each candidate's home slot (road_route.h; 0.36.2): every distinct
  ///        home found on the network once a mode, every job once for walking
  ///        and once for its riding mode, and the table read between them —
  ///        a few hundred finds a morning, not candidates times jobs queries.
  void MeasureRoads(const WorldState& current,
                    const std::vector<AssignmentJob>& jobs,
                    std::vector<AssignmentCandidate>& candidates,
                    AssignmentParams& params) const {
    const std::shared_ptr<const RoadIndex> index = RoadIndexOf(current);
    std::vector<Vec2> homes;
    for (AssignmentCandidate& candidate : candidates) {
      std::uint32_t slot = 0;
      while (slot < homes.size() &&
             (homes[slot].x != candidate.home.x || homes[slot].y != candidate.home.y)) {
        ++slot;
      }
      if (slot == homes.size()) {
        homes.push_back(candidate.home);
      }
      candidate.home_slot = slot;
    }
    constexpr std::array<TravelMode, 4> kModes = {
        TravelMode::kWalk, TravelMode::kTeam, TravelMode::kCart, TravelMode::kLogCart};
    std::vector<std::array<NetworkPlace, 4>> found_homes(homes.size());
    for (std::size_t slot = 0; slot < homes.size(); ++slot) {
      for (std::size_t mode = 0; mode < kModes.size(); ++mode) {
        found_homes[slot][mode] = index->Locate(kModes[mode], homes[slot]);
      }
    }
    params.home_slots = static_cast<std::uint32_t>(homes.size());
    params.road_km.assign(jobs.size() * homes.size() * 2U, 0.0F);
    // THE HORSE YARD, once the team stands there (horse_yard_road.h;
    // 0.37.158): every home's walk to it, every job's ride from it.
    Vec2 yard{};
    const bool stabled = HorseYardPositionOf(current, config_.horse_kind, yard);
    std::array<NetworkPlace, 4> found_yard{};
    params.yard_walk_hours.clear();
    params.yard_ride_km.clear();
    if (stabled) {
      for (std::size_t mode = 0; mode < kModes.size(); ++mode) {
        found_yard[mode] = index->Locate(kModes[mode], yard);
      }
      const float walk_hours_per_km = HoursPerKm(config_, WorkKind::kHarvest);
      params.yard_walk_hours.resize(homes.size());
      for (std::size_t slot = 0; slot < homes.size(); ++slot) {
        params.yard_walk_hours[slot] =
            index->EffectiveKm(found_homes[slot][0], found_yard[0]) * walk_hours_per_km;
      }
      params.yard_ride_km.resize(jobs.size());
    }
    for (std::size_t job_index = 0; job_index < jobs.size(); ++job_index) {
      const AssignmentJob& job = jobs[job_index];
      // The job's riding mode: a cart with produce on the roads, a cart with
      // logs off them (roads design §11), every other ride a team.
      std::size_t ride_mode = 1;
      if (job.kind == WorkKind::kHauling) {
        // AND LOGS STRAIGHT OFF A STAND TO A WORKS (the direct leg;
        // AssignmentJob::log_cart). The arm's first run had this ride as the
        // produce cart's: the stand's loading point lay past the road limit
        // by the roads alone, nobody was placed, and works with a stand's leg
        // at the front waited 150-340 days (27 works over twenty days in
        // nine villages' ten years, every one with a direct leg; the works
        // fed from stores alone waited 13 at the worst).
        const bool logs = job.stand.value != kInvalidEntityIdValue ||
                          job.limit_delivery.value != kInvalidEntityIdValue || job.log_cart;
        ride_mode = logs ? 3 : 2;
      }
      // THE BRIGADE'S CART RIDES AS A TEAM, the fellers' way and the mowers'
      // (boss: «образец — вальщики»), not as the cart with produce that
      // keeps to the roads: measured so first, the potato field 0.68 km out
      // was 1.40 km by the roads, the ride saved a quarter of an hour of an
      // hour and a half, and a hand's October day stayed at 0.60 of a norm.
      const NetworkPlace walk_place = index->Locate(TravelMode::kWalk, job.position);
      const NetworkPlace ride_place = index->Locate(kModes[ride_mode], job.position);
      for (std::size_t slot = 0; slot < homes.size(); ++slot) {
        const std::size_t at = ((job_index * homes.size()) + slot) * 2U;
        params.road_km[at] = index->EffectiveKm(found_homes[slot][0], walk_place);
        params.road_km[at + 1] = index->EffectiveKm(found_homes[slot][ride_mode], ride_place);
      }
      if (stabled) {
        params.yard_ride_km[job_index] = index->EffectiveKm(found_yard[ride_mode], ride_place);
      }
    }
  }

  /// @brief Takes off the morning's work the men whose horse is gone: the
  ///        herd day runs after the placement in hour 0 (core_world/world.cpp,
  ///        labour before production), and a horse that died there left its
  ///        ploughman in the furrow all day with nothing to pull the plough.
  ///        Measured on the canon, seed 1931, day 17: sixteen horses at the
  ///        placement, fifteen at hour 1, sixteen ploughmen (host's 18
  ///        mornings of 3240; boss, boss-core-epoch1-5 seq 24).
  /// @post The horse holders number no more than the herd. They are released
  ///       from the last row up, a ploughman, a harrower or a carter on his
  ///       horse alike: a carter is released rather than set walking, because
  ///       his reach was judged by the ride. The released stand idle for the
  ///       top-up, which may still send them to work that needs no horse;
  ///       before sunrise they have worked nothing to be paid for.
  ///       THE CARTS OF WHAT DOES NOT SPOIL GO FIRST: the district's lot
  ///       (0.36.18), a stand's logs and a dig's load (0.36.19) are the
  ///       lowest horse work of the day (windowless, below the rye's fallow),
  ///       and releasing by row alone could take the ploughman off and leave
  ///       a cart riding for logs.
  void ReleaseHorselessWork(WorldState& current) const {
    const std::uint32_t herd = DraughtHorses(current);
    std::uint32_t in_traces = HorsesInTraces(current);
    // THE BRIGADES' CARTS FIRST OF ALL (0.37.89): a reaping or a sowing that
    // loses its cart loses the ride and not the day — the driver keeps his
    // work, the horse is no longer written on him, and the field's hands walk
    // (WorkRidesOut finds no driver). Their road is measured again. THE SAME
    // FOR A MEADOW'S MOWER AND A PEOPLE'S CART (A3, A4; 0.37.168): the mowers
    // go on with scythes, the crew walks — its seats name the driver
    // (WorkAssignment::rides_cart_of), who holds no horse now.
    //
    // THE PEOPLE'S CARTS BEFORE ALL OF THEM (0.37.168): they are given from
    // what the plough and the goods carts left (assignment.h), and the top-up
    // lends their horses to its own queue (TopUpDay) — so the release takes
    // them back first, and the order «plough, carts, people» holds there too.
    for (const bool peoples_pass : {true, false}) {
      for (auto row = static_cast<std::uint32_t>(current.residents.rows.size());
           row > 0 && in_traces > herd;
           --row) {
        WorkAssignment& work = current.residents.rows[row - 1].work;
        const bool field_crew = work.kind == WorkKind::kHarvest || work.kind == WorkKind::kSowing;
        const bool peoples_cart = TakesThePeoplesCart(work.kind);
        if (work.rides_horse == 0 || (peoples_pass ? !peoples_cart : !field_crew)) {
          continue;
        }
        work.rides_horse = 0;
        work.travel_hours = -1.0F;
        const ResidentId driver = current.residents.row_ids[row - 1];
        for (ResidentRow& person : current.residents.rows) {
          const bool same_field = field_crew && person.work.kind == work.kind &&
                                  person.work.field.value == work.field.value;
          if (same_field || person.work.rides_cart_of.value == driver.value) {
            person.work.travel_hours = -1.0F;
          }
        }
        --in_traces;
      }
    }
    for (const bool timber_pass : {true, false}) {
      for (auto row = static_cast<std::uint32_t>(current.residents.rows.size());
           row > 0 && in_traces > herd;
           --row) {
        WorkAssignment& work = current.residents.rows[row - 1].work;
        const bool carter_on_horse = work.kind == WorkKind::kHauling && work.rides_horse != 0;
        const bool timber_carter =
            carter_on_horse && (work.limit_delivery.value != kInvalidEntityIdValue ||
                                work.stand.value != kInvalidEntityIdValue ||
                                work.extraction_site.value != kInvalidEntityIdValue);
        if (timber_pass ? !timber_carter : (!carter_on_horse && !IsHorseWork(work.kind))) {
          continue;
        }
        work = WorkAssignment{};
        // Freed for want of a horse: the workbook says so (IdleReason::kNoHorse).
        current.residents.rows[row - 1].idle_reason = IdleReason::kNoHorse;
        --in_traces;
      }
    }
  }

  /// @brief The horses the day's placements already hold, counted the way
  ///        PlanDayAssignments hands them out: one a ploughman or harrower,
  ///        one a carter the placement gave one (WorkAssignment::rides_horse),
  ///        one a MEADOW for its mower (the brigade's, not the mower's).
  /// @return The count, which may exceed the herd when the chairman's
  ///         standing orders put more men on the plough than the accountant
  ///         did. The pool left is nought then.
  /// @note NOT EXACTLY the planner's arithmetic, and never on the dangerous
  ///       side: PlanDayAssignments takes a meadow's horse before it fills the
  ///       crew, so a meadow nobody could reach holds a horse that stands idle;
  ///       nobody stands on it here, and the top-up may put that horse to work
  ///       (static review of 0.34.51).
  ///
  /// The count is work_seam.h's CountHarness since 0.37.2: the herd day
  /// pays the oats and books the traction off the same one.
  static std::uint32_t HorsesInTraces(const WorldState& current) {
    return CountHarness(current).in_traces;
  }

  /// The holder's morning (manual/74-posts.md §4): he is out of the
  /// accountant's pool entirely, and he stands first on the work of his OWN
  /// unit — for the groom, the yard's herd care. It is done BEFORE the
  /// accountant runs, which is what "first" means here: the seam he starts
  /// draining is the same one the accountant may send others to, and both
  /// drain it hour by hour in row order.
  ///
  /// A post whose work the core does not model yet — the storekeeper, the
  /// timekeeper, every line of the roster but this one — leaves its holder
  /// reserved and IDLE. That is a STUB named by the table rather than by
  /// code: the day such a unit grows work the core counts, this same loop
  /// puts him on it. It is also silent: no event, no journal line, no alarm
  /// (boss's condition of 2026-09-03), because being reserved is what the
  /// player asked for.
  ///
  /// A MODULE'S WORK IS THE PARENT'S POST HOLDERS' WORK (timber design §8б,
  /// boss 2026-09-13): the sawmill has no post of its own, it takes the
  /// yard's craftsmen, no more than its places at once, and on those days the
  /// craftsman neither forges nor splits. Barn care still comes first — a
  /// herd eats today, a log pile waits. No work on a day off, like every
  /// other windowless work; barn care is the one exception, as it always was.
  void AssignPostHolders(WorldState& current) const {
    const bool day_off = IsDayOffIn(current, current.calendar.day);
    std::vector<std::uint32_t> places_taken(current.units.rows.size(), 0);
    for (ResidentRow& resident : current.residents.rows) {
      if (resident.post.profession.value == kInvalidDefIdValue ||
          OffWork(resident, current.calendar.tick)) {
        continue;  // no post, or its holder is in the district or waits for its car
      }
      for (std::uint32_t row = 0; row < current.herds.rows.size(); ++row) {
        const HerdRow& herd = current.herds.rows[row];
        if (herd.unit.value != resident.post.unit.value || herd.care_days_remaining <= 0.0F) {
          continue;
        }
        resident.work.kind = WorkKind::kHerdCare;
        resident.work.herd = current.herds.row_ids[row];
        resident.work.travel_hours = -1.0F;
        break;
      }
      if (resident.work.kind != WorkKind::kNone || day_off) {
        continue;
      }
      PutOnModuleWork(current, resident, places_taken);
    }
  }

  /// THE WORK THAT OPENED AFTER THE MORNING, for the post holders (boss,
  /// host-econ-shops seq 31): production's daily block runs after labor's
  /// hour 0, and the smokehouse writes its demand there, from the morning's
  /// slaughter (OpenSameDayShops). A holder left idle at hour 0 is placed now,
  /// before sunrise, on a module with work and a free place — as TopUpDay does
  /// for the accountant's pool. The places already taken are counted first.
  void PlaceIdleHoldersOnModules(WorldState& current) const {
    if (IsDayOffIn(current, current.calendar.day)) {
      return;
    }
    std::vector<std::uint32_t> places_taken(current.units.rows.size(), 0);
    for (const ResidentRow& resident : current.residents.rows) {
      if (resident.work.kind != WorkKind::kUnitWork) {
        continue;
      }
      const std::uint32_t row = FindRow(current.units, resident.work.unit);
      if (row != kNoRow) {
        ++places_taken[row];
      }
    }
    for (ResidentRow& resident : current.residents.rows) {
      if (resident.post.profession.value == kInvalidDefIdValue ||
          OffWork(resident, current.calendar.tick)) {
        continue;  // no post, or its holder is off work (district_car.h)
      }
      if (resident.work.kind == WorkKind::kNone) {
        PutOnModuleWork(current, resident, places_taken);
        continue;
      }
      // On a module that keeps (the sauerkraut shop) and the smokehouse's
      // meat opened overnight: he moves, before sunrise, so nothing of his
      // day is lost — the cabbage waits, the meat does not.
      if (resident.work.kind != WorkKind::kUnitWork) {
        continue;
      }
      const std::uint32_t from = FindRow(current.units, resident.work.unit);
      if (from == kNoRow || IsSameDayModule(current.units.rows[from].type)) {
        continue;
      }
      const WorkAssignment before = resident.work;
      if (PutOnModuleWork(current, resident, places_taken, true)) {
        --places_taken[from];
      } else {
        resident.work = before;
      }
    }
  }

  /// The first module of the holder's unit, in row order, that has work
  /// today and a free place. Whether it can work at all is asked of the same
  /// seam the working hour drains (WorkSeamOf): a paused sawmill or a yard
  /// that fell still answers nullptr there, and one rule serves both.
  ///
  /// THE SAME-DAY SHOPS FIRST (boss, host-econ-shops seq 31): a module whose
  /// input spoils very fast (WorksTheSameDay — the smokehouse's meat) is
  /// served before the rest; the sauerkraut keeps, the meat does not.
  /// `same_day_only` asks for those alone. Returns whether he was placed.
  bool PutOnModuleWork(WorldState& current,
                       ResidentRow& resident,
                       std::vector<std::uint32_t>& places_taken,
                       bool same_day_only = false) const {
    if (resident.post.unit.value == kInvalidEntityIdValue) {
      return false;
    }
    for (const bool same_day_pass : {true, false}) {
      if (!same_day_pass && same_day_only) {
        break;
      }
      for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
        const UnitRow& unit = current.units.rows[row];
        // The sawmill's places or a shop's (processing_catalog.h): a type is
        // one or the other, so the larger is the one it has.
        const std::uint32_t places = std::max(UnitWorkPlaces(config_.timber, unit.type),
                                              ProcessingPlaces(config_.processing, unit.type));
        if (unit.parent.value != resident.post.unit.value || places_taken[row] >= places ||
            IsSameDayModule(unit.type) != same_day_pass) {
          continue;
        }
        WorkAssignment work;
        work.kind = WorkKind::kUnitWork;
        work.unit = current.units.row_ids[row];
        const float* const seam = WorkSeamOf(current, work);
        if (seam == nullptr || *seam <= 0.0F || !ReachesForADay(current, resident, work)) {
          continue;
        }
        resident.work = work;
        ++places_taken[row];
        return true;
      }
    }
    return false;
  }

  /// Whether a module of `type` works a recipe whose input spoils very fast
  /// (processing_catalog.h, WorksTheSameDay).
  bool IsSameDayModule(UnitTypeId type) const {
    return std::ranges::any_of(config_.processing.recipes, [&](const ProcessingRecipe& recipe) {
      return recipe.unit_type.value == type.value && WorksTheSameDay(config_.processing, recipe);
    });
  }

  /// Whether the holder walks to `work` today by the accountant's own road
  /// rule (RoadLeavesAWorkingDay; boss, host-econ-shops seq 13, production
  /// units §8а «Мастер цеха и дорога»): a post is no licence to walk further
  /// than a work order would send him, or out into a day the road eats. He
  /// stays home instead, and the shop says why (kProcessingStopped).
  bool ReachesForADay(const WorldState& current,
                      const ResidentRow& resident,
                      const WorkAssignment& work) const {
    Vec2 home;
    Vec2 place;
    if (!HomePosition(current, resident.family, home) || !WorkPlace(current, work, place)) {
      return false;
    }
    // On foot, by the way there is (road_route.h; 0.36.2).
    const float travel =
        RoadKm(current, TravelMode::kWalk, home, place) * HoursPerKm(config_, WorkKind::kHarvest);
    return RoadLeavesAWorkingDay(travel,
                                 current.weather.daylight_hours,
                                 config_.travel_limit_hours,
                                 config_.min_usable_hours);
  }

  /// Barn work is a daily quantity: the yearly norm of the kind spread over
  /// the year and multiplied by the adult head count. A herd at a family
  /// yard raises no kolkhoz job at all — its care is the owner's leak
  /// (livestock design §5).
  void RefillHerdCare(WorldState& current) const {
    for (HerdRow& herd : current.herds.rows) {
      herd.care_days_remaining = 0.0F;
      if (herd.unit.value == kInvalidEntityIdValue ||
          herd.kind.value >= config_.care_days_per_year.size()) {
        continue;
      }
      herd.care_days_remaining = config_.care_days_per_year[herd.kind.value] *
                                 static_cast<float>(herd.adult_count) /
                                 static_cast<float>(kDaysPerYear);
    }
  }

  /// Whether the rain is holding field work today: a field with work left in
  /// a phase the rain stops (CollectJobs below does not offer it). The idle
  /// of such a day is the weather's (IdleReason::kRain).
  static bool RainHoldsFieldWork(const WorldState& current) {
    return std::ranges::any_of(current.fields.rows, [&current](const FieldRow& field) {
      const WorkKind kind = KindOfPhase(field.phase);
      return kind != WorkKind::kNone && field.work_days_remaining > 0.0F &&
             RainStopsWork(current.weather.precipitation, kind);
    });
  }

  /// The reason the book and the workbook keep for an idle man of the
  /// morning's plan: a day off answers for everything but a contradiction of
  /// the plan (static review of 0.36.32); then the rain, for the three
  /// reasons that only say «there was no work for him» (0.37.75).
  static IdleReason BookedIdleReason(IdleReason planned, bool day_off_today, bool rain_holds) {
    if (planned == IdleReason::kUnexplained) {
      return planned;
    }
    if (day_off_today) {
      return IdleReason::kDayOff;
    }
    const bool no_work_for_him = planned == IdleReason::kNoOpenWork ||
                                 planned == IdleReason::kWorkCovered ||
                                 planned == IdleReason::kCrewCap;
    return rain_holds && no_work_for_him ? IdleReason::kRain : planned;
  }

  /// @brief THE JOBS' RUNS OF MORNINGS WITH NO HAND (task 427, item 2;
  ///        unmanned_run_state.h): every job of this morning with work left
  ///        and nobody placed on it by `plan` (empty: nobody was placed at
  ///        all) carries its run one morning on, or starts one; every other
  ///        run ends. On the kWorkGotNoHandsDays-th morning kWorkGotNoHands is
  ///        said, once a run. A day off changes nothing.
  /// @param shortfall The plan's diagnosis by job (empty: kNoHands for all).
  static void TrackUnmannedRuns(WorldState& current,
                                const std::vector<AssignmentJob>& jobs,
                                const std::vector<std::uint32_t>& plan,
                                const std::vector<std::optional<JobShortfall>>& shortfall,
                                bool day_off_today) {
    if (day_off_today) {
      return;
    }
    std::vector<std::uint8_t> manned(jobs.size(), 0);
    for (const std::uint32_t job : plan) {
      if (job != kNoJobAssigned && job < jobs.size()) {
        manned[job] = 1;
      }
    }
    const auto same = [](const UnmannedRunRow& run, const AssignmentJob& job) {
      return run.kind == job.kind && run.field.value == job.field.value &&
             run.herd.value == job.herd.value && run.unit.value == job.unit.value &&
             run.stand.value == job.stand.value &&
             run.extraction_site.value == job.extraction_site.value &&
             run.limit_delivery.value == job.limit_delivery.value &&
             run.road_work.value == job.road_work.value;
    };
    UnmannedRuns runs;
    for (std::size_t index = 0; index < jobs.size(); ++index) {
      const AssignmentJob& job = jobs[index];
      if (manned[index] != 0 || !(job.work_days_remaining > 0.0F)) {
        continue;
      }
      UnmannedRunRow run;
      for (const UnmannedRunRow& before : current.unmanned_runs) {
        if (same(before, job)) {
          run = before;
          break;
        }
      }
      run.kind = job.kind;
      run.field = job.field;
      run.herd = job.herd;
      run.unit = job.unit;
      run.stand = job.stand;
      run.extraction_site = job.extraction_site;
      run.limit_delivery = job.limit_delivery;
      run.road_work = job.road_work;
      run.days = run.days < std::numeric_limits<std::uint16_t>::max()
                     ? static_cast<std::uint16_t>(run.days + 1U)
                     : run.days;
      if (run.days >= kWorkGotNoHandsDays && run.said == 0) {
        run.said = 1;
        const JobShortfall why = index < shortfall.size() && shortfall[index]
                                     ? *shortfall[index]
                                     : JobShortfall::kNoHands;
        SimEvent& event = EmitEvent(current, EventKind::kWorkGotNoHands, EventSeverity::kNotable);
        event.amount = static_cast<std::int64_t>(run.days) |
                       (static_cast<std::int64_t>(job.kind) << 32) |
                       (static_cast<std::int64_t>(why) << 40);
        event.unit = job.unit;
        event.field = job.field;
        event.herd = job.herd;
        event.stand = job.stand;
      }
      runs.push_back(run);
    }
    current.unmanned_runs = std::move(runs);
  }

  /// Whether a booked idle reason is a JOB'S SHORTFALL — the man was free and
  /// fit, and the work that wanted him was held by a horse, a brigade or the
  /// road (kHandsIdleWithWorkWaiting; task 427). A day off, the rain, no work
  /// or the work covered say that nothing waited for him.
  static bool IdleForAJobsShortfall(IdleReason reason) {
    return reason == IdleReason::kNoHorse || reason == IdleReason::kCrewCap ||
           reason == IdleReason::kRoad || reason == IdleReason::kHorseLock;
  }

  /// @brief Says kHandsIdleWithWorkWaiting when the morning's booked reasons
  ///        left hands idle for a job's shortfall and a job stood short —
  ///        the caller asks it only on the first morning of such a run.
  /// The job named is the first left short in the allocator's own order: its
  /// placement tier, then the job list's order. Reads the reasons as booked
  /// on the residents this morning (the rain and a day off already answered).
  static void SayHandsIdleWithWorkWaiting(WorldState& current,
                                          const std::vector<AssignmentJob>& jobs,
                                          const std::vector<AssignmentCandidate>& candidates,
                                          const PlacementDiagnosis& diagnosis) {
    std::uint32_t idle = 0;
    for (const AssignmentCandidate& candidate : candidates) {
      idle += IdleForAJobsShortfall(current.residents.rows[candidate.resident_row].idle_reason)
                  ? 1U
                  : 0U;
    }
    std::size_t named = jobs.size();
    for (std::size_t index = 0; index < jobs.size(); ++index) {
      if (diagnosis.shortfall[index] &&
          (named == jobs.size() || PlacementTier(jobs[index]) < PlacementTier(jobs[named]))) {
        named = index;
      }
    }
    if (idle == 0 || named == jobs.size()) {
      return;
    }
    const AssignmentJob& job = jobs[named];
    SimEvent& event = EmitEvent(current, EventKind::kHandsIdleWithWorkWaiting);
    event.amount = static_cast<std::int64_t>(idle) | (static_cast<std::int64_t>(job.kind) << 32) |
                   (static_cast<std::int64_t>(*diagnosis.shortfall[named]) << 40);
    event.unit = job.unit;
    event.field = job.field;
    event.herd = job.herd;
    event.stand = job.stand;
  }

  /// The day's openings: fields in a working phase and barns with care
  /// left. On a day off only the barn is served — animals eat on Sundays
  /// too (manual/65-labor-model.md §5).
  std::vector<AssignmentJob> CollectJobs(const WorldState& current) const {
    const bool day_off = IsDayOffIn(current, current.calendar.day);
    std::vector<AssignmentJob> jobs;
    if (!day_off) {
      for (std::uint32_t row = 0; row < current.fields.rows.size(); ++row) {
        const FieldRow& field = current.fields.rows[row];
        const WorkKind kind = KindOfPhase(field.phase);
        if (kind == WorkKind::kNone || field.work_days_remaining <= 0.0F) {
          continue;
        }
        // RAIN STOPS THE SOWING AND THE REAPING (farming design §5, «Погода
        // останавливает работу, а не портит её»; core_common/
        // rain_stops_work.h). The job is not offered at all, rather than
        // offered and left undrainable: a crew sent to a rained-out field
        // would read as men with nothing to work with (kBlocked) all day,
        // and it is the weather, not a shortage the chairman can mend. The
        // hands go to what rain does not stop — the carting above all.
        if (RainStopsWork(current.weather.precipitation, kind)) {
          continue;
        }
        // THE AUTUMN FURROW STOPS ON FROZEN GROUND («Надо успеть до мёрзлой
        // земли», farming design; static review of 0.37.18): not offered on a
        // day below nought — the gate it opened by — and taken up again on a
        // thaw. Opened and worked on through the frosts, the December's work
        // went into a furrow the turn then let go. One home with the
        // chairman's standing order (FrostStopsFieldWork).
        if (FrostStopsFieldWork(field, current.weather.air_temperature_celsius)) {
          continue;
        }
        AssignmentJob job;
        job.kind = kind;
        job.field = current.fields.row_ids[row];
        job.position = field.center;
        job.work_days_remaining = field.work_days_remaining;
        job.window = FieldWindow(current.calendar, field, kind);
        if (config_.standing_crop_grams && InSnowLastDays(current.calendar, field, kind)) {
          job.grams_at_risk = config_.standing_crop_grams(current, field);
        }
        // WHAT THE SNOW WILL TAKE, IN FOOD, ON EVERY DAY OF THE REAPING (the
        // harvest rule 2; AssignmentJob::kcal_at_risk; 0.37.83): the queue
        // orders the reapings by it from the first ripe day, not in the last
        // three. An annual on the arable only — the meadow has no snow edge
        // and a winter crop or a perennial keeps its own window.
        if (config_.standing_crop_grams && kind == WorkKind::kHarvest &&
            field.kind == LandKind::kArable && field.crop.value < config_.crops.size() &&
            config_.crops[field.crop.value].ripen_days > 0) {
          job.kcal_at_risk = static_cast<float>(config_.standing_crop_grams(current, field)) *
                             config_.crops[field.crop.value].kcal_per_gram;
        }
        job.prepares_winter_crop = PreparesWinterCrop(field, kind, current.calendar.day);
        job.winter_window_closing =
            job.prepares_winter_crop && WinterWindowClosing(current, field, kind, job.window);
        job.plan_position = IsHorseWork(kind) && CarriesPlanPosition(current, field, kind);
        // THE ZYAB HAS NO WINDOW OF ITS OWN (register 13; boss-
        // core-epoch1-queue-2026-09-29 [22]; 0.37.18): read off the field's
        // slot, the stubble's window would be the crop just reaped — past, so
        // overdue, ahead of the carting. It is the first of the jobs with no
        // window instead (assignment.cpp, `autumn_furrow`), and no plan's
        // plough and no winter crop's preparation. One left part-turned by
        // the turn is idle over the winter and offers no job; the spring's
        // ploughing opens it (OpenPlowing, option «г»).
        if (field.autumn_furrowing != 0) {
          job.window = DeadlineNotApplicable();
          job.autumn_furrow = true;
          job.plan_position = false;
          job.prepares_winter_crop = false;
        }
        // THE THIRD TIER IS NOT WIRED, AND THE REASON IS MEASURED. The rule
        // asked for is "an overdue sowing is not offered at all" — seed put
        // in after the window does not ripen (boss, 2026-09-12). Written as
        // `kind == kSowing && window.kind == kOverdue`, it cost the first
        // year NINE TENTHS OF ITS SOWING: 6.2 game man-days against 56, the
        // harvest 197 against 280, five runs red.
        //
        // The reason is that the crop's `sow_to_month` is the window for
        // OPENING the work, not for finishing it: ploughing and harrowing
        // come first, so a field that opened in time routinely arrives at
        // the sowing phase after the month has turned. The model has always
        // let it finish — labor_year measures the overrun and prints it —
        // and cutting the crew off at the month's edge abandons a crop that
        // is all but in the ground. The tier needs a rule about the PHASE
        // that was opened in time, not about the calendar alone; with boss.
        // The meadow cut rides out: horse mower, horse rake, hay carted home
        // (farming design §5; the start canon issues both implements). The
        // crop harvest stays hand work — sickles and scythes on the strips.
        job.harnessed =
            field.kind == LandKind::kMeadow || field.kind == LandKind::kFloodplainMeadow;
        // THE REAPING OF THE ARABLE AND THE SOWING GO OUT ON ONE CART WHEN A
        // HORSE IS FREE (AssignmentJob::brigade_cart; farming design §6,
        // «Дорога пешком съедает световой день»; 0.37.89). The cart is out
        // already when a driver stands on the field from the morning — the
        // top-up's hands ride with him and take no second horse.
        job.brigade_cart = RidesTheBrigadesCart(kind, field.kind);
        job.cart_out = job.brigade_cart && BrigadeCartIsOut(current, kind, job.field);
        jobs.push_back(job);
      }
    }
    // Hauling (task A4): a field with a load lying on it wants carriers, and
    // it wants them whatever season it is — the load does not ripen, it just
    // sits there getting rained on.
    //
    // The demand was sized by PRODUCTION at the end of yesterday
    // (SettleHauling, core_common/haul.h). Labor does none of that
    // arithmetic: it reads the seam like any other and drains it with real
    // people, and production converts what was drained back into grain. One
    // owner for the price of a trip, and it is not this module.
    if (!day_off) {
      // Asked of production once a collection, and only with a meadow's heap
      // lying (StoredHayShortWithin walks the herds for each day ahead).
      std::optional<bool> hay_short;
      std::optional<bool> mowing_left;
      std::optional<bool> hay_short_soon;
      std::optional<bool> hay_out_tomorrow;
      std::uint16_t hay_days_asked = 0;
      for (std::uint32_t row = 0; row < current.fields.rows.size(); ++row) {
        const FieldRow& field = current.fields.rows[row];
        // Carrying has a seam of its own (land_state.h), so a field may be
        // ploughed and cleared at once — different crews on the same ground,
        // which the canon's rotation needs: oats come off in the eighth
        // month and winter rye goes in in the eighth.
        if (field.reaped_grams <= 0 || field.haul_days_remaining <= 0.0F) {
          continue;
        }
        AssignmentJob job;
        job.kind = WorkKind::kHauling;
        job.field = current.fields.row_ids[row];
        job.position = field.center;
        job.work_days_remaining = field.haul_days_remaining;
        // With a horse if the settlement has one to spare — and then the
        // placement takes it out of the day's pool, exactly as ploughing
        // does. The cart is the horse (boss, 2026-09-03).
        job.harnessed = DraughtHorses(current) > 0;
        // Urgency is the load's own: before the snow it is the most urgent
        // thing in the village, in June it can wait. The window of the crop
        // that is lying there says which.
        //
        // ARABLE LAND ONLY, since 0.37.128: the hay mown at a meadow lies in
        // its stacks and the snow does not take it; with the year's-end
        // window its carts outranked the fallow for this autumn's rye, as the
        // stand's logs had (0.36.19). On 0.37.127 the ploughing moved from
        // August-September to October-November and the rye's plan rows failed
        // 71 times in twenty years on nine villages (6 before).
        //
        // AND BY THE MANGER'S NEED, since 0.37.132 (LaborConfig::
        // hay_cart_need_days; boss, boss-all-carts-carry-people-go-2026-10-02
        // [51]): with the stores short of the days of hay ahead the meadow's
        // heap has the field load's window after all; with the days in store
        // it has none and is the last of the carts with none (stacked_hay).
        // Neither calendar rank held: before the logs the building fell by a
        // quarter (0.37.128), after them the stores stood empty in the spring
        // and the horses fell from 40 to 24 (0.37.129).
        const bool meadow_heap = field.kind != LandKind::kArable;
        if (meadow_heap && !hay_short.has_value()) {
          hay_short = config_.hay_cart_need_days > 0 && config_.stored_hay_short &&
                      config_.stored_hay_short(current, config_.hay_cart_need_days);
        }
        // THE RULE AND ITS ONE EXCEPTION, IN ONE SENTENCE: a meadow's stack
        // never outranks an open cut's mowing - EXCEPT when the stores' hay
        // will not last to the next morning a cart can still mend
        // (`hay_cart_urgent_days`, below: then it goes ahead of ALL work).
        //
        // AND NEVER AHEAD OF AN OPEN CUT'S MOWING (0.37.252; boss, core-boss-v3-
        // feed-contract-2026-10-14 [6] (b), [8]): hay stacked on a meadow does
        // not rot and the snow does not take it - it waits, the grass does
        // not. While any meadow still has mowing left the stack's carting has
        // no window, short stores or not. With the stores short it took a
        // field load's window and stood above the cut: in August of year 1,
        // with a cart of hay at 1.08 t, the heaps drew 49 worker-days more and
        // the scythes lost 28 - 67 t of hay never laid, a village at -6.8 %.
        // A herd underfed still raises the load's task to level 0, and that
        // is placed ahead of everything.
        if (meadow_heap && !mowing_left.has_value()) {
          mowing_left = std::ranges::any_of(current.fields.rows, [](const FieldRow& meadow) {
            return meadow.kind != LandKind::kArable && meadow.phase == FieldPhase::kHarvest &&
                   meadow.work_days_remaining > 0.0F;
          });
        }
        // BUT NOT BESIDE A HUNGRY HERD (LaborConfig::hay_cart_in_cut_days):
        // with the stores short within those few days the stack is carted
        // ahead of the mowing after all.
        if (meadow_heap && mowing_left.value_or(false) && !hay_short_soon.has_value()) {
          hay_short_soon = hay_short.value_or(false) && config_.hay_cart_in_cut_days > 0 &&
                           config_.stored_hay_short(current, config_.hay_cart_in_cut_days);
        }
        const bool waits =
            meadow_heap && (!hay_short.value_or(false) ||
                            (mowing_left.value_or(false) && !hay_short_soon.value_or(false)));
        job.window = waits ? DeadlineNotApplicable() : HaulWindow(current);
        job.stacked_hay = waits;
        // AND URGENT BEFORE A HERD GOES UNDERFED (0.37.252, routing stage В3's
        // form (c); LaborConfig::hay_cart_urgent_days): with the stores'
        // hay under that many days of the herds' ration the stack's carting
        // goes ahead of all work this morning, the mowing included. Until it
        // the stack's last resort was «a herd underfed raises the load's task
        // to level 0» - a level set in logistics' day, after the morning's
        // placement and after the hungry morning: in year 3's spring three
        // villages of nine went 21 t short with 37-88 t of hay on their
        // meadows. Set HERE and kept by MarkTheUrgentLoads: the task's row
        // takes no part in it and names no level of its own for it.
        //
        // THE DAYS ARE COUNTED TO THE NEXT MORNING A CART CAN STILL MEND: this
        // morning's feeding (it comes after this placement), the rule's days,
        // and every day off that follows tomorrow in a row - nothing is carted
        // on those. Asked about the rule's one day alone, the stores answered
        // «not short» on the morning they held exactly today's ration, the
        // rule fired at the hour-1 top-up when the horses were out, and two
        // days off stood behind it (form (c)'s first pair, seed 1934, year 3:
        // fired on day 14 at one o'clock, carted on day 17, the herds underfed
        // on 15, 16 and 17 with 94 t on the meadow).
        //
        // ASKED A RICK AT A TIME since 0.37.255 (`short_ricks`; routing stage
        // В3, step two - each farm eats from its own rick): some farm's rick
        // short within those days is the rule's «yes», whatever the village's
        // sum holds - the other farm's full rick is not at this manger. The
        // village's own answer stays beside it, for the herds bound to no
        // rick; a labour built without the ricks' question asks it alone.
        if (meadow_heap && !hay_out_tomorrow.has_value() &&
            (hay_short.value_or(false) || config_.short_ricks)) {
          // One count with production's choice of the rick (day_off.h).
          const std::uint32_t days_to_mend =
              UrgentHayHorizonDays(current, config_.hay_cart_urgent_days);
          const bool rule_on = config_.hay_cart_urgent_days > 0;
          const bool some_rick_short =
              rule_on && config_.short_ricks && !config_.short_ricks(current, days_to_mend).empty();
          hay_out_tomorrow = some_rick_short || (rule_on && hay_short.value_or(false) &&
                                                 config_.stored_hay_short(current, days_to_mend));
          hay_days_asked = static_cast<std::uint16_t>(days_to_mend);
        }
        job.logistics_urgent = meadow_heap && hay_out_tomorrow.value_or(false);
        if (job.logistics_urgent) {
          job.stacked_hay = false;
          job.urgent_by_stores_days = hay_days_asked;  // for the day's one event
        }
        jobs.push_back(job);
      }
    }
    // The timber stands (timber design §8a, 2026-09-13): felling, windowless
    // and capped by the tools in the stores, and the carting of the logs lying
    // there, by a field's load's arithmetic — though since 0.36.19 not at its
    // rank: windowless, below the rye's fallow (see HaulWindow).
    if (!day_off) {
      const std::uint32_t crew_cap =
          FellingCrewCap(config_.timber, TotalHeld(current, config_.timber.tool_resource));
      for (std::uint32_t row = 0; row < current.stands.rows.size(); ++row) {
        const TimberStandRow& stand = current.stands.rows[row];
        // NOT FELLED WHAT CANNOT BE CARTED OUT (0.36.29; boss [69], «не
        // рубить то, что не вывезти»): the felling is offered only where the
        // log cart reaches within the road limit, by the network. A third of
        // the forest in the team's reach was past the log cart's, and the
        // fellers were sent where the logs then lay. A dirt road laid there
        // brings the stand back by itself; kFellingUnreachable says why it
        // stands (timber_felling.cpp, the same road).
        // AND ONLY WHERE A FELLER CAN BE SENT TODAY (0.37.208; home_reach.h,
        // HandReachToday — the placement's own question): on foot, or by a
        // ride while the kolkhoz has a horse. Until then the log cart's reach
        // was the whole offer, the fellers walk (routing stage A4), and a
        // stand past the walk was offered every morning to nobody.
        const bool carted_out =
            stand.marked_m3 > 0.0F && FellingCanBeManned(current, stand.position);
        if (stand.marked_m3 > 0.0F && stand.work_days_remaining > 0.0F && crew_cap > 0 &&
            carted_out) {
          AssignmentJob job;
          job.kind = WorkKind::kFelling;
          job.stand = current.stands.row_ids[row];
          job.position = stand.position;
          job.work_days_remaining = stand.work_days_remaining;
          job.max_crew = static_cast<std::uint8_t>(crew_cap);
          jobs.push_back(job);
        }
        // A planting still to be planted (timber_planting.h): windowless,
        // uncapped by tools — a sapling and a spade (map design §7).
        if (stand.kind == TimberStandKind::kPlanted && stand.planted_day == kNeverPlanted &&
            stand.work_days_remaining > 0.0F) {
          AssignmentJob job;
          job.kind = WorkKind::kPlanting;
          job.stand = current.stands.row_ids[row];
          job.position = stand.position;
          job.work_days_remaining = stand.work_days_remaining;
          jobs.push_back(job);
        }
        if (stand.load_grams > 0 && stand.haul_days_remaining > 0.0F) {
          AssignmentJob job;
          job.kind = WorkKind::kHauling;
          job.stand = current.stands.row_ids[row];
          job.position = stand.position;
          job.work_days_remaining = stand.haul_days_remaining;
          job.harnessed = DraughtHorses(current) > 0;
          // WINDOWLESS SINCE 0.36.19, as the district's lot (0.36.18): logs
          // lying at the felling do not spoil, and the fallow for this
          // autumn's rye goes first (parcel 233). With the year's-end window
          // (HaulWindow) the stand's carts took 27-30 horses of 30 in the
          // last days of September while the rye's harrowing waited with 0-3
          // (seed 1934, year 5, f11): the sowing never opened, the slot was
          // lost (278). Seven lost autumns on the canon's nine seeds -> one
          // (boss-core-epoch1-resume [39]-[40]).
          jobs.push_back(job);
        }
      }
    }
    // The extraction sites (construction design §3; boss, parcel 270): the
    // stands' two jobs on the site's row — digging, windowless and capped by
    // the tools in the stores, and the carting of what lies dug.
    if (!day_off) {
      const std::uint32_t crew_cap =
          DiggingCrewCap(config_.extraction, TotalHeld(current, config_.extraction.tool_resource));
      for (std::uint32_t row = 0; row < current.extraction_sites.rows.size(); ++row) {
        const ExtractionSiteRow& site = current.extraction_sites.rows[row];
        if (site.marked_grams > 0 && site.work_days_remaining > 0.0F && crew_cap > 0) {
          AssignmentJob job;
          job.kind = WorkKind::kExtraction;
          job.extraction_site = current.extraction_sites.row_ids[row];
          job.position = site.position;
          job.work_days_remaining = site.work_days_remaining;
          job.max_crew = static_cast<std::uint8_t>(crew_cap);
          jobs.push_back(job);
        }
        if (site.load_grams > 0 && site.haul_days_remaining > 0.0F) {
          AssignmentJob job;
          job.kind = WorkKind::kHauling;
          job.extraction_site = current.extraction_sites.row_ids[row];
          job.position = site.position;
          job.work_days_remaining = site.haul_days_remaining;
          job.harnessed = DraughtHorses(current) > 0;
          // Windowless since 0.36.19, for the stand's reason above: dug
          // stone, sand and clay do not spoil either.
          jobs.push_back(job);
        }
      }
      // THE PEREVALKA (kEmptyStore; start §5): the carting out of a store
      // being emptied, on the same carts and hands, and WINDOWLESS — below
      // the harvest's carting by default (boss seq 116-117). A paused unit
      // asks for nobody; its order stands.
      for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
        const UnitRow& unit = current.units.rows[row];
        if (unit.emptying != 1 || !(unit.haul_days_remaining > 0.0F)) {
          continue;
        }
        AssignmentJob job;
        job.kind = WorkKind::kHauling;
        job.unit = current.units.row_ids[row];
        job.position = unit.position;
        job.work_days_remaining = unit.haul_days_remaining;
        job.harnessed = DraughtHorses(current) > 0;
        jobs.push_back(job);
      }
      // THE SUPPLY OF A WORKS (routing stage V2; transport §12): the carting
      // of the materials fixed for a site, from their sender to it — on the
      // same carts and hands. DUE BY A DATE (level 1, «срок»): its window is
      // the days left of LaborConfig::site_supply_term_days from the day the
      // materials were fixed. SINCE #561 (0.37.225) the window orders it only
      // INSIDE the works' own tier (PlacementTier: supplies_works -> the
      // windowless tier), ahead of the works it feeds; every job with an open
      // window, the overdue, the meadow's cut and the rye's fallow go first.
      // Until then the window put it among the open windows by its days, and
      // in year 1's June it took the mowing's hands (transport design «Подвоз
      // к стройке — на месте самой стройки, а не своего срока»).
      // NOT IN THE CORE (#561's second half, «дни, когда покос забрал
      // возчиков, не идут в срок стройки»): the core has no lamp or alarm on
      // this term yet - its one reader is the order above - so there is
      // nothing to exempt; the rule goes with the alarm when stage В6 brings
      // one. The carter's place is the sender, where he loads. A unit
      // being emptied is carted out first (work_seam.cpp): it is not offered
      // here until that is done.
      for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
        const UnitRow& unit = current.units.rows[row];
        if (unit.emptying != 0 || unit.paused != 0 ||
            !(unit.construction.supply_days_remaining > 0.0F) ||
            !AnythingAwaits(unit.construction)) {
          continue;
        }
        AssignmentJob job;
        job.kind = WorkKind::kHauling;
        job.unit = current.units.row_ids[row];
        job.position = unit.construction.supply_from;
        job.supplies_works = true;
        // THE FLOOR (#734; core_common/site_supply_floor.h): nothing delivered
        // for the rule's days - carters with horses ahead of the windows.
        job.supply_floor = OnSupplyFloor(config_.supply_floor, unit.construction);
        // LOGS BY TWO (#671): no lone back takes a log; pairs under the gate.
        const LogLeg log_leg = LogLegOf(current, unit);
        job.walker_log = log_leg.walker_log;
        job.log_pair_open = log_leg.open;
        job.log_pair_leg_m = log_leg.leg_m;
        // THE SITES' QUEUE (#595; core_common/site_supply_queue.h): the FIRST
        // site's supply ranks first inside the windowless tier - the same key
        // a raised site's own job carries - and the next site's hands come
        // from what the first cannot use today. Every site keeps its offer.
        job.raised = SiteSupplyRank(current, row) == 0;
        job.log_cart = unit.construction.supply_log_way != 0;
        const SimDay since = unit.construction.supply_since_day;
        const SimDay waited = current.calendar.day > since ? current.calendar.day - since : 0;
        // THE TERM BY THE WORKS' OWN LOAD (econ, core-boss-c2-site-supply-
        // 2026-10-09 [19]-[20]): the flat term up to twice its number of
        // cart-days, and above that half the load's cart-days — a works of
        // thirty cart-days held to the eight days of a works of two was late
        // by the rule's own making (pair 4: 85.4 % within the flat term,
        // worst wait 23 days). The load is the one production priced when
        // the materials were fixed (ConstructionState::supply_days_first).
        const SimDay term =
            SiteSupplyTermDays(config_.site_supply_term_days, unit.construction.supply_days_first);
        job.window = DeadlineInDays(waited < term ? static_cast<std::int32_t>(term - waited) : 0);
        // THE CREW IS SIZED BY THE WHOLE LOAD, every leg's (ConstructionState::
        // supply_days_all): the seam the carters drain is the front leg's,
        // the next is written the hour it is carted out, and they go on to
        // it — a crew sized by the front leg alone carted a leg a morning.
        job.work_days_remaining =
            std::max(unit.construction.supply_days_remaining, unit.construction.supply_days_all);
        job.harnessed = DraughtHorses(current) > 0;
        jobs.push_back(job);
      }
      // A RICK'S SUPPLY FROM ANOTHER RICK (#1056 C1.2; econ's stage C
      // contract 574c366): priced by production's evening while the rick's
      // herds' hay stands below its threshold (unit_state.h, RickSupplySeam).
      // Level 2, windowless - the common queue; LEVEL 0 UNDER TODAY'S URGENT
      // RULE, the stack's own (below, in the field loop): the rick short
      // within the days a cart can still mend is carted ahead of all work.
      // The carter's place is the rick it comes from, where he loads.
      std::optional<std::vector<ShortRickUnit>> ricks_short;
      for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
        const UnitRow& unit = current.units.rows[row];
        if (!RickSupplyStands(unit) || !(unit.rick_supply.days_remaining > 0.0F)) {
          continue;
        }
        const std::uint32_t from_row = FindRow(current.units, unit.rick_supply.from);
        if (from_row == kNoRow) {
          continue;
        }
        AssignmentJob job;
        job.kind = WorkKind::kHauling;
        job.unit = current.units.row_ids[row];
        job.position = current.units.rows[from_row].position;
        job.work_days_remaining = unit.rick_supply.days_remaining;
        job.harnessed = DraughtHorses(current) > 0;
        if (!ricks_short.has_value()) {
          ricks_short.emplace();
          if (config_.short_ricks && config_.hay_cart_urgent_days > 0) {
            *ricks_short = config_.short_ricks(
                current, UrgentHayHorizonDays(current, config_.hay_cart_urgent_days));
          }
        }
        job.logistics_urgent =
            std::ranges::any_of(*ricks_short, [&job](const ShortRickUnit& short_rick) {
              return short_rick.rick.value == job.unit.value;
            });
        jobs.push_back(job);
      }
      // THE TIMBER LOT AT THE DISTRICT CENTRE (decision 279, 0.36.17): the
      // village's own carts fetch it, WINDOWLESS since 0.36.18 — below the
      // fallow for this autumn's rye (parcel 233: it yields to every job with
      // a window of its own). A lot at the district is not bread in the field
      // and does not spoil; the horse goes for it when no field holds it.
      // 0.36.17 gave it a stand's haul window (the year's end), and on the
      // canon all sixteen horses stood at the district every working day of
      // July-September while the rye's fallow waited with no crew. With the
      // run's chairman buying no faster than the lots are fetched, the window
      // still failed 38 plan years of 180, windowless 16, 0.36.16 6
      // (boss-core-epoch1-resume [36]-[37]).
      // The carter's place is the map's northern border end, where the
      // district's road begins (road_route.h, DistrictExitPoint).
      for (std::uint32_t row = 0; row < current.limit_deliveries.rows.size(); ++row) {
        const LimitDeliveryRow& lot = current.limit_deliveries.rows[row];
        // IN THE MUD NOBODY GOES TO THE DISTRICT (routing stage В5; district §4,
        // «не проходит ни свой транспорт, ни районный»): no own cart is offered
        // for a lot coming in or the plan's delivery going out on a mud day.
        if (lot.own_carts == 0 || lot.arrive_day > current.calendar.day ||
            !(lot.haul_days_remaining > 0.0F) || current.weather.mud) {
          continue;
        }
        AssignmentJob job;
        job.kind = WorkKind::kHauling;
        job.limit_delivery = current.limit_deliveries.row_ids[row];
        job.position = DistrictExitPoint(current);
        job.work_days_remaining = lot.haul_days_remaining;
        job.harnessed = DraughtHorses(current) > 0;
        jobs.push_back(job);
      }
    }
    for (std::uint32_t row = 0; row < current.herds.rows.size(); ++row) {
      const HerdRow& herd = current.herds.rows[row];
      Vec2 position;
      if (herd.care_days_remaining <= 0.0F || !HerdPosition(current, herd, position)) {
        continue;
      }
      AssignmentJob job;
      job.kind = WorkKind::kHerdCare;
      job.herd = current.herds.row_ids[row];
      job.position = position;
      job.work_days_remaining = herd.care_days_remaining;
      // Undone barn work expires tonight: an open window with nothing left in
      // it, which is what a zero under kDays means and the one honest use of
      // that zero (deadline.h).
      job.window = DeadlineInDays(0);
      jobs.push_back(job);
    }
    // THE MILK LEG (#1056 C1.1; boss, the #1056 thread [4]: «carrying the milk
    // away is part of milking»): the barn's milk to its store, OUTSIDE the
    // day-off guard as herd care is - on the same carts and hands as any
    // hauling, estimated by production's evening for the next morning's
    // placement and re-priced after the milking (herd_state.h,
    // leg_days_remaining). Its window closes tonight: the milk sours by the
    // morning.
    for (std::uint32_t row = 0; row < current.herds.rows.size(); ++row) {
      const HerdRow& herd = current.herds.rows[row];
      Vec2 position;
      if (!(herd.leg_days_remaining > 0.0F) || !HerdPosition(current, herd, position)) {
        continue;
      }
      AssignmentJob job;
      job.kind = WorkKind::kHauling;
      job.herd = current.herds.row_ids[row];
      job.position = position;
      job.work_days_remaining = herd.leg_days_remaining;
      // ON FOOT, the cans in hand (#1056 C1.3; herd_state.h, leg_on_foot):
      // no horse is asked where production priced a walk.
      job.harnessed = herd.leg_on_foot == 0 && DraughtHorses(current) > 0;
      job.window = DeadlineInDays(0);
      jobs.push_back(job);
    }
    // Construction sites (task A2). The seam is the same shape as a field's,
    // and so is this loop: core_construction sets labor_days_remaining when
    // a site starts and reads it back at zero; the two never call each other
    // (manual/65-labor-model.md §2). No calendar window — a site waits —
    // and the crew cap rides with the site so no table is opened here.
    if (!day_off) {
      for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
        const UnitRow& unit = current.units.rows[row];
        // A module whose parent does not stand sound is not built (unit
        // rules §11): its site keeps its seam and nobody is sent to it.
        // A PAUSED building or demolition asks for nobody (construction design
        // §6): this morning's crew worked out yesterday, and none is sent
        // today. The share done stays on the seam for the resume.
        // TAKING A MODULE DOWN ASKS NO PARENT (0.37.115): the rule is the
        // building's. Until then a module whose parent was gone could not be
        // demolished either — a site and a plot for ever (architecture §7ж³).
        const bool taking_down = unit.construction.phase == ConstructionPhase::kDemolishing;
        if (unit.construction.labor_days_remaining <= 0.0F ||
            (!taking_down && !ModuleParentSound(current, unit)) || unit.paused != 0) {
          continue;
        }
        // AND A SITE OF A CLASS THAT STANDS IN WINTER asks for nobody in the
        // core's winter season (construction design §8; rain_stops_work.h,
        // WinterStopsSite) — the pause's rule: nothing is lost, nobody sent.
        if (WinterStopsSite(current.calendar.season, unit.construction.winter_works)) {
          continue;
        }
        AssignmentJob job;
        job.kind = WorkKind::kConstruction;
        job.unit = current.units.row_ids[row];
        job.position = unit.position;
        job.work_days_remaining = unit.construction.labor_days_remaining;
        job.max_crew = unit.construction.max_crew;
        // The allocator's DEFAULT for the works that will house a herd facing
        // the cold (cold_housing.h). The player's explicit builders win over
        // it: his standing work orders override this morning's placement
        // after the accountant has made it (work_orders.h, ApplyStandingWork).
        job.houses_cold_herd = WorksHouseAColdHerd(config_.cold_housing, current, row);
        // «Поднято в очереди» (kRaiseSite; #595): first inside its tier.
        job.raised = unit.construction.raised != 0;
        jobs.push_back(job);
      }
      // THE PIECES OF ROAD UNDER WORK (7e; road_work_state.h): a site as a
      // unit's is — its labour, its brigade, no window, and the winter's rule
      // of its surface's level.
      for (std::uint32_t row = 0; row < current.road_works.rows.size(); ++row) {
        const RoadWorkRow& work = current.road_works.rows[row];
        if (work.labor_days_remaining <= 0.0F ||
            WinterStopsSite(current.calendar.season, work.winter_works)) {
          continue;
        }
        AssignmentJob job;
        job.kind = WorkKind::kRoadWork;
        job.road_work = current.road_works.row_ids[row];
        job.position = work.place;
        job.work_days_remaining = work.labor_days_remaining;
        job.max_crew = work.max_crew;
        jobs.push_back(job);
      }
    }
    MarkTheReapingsTheSnowWillTake(current, jobs);
    MarkTheUrgentLoads(current, jobs);
    DropPausedLoads(current, jobs);
    PriceTheWalkersShares(current, jobs);
    return jobs;
  }

  /// The hauling a job is, as the assignment its hands will carry.
  static WorkAssignment HaulingWorkOfJob(const AssignmentJob& job) {
    WorkAssignment work;
    work.kind = WorkKind::kHauling;
    work.field = job.field;
    work.stand = job.stand;
    work.extraction_site = job.extraction_site;
    work.limit_delivery = job.limit_delivery;
    work.unit = job.unit;
    // THE MILK LEG'S HERD (#1056 C1.1): without it the leg's job matched no
    // task, so its level 0 never marked it urgent (MarkTheUrgentLoads) - the
    // first arm's 28 / 15 / 24 % left in the barn in March, May and October.
    work.herd = job.herd;
    return work;
  }

  /// LOGS BY TWO, THE WORKS' SIDE (#671; core_common/haul.h, LogPairMayCarry):
  /// what a walker's next trip on a works' supply is to the last resort's gate.
  struct LogLeg {
    /// The next trip is a log from a pile: the front leg's first resource by
    /// id is the log and the leg is not a stand's own (log_way).
    bool walker_log = false;
    /// The works is delivering and the walk is within log_pair_max_leg_m -
    /// the gate's two terms that are the works' own; «no horse free» is the
    /// morning pool's (assignment.cpp) and the hour's (LogPairCarriesNow).
    bool open = false;
    /// The walk from the pile to the site, metres.
    float leg_m = 0.0F;
  };

  LogLeg LogLegOf(const WorldState& current, const UnitRow& unit) const {
    LogLeg leg;
    const ConstructionState& works = unit.construction;
    if (works.legs.empty() || works.legs.front().log_way != 0 ||
        config_.porter.log.value == kInvalidDefIdValue) {
      return leg;
    }
    const ResourceAmounts& grams = works.legs.front().grams;
    std::size_t first = 0;
    while (first < grams.size() && grams[first] <= 0) {
      ++first;
    }
    if (first != config_.porter.log.value) {
      return leg;
    }
    leg.walker_log = true;
    leg.leg_m =
        RoadKm(current, TravelMode::kWalk, works.legs.front().from, unit.position) * 1000.0F;
    leg.open = LogPairMayCarry(works.phase == ConstructionPhase::kDelivering,
                               /*draught_horse_free=*/false,
                               leg.leg_m,
                               config_.porter.log_pair_max_leg_m);
    return leg;
  }

  /// THE MORNING'S LOGS BY TWO, FOR THE YEAR'S BOOK (#671; YearLedger::
  /// log_pair_days, log_pair_worst_leg_m, log_pair_near_miss_days): a day
  /// counts once whatever its pairs. A NEAR MISS is a works that waited for a
  /// log with no horse for it this morning and the pile beyond the pair's
  /// walk - with firing at nought everywhere this row says which term held
  /// the gate shut (econ's porter page).
  void BookLogPairs(const std::vector<AssignmentJob>& jobs,
                    const std::vector<std::uint32_t>& plan,
                    const std::vector<std::uint8_t>& rides_horse,
                    const PlacementDiagnosis& diagnosis,
                    YearLedger& book) const {
    bool carried = false;
    bool near_miss = false;
    for (std::uint32_t index = 0; index < jobs.size(); ++index) {
      const AssignmentJob& job = jobs[index];
      if (!job.walker_log) {
        continue;
      }
      std::uint32_t on_foot = 0;
      for (std::size_t hand = 0; hand < plan.size(); ++hand) {
        on_foot +=
            plan[hand] == index && (hand >= rides_horse.size() || rides_horse[hand] == 0) ? 1U : 0U;
      }
      if (job.log_pair_open && on_foot >= 2) {
        carried = true;
        book.log_pair_worst_leg_m = std::max(book.log_pair_worst_leg_m, job.log_pair_leg_m);
      } else if (job.log_pair_leg_m > config_.porter.log_pair_max_leg_m &&
                 diagnosis.shortfall[index] == JobShortfall::kNoHorse) {
        near_miss = true;
      }
    }
    book.log_pair_days = static_cast<std::uint16_t>(book.log_pair_days + (carried ? 1U : 0U));
    book.log_pair_near_miss_days =
        static_cast<std::uint16_t>(book.log_pair_near_miss_days + (near_miss ? 1U : 0U));
  }

  /// One head of a pair's share of a cart-day under a log today: below 1
  /// while the settlement has carts, a whole day while it has none (the seam
  /// is then the pair's own - supply_haul.cpp, LegDays).
  float LogPairShareToday(const WorldState& current) const {
    if (!SettlementHasCarts(current, config_.horse_kind)) {
      return 1.0F;
    }
    const CartGear gear = CartGearToday(current, CartSpecsOf(config_));  // SL
    return LogPairShareOfCartDay(config_.porter,
                                 CartLoadGrams(config_.porter, gear.load_kg, config_.porter.log),
                                 HoursPerKm(config_, WorkKind::kHarvest),
                                 static_cast<float>(kClockScale) / gear.empty_kmh,
                                 static_cast<float>(kClockScale) / gear.loaded_kmh);
  }

  /// WHETHER THIS HAND IS ONE OF A PAIR UNDER A LOG THIS HOUR (#671): asked of
  /// a carrier on foot whose works' next trip is a log. The works' own terms
  /// (LogLegOf), no cart on the same supply today - a carter with a horse
  /// takes the log, and the pairs stand - and A SECOND HAND: the on-foot
  /// hands of this supply in row order go two by two, and the odd last one
  /// stands. Stateless on purpose: the morning's pairs are not kept anywhere,
  /// and a hand whose load BECAME a log at noon is judged by the same rule.
  bool LogPairCarriesNow(const WorldState& current,
                         std::uint32_t resident_row,
                         const UnitRow& unit) const {
    if (!LogLegOf(current, unit).open) {
      return false;
    }
    const UnitId works = current.residents.rows[resident_row].work.unit;
    std::uint32_t on_foot = 0;
    std::uint32_t before = 0;
    for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
      const WorkAssignment& work = current.residents.rows[row].work;
      if (work.kind != WorkKind::kHauling || work.unit.value != works.value ||
          work.stand.value != kInvalidEntityIdValue ||
          work.limit_delivery.value != kInvalidEntityIdValue) {
        continue;
      }
      if (work.rides_horse != 0) {
        return false;  // a cart is on it
      }
      ++on_foot;
      before += row < resident_row ? 1U : 0U;
    }
    return before + 1U < on_foot || on_foot % 2U == 0U;
  }

  /// A WALKER'S SHARE OF A CART-DAY BY THE JOB'S OWN LOAD (#671; AssignmentJob::
  /// walker_share): the last pass counts a hand on a heap of hay at the
  /// bundle's share, half the sack's. Only while the settlement has carts -
  /// without them the seam is the walker's own and the share is one.
  void PriceTheWalkersShares(const WorldState& current, std::vector<AssignmentJob>& jobs) const {
    if (!SettlementHasCarts(current, config_.horse_kind)) {
      return;
    }
    for (AssignmentJob& job : jobs) {
      if (job.walker_log) {
        job.walker_share = LogPairShareToday(current);  // a head of a pair, not a sack
        continue;
      }
      if (job.kind == WorkKind::kHauling) {
        const ResourceId carried =
            WalkerLoadResourceOfWork(current, HaulingWorkOfJob(job), config_.porter.log);
        job.walker_share = WalkerShareToday(current, carried);
        // A RICK IS NOT CARRIED ON FOOT (row 818; AssignmentJob::
        // rick_waits_for_a_cart): under the cart's share of a rick's tonnage
        // (0.37.252) a meadow's stack waits behind the open cut with no window,
        // and the last pass gave it the hands the queue had left - 16435
        // carter-hours on foot over two years in nine villages against 2690
        // before, all of them June to September, at a bundle of ten kilograms
        // a trip; and a hand the morning put on a stack is not idle for the
        // hour-1 top-up, which is where a field opened at hour 0 finds its
        // mowers. A works' supply is not touched: its hands have their own
        // gate (the horse yard's), and straw to a site in the first weeks goes
        // as it went.
        job.rick_waits_for_a_cart =
            !job.supplies_works && CargoFormOf(config_.porter, carried) == CargoForm::kRick;
      }
    }
  }

  /// THE GROOM'S REQUEST AT LEVEL 0 GOES AHEAD OF ALL WORK (boss, the
  /// logistics thread [9], Transport §11 as mended; routing stage B, B3): a
  /// hauling job whose load's task stands at level 0 (logistics_state.h) is
  /// placed ahead of every window (assignment.h, logistics_urgent). Levels 1-3
  /// keep their load's window in the common queue, as before.
  /// A PAUSED TASK'S LOAD IS NOT OFFERED (B7; transport §12: «Поставить
  /// задачу на паузу — временно снять фон, чтобы освободить мощность под
  /// срочное»): a hauling job whose load's task the chairman paused leaves the
  /// queue, so the hands and horses go to what is not paused. Until B7 the
  /// pause held only the plan (logistics_plan.cpp, OpenTasks) — and a pause
  /// with no door to set it was a flag nothing wrote.
  static void DropPausedLoads(const WorldState& current, std::vector<AssignmentJob>& jobs) {
    std::erase_if(jobs, [&current](const AssignmentJob& job) {
      if (job.kind != WorkKind::kHauling) {
        return false;
      }
      const WorkAssignment work = HaulingWorkOfJob(job);
      return std::ranges::any_of(current.logistics_tasks.rows,
                                 [&work](const LogisticsTaskRow& task) {
                                   return task.paused && WorkServesTask(work, task);
                                 });
    });
  }

  /// THE STANDING CROP BEFORE A HEAP THAT KEEPS (#1099; farming design rule
  /// 2, «в два шага»; econ standing-crop-before-keeping-heap-2026-10-19): on
  /// a day a ripe crop stands with work left (a reaping with grams at risk,
  /// the meadow cut aside), a heap urgent by its own rot is no loss deadline
  /// above the reaping's while its shelf outlasts rule 5's snow edge
  /// (early_snow_last_day) - the snow takes the crop and not the heap.
  /// AND ITS CARTING IS STEP TWO (rule 2: «возка» is named among the works
  /// without a loss deadline): its window goes too. The first pair read it:
  /// with the level gone the heap still kept HaulWindow, a kDays window on
  /// the reaping's own tier 0, and its carters stood beside the diggers (420
  /// noons on the canon, 940 on the novice). True when the heap on `field`
  /// keeps past the snow edge while a ripe crop stands.
  [[nodiscard]] bool HeapKeepsPastTheSnow(const WorldState& current,
                                          FieldId field,
                                          bool ripe_crop_stands) const {
    if (!ripe_crop_stands) {
      return false;
    }
    const std::uint32_t row = FindRow(current.fields, field);
    if (row == kNoRow || current.fields.rows[row].kind != LandKind::kArable ||
        current.fields.rows[row].reaped_grams <= 0) {
      return false;
    }
    const ResourceId resource = current.fields.rows[row].reaped_resource;
    const float shelf = resource.value < config_.heap_shelf_days.size()
                            ? config_.heap_shelf_days[resource.value]
                            : std::numeric_limits<float>::infinity();
    const auto today = static_cast<float>(current.calendar.day % kDaysPerYear);
    return !(today + shelf < static_cast<float>(config_.early_snow_last_day));
  }

  void MarkTheUrgentLoads(const WorldState& current, std::vector<AssignmentJob>& jobs) const {
    const bool ripe_crop_stands = std::ranges::any_of(jobs, [](const AssignmentJob& job) {
      return job.kind == WorkKind::kHarvest && job.grams_at_risk > 0 && !IsMeadowCut(job);
    });
    for (AssignmentJob& job : jobs) {
      if (job.kind != WorkKind::kHauling) {
        continue;
      }
      const WorkAssignment work = HaulingWorkOfJob(job);
      const bool keeps = job.field.value != kInvalidEntityIdValue &&
                         HeapKeepsPastTheSnow(current, job.field, ripe_crop_stands);
      if (keeps) {
        job.window = DeadlineNotApplicable();  // step two (rule 2), below the reaping
      }
      for (const LogisticsTaskRow& task : current.logistics_tasks.rows) {
        if (!WorkServesTask(work, task)) {
          continue;
        }
        // One task a load: its level and the chairman's «важнее полевых
        // работ» (#734; LogisticsTaskRow::over_fields) are read together.
        // A stack came urgent by the stores' hay (CollectJobs) and stays so
        // whatever its task's row says this morning. A heap urgent by its
        // own rot that keeps past the snow is not (#1099); the player's level
        // 0 and a hungry herd's are, whatever the shelf.
        const bool rot_gives_way = keeps && task.urgent_by == LogisticsUrgentBy::kSpoil;
        job.logistics_urgent =
            job.logistics_urgent || (task.level == LogisticsLevel::kUrgent && !rot_gives_way);
        job.over_fields = task.over_fields && job.supplies_works;
        break;
      }
    }
  }

  /// THE LAST DAYS: WHAT CAN STILL BE DONE, THEN THE HEAVIER (boss seq 103).
  /// Reaping is all or nothing — the grams come when the field is reaped
  /// whole, and the snow takes the whole standing crop — so the heaviest
  /// field first could start one the days cannot finish and lose both. The
  /// reapings carrying grams are walked heaviest first against what the
  /// village can still reap before the snow; each that fits is kept and
  /// spends its days, each that does not is marked `beyond_the_snow` and
  /// ranks after every one that fits (assignment.cpp).
  ///
  /// THE COUNT IS THE ALARM'S, BY FIELDS (core_common/reaping_pace.h, one
  /// home; the harvest rule 5, 0.37.147): the crew the reaping can have at
  /// its last norm-days an hour, walked over each field's own road and each
  /// day's own light and dry share TO THE EARLY EDGE OF THE SNOW
  /// (LaborConfig::early_snow_last_day) — the heaviest first, the crew
  /// coming to a field when the one before it is reaped.
  ///
  /// UNTIL THEN a capacity: the village's one pace of yesterday times the
  /// working days to the MEAN edge, the fields subtracted from it. Before
  /// 2026-09-19 it was every employable hand at today's daylight over the
  /// standard day, efficiency one, and host's seed 9 with the reaping made
  /// three times longer showed what that costs: 46 hands reaped 22.7
  /// norm-days a November day against about 39 counted, both fields read as
  /// finishable, and the first step of the rule could not tell them apart.
  void MarkTheReapingsTheSnowWillTake(const WorldState& current,
                                      std::vector<AssignmentJob>& jobs) const {
    std::vector<std::uint32_t> reapings;
    for (std::uint32_t index = 0; index < jobs.size(); ++index) {
      if (jobs[index].grams_at_risk > 0) {
        reapings.push_back(index);
      }
    }
    if (reapings.empty()) {
      return;
    }
    std::ranges::stable_sort(reapings, [&jobs](std::uint32_t left, std::uint32_t right) {
      return jobs[left].grams_at_risk > jobs[right].grams_at_risk;
    });
    std::uint32_t hands = 0;
    for (const ResidentRow& resident : current.residents.rows) {
      hands += Employable(current, resident) ? 1U : 0U;
    }
    const auto today = static_cast<std::uint32_t>(current.calendar.day % kDaysPerYear);
    // A WORKING DAY COUNTS BY ITS DRY SHARE: rain stops the reaping
    // (core_common/rain_stops_work.h), and the climate's share of rain days
    // is what can be known of the days ahead. TODAY IS NOT AHEAD: its sky is
    // written, and a rained-out today is no working day at all.
    // A day off is no working day, written as «no dry share» so that the one
    // walk skips it (the alarm builds the same array).
    RainDayShares ahead = config_.rain_day_shares;
    ahead[today] = RainStopsWork(current.weather.precipitation, WorkKind::kHarvest) ? 1.0F : 0.0F;
    for (std::uint32_t day = today; day < kDaysPerYear; ++day) {
      if (IsDayOffIn(current, current.calendar.day - today + day)) {
        ahead[day] = 1.0F;
      }
    }
    const ReapingCrew crew = ReapingCrewOf(current.ledger.current,
                                           current.ledger.closed,
                                           hands,
                                           config_.reaping_days_per_hand_light_hour);
    const bool by_cart = DraughtHorses(current) > 0;
    double clock = static_cast<double>(today);
    for (const std::uint32_t index : reapings) {
      AssignmentJob& job = jobs[index];
      const float road = ReapingRoadHours(
          current, job.position, by_cart, config_.walk_speed_kmh, config_.harness_speed_kmh);
      const ReapingWalk walk = WalkTheReaping(ahead,
                                              crew,
                                              road,
                                              static_cast<double>(job.work_days_remaining),
                                              clock,
                                              config_.early_snow_last_day);
      if (walk.done) {
        clock = walk.clock;  // the crew goes on to the next field from here
      } else {
        job.beyond_the_snow = true;  // and spends none of the days on it
      }
    }
  }

  /// Grams of `resource` lying in every unit's stock — the tools a felling
  /// crew can pick up. Counted here rather than asked of core_production's
  /// store lookups, which this module does not reach (manual/65-labor-model.md
  /// §2: the two meet only at the seams).
  static Grams TotalHeld(const WorldState& current, ResourceId resource) {
    Grams held = 0;
    for (const UnitRow& unit : current.units.rows) {
      held += resource.value < unit.stock.size() ? unit.stock[resource.value] : 0;
    }
    return held;
  }

  static bool HerdPosition(const WorldState& current, const HerdRow& herd, Vec2& position) {
    const std::uint32_t unit_row = FindRow(current.units, herd.unit);
    if (unit_row == kNoRow) {
      return false;
    }
    position = current.units.rows[unit_row].position;
    return true;
  }

  /// Urgency of a load waiting on a field: THE DAYS UNTIL THE SNOW, which
  /// is the deadline a lying load actually has (A3's named term — the first
  /// settled snow takes what is still out).
  ///
  /// It used to be the harvest window of whatever was lying there, and that
  /// was wrong in the one direction that mattered. A load left over from
  /// last autumn has a window that shut months ago, which reads as "as
  /// urgent as work gets" — so through the whole of spring the carts
  /// outranked the plough and took every horse in the village. The load was
  /// indeed old; it was not due tonight.
  ///
  /// A FIELD'S LOAD ONLY, since 0.36.19: grain lying on a field is rained
  /// on and the snow takes what is still out. A stand's logs, a dig's load
  /// (0.36.19) and the district's timber lot (0.36.18) had it too, and did
  /// not spoil; ranked with a window, above the fallow for the rye, they took
  /// its horses at the end of September (boss-core-epoch1-resume [36]-[40]).
  /// The field's load still does, and knowingly: on seed 1934, year 5, its
  /// carts held 22 and 30 horses on two days of the rye's harrowing — boss
  /// kept it with a window ([40]: «зерно на поле мокнет»).
  ///
  /// AN ARABLE FIELD'S LOAD ONLY, since 0.37.128: the hay lying mown at a
  /// meadow was a field's load too (0.37.127) and took the horses of the
  /// rye's fallow in August and September; it is windowless, as the logs.
  static Deadline HaulWindow(const WorldState& current) {
    const std::uint32_t day_of_year = current.calendar.day % kDaysPerYear;
    return DeadlineInDays(static_cast<std::int32_t>(kDaysPerYear - day_of_year - 1U));
  }

  /// The crop a field job works toward: the one opened on the field, else
  /// this year's in the rotation, else — a fallow year — next year's.
  /// The crop a field's work is for when the field names none: this year's
  /// slot — or NONE when that slot is a winter crop lost to its window
  /// (question 278, WinterSlotLost): its year is a fallow's, and the fallow's
  /// ploughing is not the lost rye's work (static review of 0.36.13: it took
  /// the rye's window and its plan position).
  CropId SlotCrop(const FieldRow& field, SimDay today) const {
    if (field.crop.value != kInvalidDefIdValue) {
      return field.crop;
    }
    const CropId year0 = field.rotation_year0;
    const bool winter = year0.value < config_.crops.size() && config_.crops[year0.value].is_winter;
    return WinterSlotLost(field, winter, today) ? CropId{} : year0;
  }

  CropId JobCrop(const FieldRow& field, SimDay today) const {
    const CropId slot = SlotCrop(field, today);
    return slot.value != kInvalidDefIdValue ? slot : field.rotation_year1;
  }

  /// Whether this field job — ploughing, harrowing or sowing — works toward a
  /// WINTER crop of next year's slot: the fallow ploughed for it in spring,
  /// and the autumn's ploughing and sowing of it. The job then takes that
  /// crop's window and ranks below every job with a window of its own
  /// (assignment.h, prepares_winter_crop). A winter crop already of this
  /// year's slot is standing and only reaped, so it never qualifies.
  bool PreparesWinterCrop(const FieldRow& field, WorkKind kind, SimDay today) const {
    const CropId crop = JobCrop(field, today);
    return kind != WorkKind::kHarvest && crop.value < config_.crops.size() &&
           config_.crops[crop.value].is_winter != 0 && crop.value != field.rotation_year0.value;
  }

  /// Whether the winter crop's sowing window, `window`, has fewer days left
  /// than the preparation still needs (assignment.h, winter_window_closing;
  /// 0.37.165): this phase's norm-days over the village's draught horses,
  /// rounded up, a day for each phase after this one up to the sowing, and a
  /// day to spare — STUB core, the spare day is not a design number. A window
  /// not counted in days (overdue, none) is not closing: past it the slot is
  /// lost, and the overdue tier already stands above the preparation's.
  bool WinterWindowClosing(const WorldState& current,
                           const FieldRow& field,
                           WorkKind kind,
                           const Deadline& window) const {
    if (window.kind != DeadlineKind::kDays) {
      return false;
    }
    std::int32_t phases_after = 0;
    if (kind == WorkKind::kPlowing) {
      phases_after = 2;  // the harrow and the drill
    } else if (kind == WorkKind::kHarrowing) {
      phases_after = 1;  // the drill
    }
    const auto horses = static_cast<float>(std::max<std::uint32_t>(1U, DraughtHorses(current)));
    const auto this_phase =
        static_cast<std::int32_t>(std::ceil(field.work_days_remaining / horses));
    constexpr std::int32_t kSpareDays = 1;
    return window.days <= this_phase + phases_after + kSpareDays;
  }

  /// Urgency of a field job: the crop's own window, open or closed. The
  /// crop is the one in the ground, or — while the field is still being
  /// prepared — the one the rotation plans for this year, or, in a FALLOW
  /// year, the winter crop that follows it.
  ///
  /// THE FALLOW BEFORE A WINTER CROP HAS A DEADLINE, and until 2026-09-14 it
  /// had none. Its ploughing is the preparation for the rye sown that same
  /// autumn (farming design §7; start canon §8, "winter rye goes in the
  /// autumn of the same year"), but with no window it ranked below every job
  /// that had one — and from the thaw to the snow the harness work (the
  /// windowed ploughs, then the meadow cut) took every horse. Measured on
  /// labor_year, seed 1930: the start's 3.5 ha fallow opened for ploughing on
  /// day 16 and stood unworked to the year's end with sixty people idle a
  /// day; the rye was never sown and the ploughing band read 84.29.
  /// Whether this is the reaping of an annual the snow gates (ripen days
  /// above nought) with `harvest_snow_last_days` or fewer to the snow, the
  /// snow's own day included. Nothing past the snow: the field is gone.
  bool InSnowLastDays(const CalendarState& calendar, const FieldRow& field, WorkKind kind) const {
    if (kind != WorkKind::kHarvest || config_.harvest_snow_last_days == 0 ||
        field.crop.value >= config_.crops.size() ||
        config_.crops[field.crop.value].ripen_days <= 0) {
      return false;
    }
    const auto day_of_year = static_cast<std::int32_t>(calendar.day % kDaysPerYear);
    const auto snow = static_cast<std::int32_t>(config_.growing_season_last_day);
    const std::int32_t to_snow = snow - day_of_year;
    return to_snow >= 0 && to_snow <= static_cast<std::int32_t>(config_.harvest_snow_last_days);
  }

  /// @brief Whether the crop this field's work is for carries a position of
  ///        this year's plan: plan.due above nought for its resource (boss,
  ///        boss-core-epoch1-5 seq 50). The crop is the one FieldWindow asks
  ///        of — the field's own, else its slot's, else the winter crop a
  ///        fallow is prepared for.
  bool CarriesPlanPosition(const WorldState& current, const FieldRow& field, WorkKind kind) const {
    const SimDay today = current.calendar.day;
    CropId crop = SlotCrop(field, today);
    if (PreparesWinterCrop(field, kind, today)) {
      crop = JobCrop(field, today);
    }
    if (crop.value >= config_.crops.size()) {
      return false;
    }
    const ResourceId resource = config_.crops[crop.value].resource;
    return resource.value < current.plan.due.size() && current.plan.due[resource.value] > 0;
  }

  Deadline FieldWindow(const CalendarState& calendar, const FieldRow& field, WorkKind kind) const {
    // THE MEADOW CUT HAS A WINDOW OF ITS OWN, June to July (farming design,
    // the months' row: "рост и сенокос"). A meadow has no crop, and until
    // 2026-09-14 the cut fell through to "no window" below: from 0.23.0 it
    // ranked under the fallow's ploughing for rye, which took its hands and
    // horses — on seed 9 of the host's party with no orders the first year's
    // hay fell from 140 t to 110 t and 17 cows of 33 starved in the second
    // winter (boss, parcels 258 and 260). Where the cut stands among the
    // other work is the queue's tier, not this window (assignment.cpp).
    if (field.kind == LandKind::kMeadow || field.kind == LandKind::kFloodplainMeadow) {
      return WindowOf(calendar, config_.meadow_cut_to_month);
    }
    CropId crop = SlotCrop(field, calendar.day);
    if (PreparesWinterCrop(field, kind, calendar.day)) {
      crop = JobCrop(field, calendar.day);
    }
    if (crop.value >= config_.crops.size()) {
      // FALLOW, OR A CROP THIS BUILD DOES NOT KNOW: no window to miss, and
      // no claim on the hands ahead of work that has one.
      return DeadlineNotApplicable();
    }
    const CropWindows& windows = config_.crops[crop.value];
    const bool harvest = kind == WorkKind::kHarvest;
    const Deadline window =
        WindowOf(calendar, harvest ? windows.harvest_to_month : windows.sow_to_month);
    // THE REAPING'S EDGE IS THE SNOW; ITS WINDOW IS THE URGENCY (boss seq 71,
    // 2026-09-18). A potato sown three days late ripened past its reaping
    // window, fell to the overdue tier below every job with an open window,
    // and was taken whole by the snow — 150 t a seed (host, seeds 9 and 42).
    // While the window is open it ranks the reaping as it always did; past
    // it, an annual still stands until the snow, and that is a deadline, not
    // a miss. A crop the snow does not gate (ripen 0: winter crops,
    // perennials) keeps its window.
    //
    //
    // THE SOWING'S END, ONLY FOR GROUND ALREADY PLOUGHED (boss seq 76). A
    // field whose ploughing is done — harrowing or sowing left — is due past
    // its window by the last day its crop can still ripen: the cabbage of
    // seed 1934, ploughed by day 17 and harrowed past its window until day
    // 26 of the 23 it had, is that case. A field NOT YET PLOUGHED keeps its
    // window: ranked by that last day too, a new ploughing took the horses
    // of a village that had few — on the no-horses arm of idle_curve, seeds
    // 1930-1939, five of ten twelfth years sowed nothing against none before
    // — for a field the player never got to start.
    if (InSnowLastDays(calendar, field, kind)) {
      // THE LAST DAYS (boss seq 95, register 235): open window or closed,
      // the snow is the one edge, and CollectJobs weighs the fields by the
      // grams the snow would take.
      return DueByDay(calendar, static_cast<std::int32_t>(config_.growing_season_last_day));
    }
    const bool ploughed = kind == WorkKind::kHarrowing || kind == WorkKind::kSowing;
    if (windows.ripen_days > 0 && window.kind == DeadlineKind::kOverdue &&
        (harvest || (ploughed && !PreparesWinterCrop(field, kind, calendar.day)))) {
      const auto snow = static_cast<std::int32_t>(config_.growing_season_last_day);
      return DueByDay(calendar, harvest ? snow : snow - windows.ripen_days);
    }
    return window;
  }

  /// @brief Whether this man could be put to work at all — THE SAME TEST
  /// the accountant applies each morning, which is the whole point of it
  /// being one function: `CollectCandidates` calls it, and so does the
  /// boundary's answer to the layer. A second copy would drift the day
  /// somebody added a reason and touched only one of them.
  ///
  /// A POST HOLDER IS EMPLOYABLE and deliberately so: he is out of the
  /// accountant's pool because he has his own place, not because he cannot
  /// be given work. Counting him idle would put the village's groom into a
  /// red number every day of his life.
  /// The knobs of the one reach question (home_reach.h), from this config.
  ReachRule ReachRuleOf() const {
    return ReachRule{.walk_speed_kmh = config_.walk_speed_kmh,
                     .ride_speed_kmh = config_.harness_speed_kmh,
                     .travel_limit_hours = config_.travel_limit_hours,
                     .min_usable_hours = config_.min_usable_hours};
  }

  bool Employable(const WorldState& state, const ResidentRow& resident) const {
    // AWAY IN THE DISTRICT (away_in_district.h): in its hospital, or on the
    // road home from its border — nobody's worker (district_car.h).
    if (OffWork(resident, state.calendar.tick)) {
      return false;
    }
    Vec2 home;
    const float age = BiologicalAgeYears(config_, resident.birth_day, state.calendar.day);
    return age >= config_.adult_age_years && HomePosition(state, resident.family, home);
  }

  /// Everyone of working age whose day can start somewhere. Children are
  /// left out entirely: child labor (life-cycle §7) is deferred.
  /// `resting`, when given, gets the row of each adult who passed every
  /// other test of the list and was kept home by the rest limit alone
  /// (IdleReason::kResting) — rows since the office's workbook (the reason
  /// on the resident), a count before. The rest test stands last for that: a
  /// mirror of the list's tests kept apart from it missed the efficiency drop
  /// (static review of 0.36.32).
  std::vector<AssignmentCandidate> CollectCandidates(
      const WorldState& current, std::vector<std::uint32_t>* resting = nullptr) const {
    const std::vector<bool> horse_locked = MarkHorseHosts(current);
    const float aging_from = AgingFromYears(config_, current);
    std::vector<AssignmentCandidate> candidates;
    candidates.reserve(current.residents.rows.size());
    for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
      const ResidentRow& resident = current.residents.rows[row];
      Vec2 home;
      if (!Employable(current, resident) || !HomePosition(current, resident.family, home)) {
        continue;
      }
      const float age = BiologicalAgeYears(config_, resident.birth_day, current.calendar.day);
      // A post of the working day is a place of his own, and the accountant
      // does not touch him. An evening or bath-day post leaves his day to the
      // list like anybody's (post_shift.h; the human's word of 2026-09-14,
      // "днём оба в наряде").
      if (resident.post.profession.value != kInvalidDefIdValue &&
          (resident.post.profession.value >= config_.professions.size() ||
           PostHoldsTheDay(config_.professions[resident.post.profession.value].shift))) {
        continue;
      }
      const bool first_year = current.calendar.date.year == 0;
      // PAST HIS LIMIT HE STAYS HOME (units rules §8: "Решает сам работник —
      // не игрок и не учётчик", "Возвращается на следующий день, отдохнув").
      // Placed anyway until 0.35.11, he walked off in his first working hour,
      // his crew stood "crewed" for the top-up, and his day counted as worked,
      // so he never rested back: thirty_years year 19, the same five harrowers
      // at rest 0-9 sent to the oat fields three mornings running while 707
      // rested men idled, and the oats missed their window. EMPLOYABLE STILL
      // SAYS YES — he is not away and not a child; he is resting today.
      if (resident.rest <= config_.rest_walkoff_threshold) {
        if (resting != nullptr) {
          // Counted only if a rested morning would put him on the list: his
          // efficiency with the rest restored, not today's, since the table
          // may set rest_factor_spent to nought.
          ResidentRow rested = resident;
          rested.rest = kMetricMax;
          if (ResidentEfficiency(config_, rested, age, aging_from, first_year, false) > 0.0F) {
            resting->push_back(row);
          }
        }
        continue;
      }
      AssignmentCandidate candidate;
      candidate.resident_row = row;
      candidate.home = home;
      // Ranked before any work is chosen, so the sober factor is the general
      // one: the three spared works (AlcoholSparesWork) are spared where the
      // work is delivered, below, and not in who comes first to the list.
      candidate.efficiency =
          ResidentEfficiency(config_, resident, age, aging_from, first_year, false);
      candidate.rest = resident.rest;
      candidate.skill = FieldSkillBlend(config_, resident);
      candidate.horse_locked = horse_locked[row];
      if (candidate.efficiency <= 0.0F) {
        continue;
      }
      candidates.push_back(candidate);
    }
    return candidates;
  }

  /// The start canon (livestock design §5): while the kolkhoz yard is not
  /// built, every kolkhoz horse stands at somebody's yard and that
  /// household owes it horse work — one worker per adult horse hosted,
  /// taken in row order so the choice is stable. ASSUMPTION: the design
  /// names "the resident whose yard hosts the horse" without saying which
  /// member of the household that is (manual/65-labor-model.md §4).
  std::vector<bool> MarkHorseHosts(const WorldState& current) const {
    std::vector<bool> locked(current.residents.rows.size(), false);
    if (config_.horse_kind.value == kInvalidDefIdValue || current.chairman.horses_stabled != 0) {
      // Once the team is stabled the lock is gone for good, whatever later
      // becomes of the yard (livestock design §5: the mark "at the horse"
      // never comes back). The loop below would say the same today — no
      // kolkhoz horse stands at a yard any more — but the flag says it for
      // every tomorrow as well.
      return locked;
    }
    for (const HerdRow& herd : current.herds.rows) {
      if (herd.kind.value != config_.horse_kind.value ||
          herd.household.value == kInvalidEntityIdValue) {
        continue;
      }
      std::uint16_t left = herd.adult_count;
      for (std::uint32_t row = 0; row < current.residents.rows.size() && left > 0; ++row) {
        const ResidentRow& resident = current.residents.rows[row];
        const float age = BiologicalAgeYears(config_, resident.birth_day, current.calendar.day);
        if (resident.family.value == herd.household.value && age >= config_.adult_age_years &&
            !locked[row]) {
          locked[row] = true;
          --left;
        }
      }
    }
    return locked;
  }

  /// Adult kolkhoz horses, wherever they stand (work_seam.h, DraughtHorsesOf:
  /// one home with the logistics' lamp since 0.37.276).
  std::uint32_t DraughtHorses(const WorldState& current) const {
    return DraughtHorsesOf(current, config_.horse_kind);
  }

  AssignmentParams DayParams(const WorldState& current) const {
    AssignmentParams params;
    params.window_hours = current.weather.daylight_hours;
    // THE EXEMPLAR OF WALKING WORK IS THE HARVEST, not the sowing, and the
    // difference is not academic: HoursPerKm answers by KIND, so a probe that
    // made sowing horse work (the drill measurement of 2026-09-12) turned
    // this line into the harness rate for every walking job in the village —
    // barn care included, which lost 24 man-days of care a year to an
    // instrument, not to a drill. A parameter named for a SPEED must be
    // taken from a kind that cannot change its speed under the question
    // being asked.
    params.walk_hours_per_km = HoursPerKm(config_, WorkKind::kHarvest);
    params.harness_hours_per_km = HoursPerKm(config_, WorkKind::kPlowing);
    params.people_cart_hours_per_km = static_cast<float>(kClockScale) / config_.loaded_speed_kmh;
    params.travel_limit_hours = config_.travel_limit_hours;
    // The queue advances by one a day and wraps on the roster.
    params.rotation = static_cast<std::uint32_t>(current.calendar.day);
    params.roster = static_cast<std::uint32_t>(current.residents.rows.size());
    params.min_usable_hours = config_.min_usable_hours;
    params.standard_day_hours = config_.standard_day_hours;
    params.draught_horses = DraughtHorses(current);
    // The floor under the works' supply (#734): the morning's, by the whole
    // team; the day's top-up deals none.
    params.supply_floor_carters = SupplyFloorCarters(config_.supply_floor, params.draught_horses);
    params.placement_level = config_.placement_level;
    params.walker_share_of_cart_day = WalkerShareToday(current);
    params.walker_min_trips_per_day = config_.walker_min_trips_per_day;
    params.people_cart_seats = config_.people_cart_seats;
    params.people_cart_min_walk_hours = config_.people_cart_min_walk_hours;
    return params;
  }

  /// @brief The part of a cart-day a carrier on foot does today: below 1
  ///        while the settlement has carts — production writes the loads'
  ///        seams in cart-days then, by the same predicate
  ///        (work_seam.h, SettlementHasCarts) — and a whole day while it
  ///        has none (core_common/haul.h, WalkerShareOfCartDay).
  /// @param carried The load's resource: a back takes a bundle of hay or a
  ///        sack by its form (#671; haul.h, WalkerLoadGrams). Invalid is the
  ///        sack - the day's own share, which the placement reads as «the
  ///        settlement has carts» when below 1.
  float WalkerShareToday(const WorldState& current, ResourceId carried = ResourceId{}) const {
    if (!SettlementHasCarts(current, config_.horse_kind)) {
      return 1.0F;
    }
    // The cart's load by the same resource (0.37.252): labour's share of a
    // cart-day and logistics' (porter_pairs.cpp) ask one function, or a
    // walker with hay would be paid against a cart heavier than hay's.
    const CartGear gear = CartGearToday(current, CartSpecsOf(config_));  // SL
    return WalkerShareOfCartDay(WalkerLoadGrams(config_.porter, carried),
                                CartLoadGrams(config_.porter, gear.load_kg, carried),
                                HoursPerKm(config_, WorkKind::kHarvest),
                                static_cast<float>(kClockScale) / gear.empty_kmh,
                                static_cast<float>(kClockScale) / gear.loaded_kmh);
  }

  // -- the working hour ----------------------------------------------------

  /// One hour of work for everybody who has an assignment and is inside his
  /// own window. Row order, sequentially: two workers draining the same
  /// seam must always drain it in the same order.
  void RunHour(WorldState& current, std::uint32_t hour) const {
    const DayWindow window = SolarWindow(current.weather.daylight_hours);
    std::vector<std::uint32_t> released_carters;  // S1': no leg of his load in a fresh plan
    for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
      const WorkKind kind = current.residents.rows[row].work.kind;
      if (kind == WorkKind::kNone) {
        continue;
      }
      // THE CART FOLLOWS THE GROOM'S PLAN (routing stage B, B4; logistics_
      // state.h): its load carted, or gone, it goes on to the next load of
      // its chain the same day. Until 0.37.177 it stood about, unpaid, to the
      // evening (below: «the job is done for today»).
      FollowThePlan(current, row);
      float* seam = WorkSeam(current, current.residents.rows[row].work);
      Vec2 target;
      Vec2 home;
      if (seam == nullptr || !WorkPlace(current, current.residents.rows[row].work, target) ||
          !HomePosition(current, current.residents.rows[row].family, home)) {
        // The job is gone from under him — most often because the crew
        // finished the phase and production moved the field on this very
        // hour. His day on it ends here, and it is paid here: a settled
        // assignment is the only thing that still knows its own rate.
        PayDay(current, current.residents.rows[row]);
        continue;
      }
      // A HERD'S CARE DRAINED TO NOUGHT IS WORK FINISHED BY THE WORLD (#709;
      // time design «Один работник — одна работа в день», the exception row;
      // boss, 12 October 2026): his hands are released for the rest of the
      // day. With no clerk in the core only the player re-places them, by an
      // order; nothing automatic - he goes home, and the hour takes no more of
      // him. What he did stands and is paid at the day's close. Until #709 he
      // stood at the barn to the evening - 10 to 20 hands of seed 1936's cattle
      // yard from noon to hour 17 while a ripe potato field had nobody.
      if (kind == WorkKind::kHerdCare &&
          (current.residents.rows[row].work.released_home != 0 || !(*seam > 0.0F))) {
        current.residents.rows[row].work.released_home = 1U;
        continue;
      }
      // Asked of the ASSIGNMENT and not of the kind: a meadow's cut rides and
      // a strip's harvest walks, one kind between them (work_seam.h).
      // AND THE CARTING RIDES WHEN THERE IS A HORSE — the one question the
      // assignment asks of every hauling job (CollectJobs, `harnessed`). The
      // shoulder was measured riding and the day walking until 0.34.28: on a
      // stand three kilometres out a carter walked his day away, 0.015 t of
      // logs a carter-day, and the logs lay on the stands while every site
      // in the village waited for them (the Epoch II diagnosis; boss seq 27;
      // the meadow's same defect, parcel 312).
      //
      // AND ON THE HORSE HE WAS GIVEN, NOT ON THE HERD (0.34.51; boss,
      // boss-core-topup-horses seq 2): "while the village has a horse" let a
      // carter ride with every horse in the plough. The placement records who
      // got one (WorkAssignment::rides_horse) and WorkRidesOut reads it, so
      // the hour, the reach and the resident's activity ask one question.
      const WorkAssignment& work = current.residents.rows[row].work;
      // THE ROAD BY THE WAY THERE IS, MEASURED ONCE A TARGET (road_route.h;
      // 0.36.2; WorkAssignment::travel_hours): a way by the network costs a
      // query a straight line did not, and it is the same all day.
      // A DAY WITH A HORSE BEGINS AT THE HORSE YARD (horse_yard_road.h;
      // 0.37.158): on foot to the horse, on it to the work.
      if (current.residents.rows[row].work.travel_hours < 0.0F) {
        // A road measured anew is a target changed since the morning: the
        // seat on a cart was for the old one (cart_passengers.h) — except a
        // seat on the people's cart his own crew rides (A3; 0.37.168), whose
        // driver is on the same work and target still: the morning's
        // placement measures every road anew, and it gave him that seat.
        if (!RidesThePeoplesCart(current, current.residents.rows[row].work)) {
          current.residents.rows[row].work.rides_cart_of = ResidentId{};
        }
        // And the evening's seat and way home were the old target's too
        // (F3 (B); logistics_plan.h, SeatEveningRiders): the next plan asks.
        current.residents.rows[row].work.travel_home_hours = -1.0F;
        current.residents.rows[row].work.rides_home_with = ResidentId{};
        current.residents.rows[row].work.travel_hours =
            WorkRoadHours(current,
                          work,
                          config_.horse_kind,
                          home,
                          target,
                          HoursPerKm(config_, WorkKind::kHarvest),
                          HoursPerKm(config_, WorkKind::kPlowing),
                          static_cast<float>(kClockScale) / config_.loaded_speed_kmh);
      }
      const float travel = current.residents.rows[row].work.travel_hours;
      // A CARTER'S ROAD TO THE LOAD IS HIS FIRST TRIP'S EMPTY HALF (0.37.139;
      // econ and boss, boss-all-carts-carry-people-go-2026-10-02 [72]-[76]).
      // A load's seam is priced in round trips load-store-load (haul.h,
      // HaulDaysFor); the road home-load-home was taken from the carter's
      // light besides — and his way out IS the empty half of the first
      // round, his way back the empty half of one he never drives: the last
      // cart comes into the village, where he lives. A whole trip a rider-day
      // was counted twice: 0.83 of a cart of hay a rider-day where the light
      // holds 1.4 (nine villages, 0.37.133). So the hauling's day is the
      // light. Every other work keeps its road: a field has no trip in its
      // seam, and the ploughman's way there is a way.
      // AN APPROXIMATION, NAMED: «the store is the horse yard» — the yard's
      // road to the village store is not in the day.
      // AND THE WALK TO THE HORSE IS (0.37.158; livestock design §5): once
      // the team is stabled, a carter with a horse walks to the yard in the
      // morning and home from it at night, and that walk is his road — the
      // only part of his way the load's seam does not price. A carter who
      // sets out from home (no yard yet, or on foot) keeps the light whole.
      // The road rule asks the whole way, walk and ride, at the placement
      // (AssignmentParams::yard_walk_hours) — whether he can get there.
      Vec2 yard{};
      const bool from_yard = DayStartsAtHorseYard(current, work) &&
                             HorseYardPositionOf(current, config_.horse_kind, yard);
      const float road = kind == WorkKind::kHauling && !from_yard ? 0.0F : travel;
      // THE WAY HOME, ITS OWN (stage В, F3 (B); transport design «Обратный
      // путь», 357c60d0): a morning rider the evening plan seated rides home
      // and one it refused walks — the day ends that much before sunset, and
      // the walk-minus-ride is what a refusal costs. Unwritten (negative): the
      // way home is the way there, as before.
      const float written_home = current.residents.rows[row].work.travel_home_hours;
      const float road_home = written_home >= 0.0F ? written_home : road;
      const float worked = HoursInside(hour, window.sunrise + road, window.sunset - road_home);
      if (worked <= 0.0F) {
        continue;
      }
      ResidentRow& resident = current.residents.rows[row];
      if (resident.work.hours_away_today <= 0.0F) {
        resident.work.hours_away_today = road + road_home;  // the round trip, booked once
      }
      resident.work.hours_away_today += worked;
      const float age = BiologicalAgeYears(config_, resident.birth_day, current.calendar.day);
      const float efficiency = ResidentEfficiency(config_,
                                                  resident,
                                                  age,
                                                  AgingFromYears(config_, current),
                                                  current.calendar.date.year == 0,
                                                  AlcoholSparesWork(resident.work));
      // THE AVRAL (unit rules §7; rush.h): the work delivers more by the
      // step, and the rest drains by twice the boost (leisure §6) — the
      // drain is taken on the work WITHOUT the boost and then raised, since
      // RestDrain already scales with what was delivered.
      const float boost = RushBoost(config_, current, resident.work);
      float delivered = worked * efficiency / config_.standard_day_hours * (1.0F + boost);
      // A CARRIER ON FOOT WRITES OFF HIS OWN CARRY (0.37.105; haul.h,
      // WalkerShareOfCartDay; manual/75-logistics.md §9): the load's seam is
      // in cart-days while the settlement has carts, and his norm-day is a
      // ninetieth of one. Until 0.37.105 it drained a whole cart-day — a log
      // of 200 kg «rode» on a back.
      const bool on_foot_at_a_cart_load =
          kind == WorkKind::kHauling && resident.work.rides_horse == 0 &&
          resident.work.stand.value == kInvalidEntityIdValue &&
          resident.work.limit_delivery.value == kInvalidEntityIdValue;
      // BY HIS LOAD'S FORM (#671): the bundle of hay is half the sack, and so
      // is his share of the cart-day and his pay below.
      const ResourceId carried =
          on_foot_at_a_cart_load
              ? WalkerLoadResourceOfWork(current, resident.work, config_.porter.log)
              : ResourceId{};
      // A LOG GOES BY TWO OR NOT AT ALL (#671; LogPairCarriesNow): a hand
      // whose works' next trip is a log from a pile carries it as one of a
      // pair under the last resort's gate, or stands this hour.
      bool under_a_log = false;
      if (on_foot_at_a_cart_load && carried.value != kInvalidDefIdValue &&
          carried.value == config_.porter.log.value) {
        const std::uint32_t works_row = FindRow(current.units, resident.work.unit);
        if (works_row != kNoRow && LogLegOf(current, current.units.rows[works_row]).walker_log) {
          if (!LogPairCarriesNow(current, row, current.units.rows[works_row])) {
            continue;
          }
          under_a_log = true;
        }
      }
      float walker_share = 1.0F;
      if (under_a_log) {
        walker_share = LogPairShareToday(current);
      } else if (on_foot_at_a_cart_load) {
        walker_share = WalkerShareToday(current, carried);
      }
      const float seam_share = on_foot_at_a_cart_load ? walker_share : 1.0F;
      const float could_deliver = delivered;
      if (delivered * seam_share > *seam) {
        delivered = *seam / seam_share;
      }
      if (delivered <= 0.0F) {
        continue;  // the job is done for today; he stands about, unpaid
      }
      // ONE DISPATCHER (#1056 C3 step 1, S1'; core-boss-oats-b-pair [87]-[96]):
      // a carter on a horse drains a planned load by his cart's plan - the plan
      // is the carting. Step 1 drained it only inside the legs and by his
      // efficiency, and so drained a priced trip at 0.57 of its price an hour:
      // the loading between the legs drained nothing, and the efficiency
      // slowed the horse (hay rot +162 % canon, +116 % novice; [95]). On the
      // road the cart's clock drains - his skill does not make the horse walk
      // faster; at the load and the receiver his efficiency stays, since a
      // skilled carter loads faster (econ, boss-econ-c3-body-pair [12]).
      // AND BY THE MINUTE (T1, save 178): the hour drains the share of it the
      // cart spends on his load's legs and next to them, by the plan's
      // unfloored clock. With the floored ticks the legs held 0.78 of the
      // hours the plan priced, and S1' carted every heap at that pace (hay rot
      // +116 % canon, +107 % novice; core-boss-oats-b-pair [98]).
      const CartShare cart_share = kind == WorkKind::kHauling && resident.work.rides_horse != 0
                                       ? CartShareOf(current, row)
                                       : CartShare{};
      if (cart_share.kind == CartHour::kUndrawn) {
        if (!current.groom_plan.stale && !FollowHisChain(current, row)) {
          released_carters.push_back(row);
        }
        continue;  // re-planned, sent on along his chain, or released
      }
      if (cart_share.kind == CartHour::kPlanned) {
        const float dwell_rate = worked > 0.0F ? could_deliver / worked : 0.0F;
        delivered = (cart_share.road_hours / config_.standard_day_hours) +
                    (cart_share.dwell_hours * dwell_rate);
        if (delivered > *seam) {
          delivered = *seam;
        }
        if (delivered <= 0.0F) {
          continue;  // riding to or from another load, or home; unpaid here
        }
      }
      const float seam_before = *seam;
      *seam -= delivered * seam_share;
      if (*seam < 0.0F) {
        *seam = 0.0F;  // the division above may leave a float's last digit
      }
      // AND THE HAND WHOSE HOUR DRAINED A HERD'S CARE is released with it (#709;
      // above): the rest of his day is not the barn's.
      if (kind == WorkKind::kHerdCare && !(*seam > 0.0F)) {
        resident.work.released_home = 1U;
      }
      // WHOSE UNITS THEY WERE (#671's instrument; hauled_book.h): noted on the
      // load's task, split into grams by production's settle.
      if (kind == WorkKind::kHauling) {
        NoteHaulDrain(current, resident.work, seam_before - *seam, resident.work.rides_horse == 0);
        // AND THE LOADS HIS CART CARRIES ALONG (#1056 C3, ruling 3; work_seam.h,
        // CoDrainCarriedTasks): a pooled second field drains with his seam.
        if (resident.work.rides_horse != 0) {
          const std::uint32_t own = TaskRowOfWork(current, resident.work);
          if (own != kNoRow) {
            CoDrainCarriedTasks(current, current.logistics_tasks.row_ids[own], seam_before - *seam);
          }
        }
      }
      // AND HE IS PAID BY WHAT HE CARRIED, NOT BY HIS HOURS (0.37.109; boss, 2
      // October 2026; labor.csv walker_norm_kg_per_day): the day's trips —
      // the light over his round trip — by his carry over the carrying norm,
      // never above one, spread over the hours he works. Until 0.37.109 his
      // hours' norm-days were paid whole: some 270 trudodni a village a year
      // for some 10 t. The trip is his walk from home (assignment.h,
      // walker_min_trips_per_day — the same approximation, named there). In a
      // settlement with no cart the seam is a walker's and so is the norm-day.
      float paid = delivered;
      if (on_foot_at_a_cart_load && walker_share < 1.0F && travel > 0.0F &&
          config_.walker_norm_kg_per_day > 0.0F) {
        const float light = current.weather.daylight_hours;
        const float trips = light / (2.0F * travel);
        // BY HIS OWN OUTPUT (0.37.113; econ, 2 October 2026): what he carried
        // is the trips by the carry by his efficiency — the multiplier every
        // other norm-day is cut by, the stub of a carrying capacity the core
        // does not keep yet (transport design §2). Without it the pay ROSE
        // with the pair of 0.37.109, 0.56 -> 0.71 norm-days a walker's day:
        // the man had dropped out of the sum.
        const float carry_kg =
            static_cast<float>(under_a_log ? config_.porter.log_piece_grams / 2
                                           : WalkerLoadGrams(config_.porter, carried)) /
            static_cast<float>(kGramsPerKilogram);
        const float day_pay =
            std::min(1.0F, trips * carry_kg * efficiency / config_.walker_norm_kg_per_day);
        // Spread over the hours he works — the whole light since 0.37.139
        // (his walk to the heap is his first trip's, above); until then the
        // light less the road, which his hours no longer are.
        const float usable = light - (2.0F * road);
        paid = usable > 0.0F ? day_pay * (worked / usable) * (delivered / could_deliver) : 0.0F;
      }
      resident.work.worked_norm_days_today += paid;
      const float drain =
          RestDrain(config_, resident, kind, delivered / (1.0F + boost)) * (1.0F + (2.0F * boost));
      resident.rest = resident.rest > drain ? resident.rest - drain : 0.0F;
      if (resident.rest <= config_.rest_walkoff_threshold) {
        // The critical fatigue limit (unit rules §8): his own decision, and
        // it ends his working day — so his day is settled here and now.
        current.ledger.current.walk_offs += 1;
        SimEvent& event = EmitEvent(current, EventKind::kWalkOff, EventSeverity::kNotable);
        event.resident = current.residents.row_ids[row];
        PayDay(current, resident);
      }
    }
    // A CARTER THE PLAN DOES NOT CARRY IS RELEASED TO OTHER WORK (S1'; econ,
    // boss-econ-c3-body-pair [12]): the world ended his job - a fresh plan has
    // no leg of his load and no load of his chain - so his day on it is paid
    // now, his horse goes back, and the top-up may place him this hour on work
    // nobody holds (one job a day: the world, not a better job, ended it).
    // Until S1' he stood unpaid by his load to the evening (step 1's pair:
    // the heaps' carter-hours doubled, the district lot lost 96 % of its own).
    if (!released_carters.empty()) {
      std::vector<WorkAssignment> released_from;
      for (const std::uint32_t row : released_carters) {
        ResidentRow& carter = current.residents.rows[row];
        released_from.push_back(carter.work);
        PayDay(current, carter);
        carter.work.rides_horse = 0;
        carter.work.stand = TimberStandId{};
        carter.work.unit = UnitId{};
        carter.work.extraction_site = ExtractionSiteId{};
        carter.work.limit_delivery = LimitDeliveryId{};
      }
      TopUpDay(current, &released_carters, &released_from);
    }
    // AND EVERY HAND OF A CARE THE HOUR DRAINED, at the hour's end (#709): a
    // hand earlier in the rows than the one whose hour drained it was asked
    // while it still had work, and would stand at the barn to the next hour.
    // The suite's first run of the release read 3 700 - 4 300 such hand-hours
    // over thirty years: one hour per hand per drained care.
    for (ResidentRow& resident : current.residents.rows) {
      if (resident.work.kind != WorkKind::kHerdCare || resident.work.released_home != 0) {
        continue;
      }
      const float* seam = WorkSeam(current, resident.work);
      if (seam != nullptr && !(*seam > 0.0F)) {
        resident.work.released_home = 1U;
      }
    }
  }

  /// @brief Whether a passenger's cart HAS COME to him (0.37.186): its
  ///        driver is there and holds a horse, and today's plan does not
  ///        refute it. «Come» is «the hour the seating estimated, and the cart
  ///        alive»: the core's cart has no hour of arrival but an estimate.
  ///        THE PLAN REFUTES, IT DOES NOT CONFIRM: a plan that has this
  ///        driver's goods cart and carries the passenger on none of its legs
  ///        says the cart is not bringing him; a driver the plan has no cart
  ///        for — no plan today, no task on his load, before the first horse
  ///        yard there are no tasks at all — is taken at the seating's word.
  ///        Asked for a cart in the plan, every passenger of a planless
  ///        morning would have hung, and the dog walked them all.
  static bool PassengersCartCame(const WorldState& current, ResidentId passenger, ResidentId of) {
    const std::uint32_t driver = FindRow(current.residents, of);
    if (driver == kNoRow || current.residents.rows[driver].work.rides_horse == 0) {
      return false;
    }
    if (current.groom_plan.day != current.calendar.day) {
      return true;
    }
    bool planned = false;
    bool carried = false;
    for (const CartPlan& cart : current.groom_plan.carts) {
      if (cart.driver.value != of.value || cart.on_foot) {
        continue;
      }
      planned = true;
      for (const CartLeg& leg : cart.legs) {
        carried = carried || std::ranges::any_of(leg.riders, [&](ResidentId rider) {
                    return rider.value == passenger.value;
                  });
      }
    }
    return !planned || carried;
  }

  /// @brief A passenger's wait ends when the cart has come (B6): at the hour
  ///        it is due at his point (WaitRecord::due, the seating's) if the
  ///        cart has come (PassengersCartCame); with the work it served; and
  ///        at the day's last hour whatever stands. A cart that has not come
  ///        leaves the record standing, and past its term the dog finds it —
  ///        «term passed» — and he walks (wait_rules.cpp). Struck before the
  ///        watchdog walks (phase 6).
  ///        BY THE CLOCK ALONE UNTIL 0.37.186: the hour after he reached his
  ///        point (0.37.182-0.37.184), then the term's end (0.37.185) — and in
  ///        both forms the dog's «term passed», which needs the tick PAST the
  ///        term, could not fire for a passenger: the strike came first
  ///        (boss, the logistics thread [77] p. 2).
  static void ClearPassengerWaits(WorldState& current, bool day_ends) {
    const Tick now = current.calendar.tick;
    for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
      ResidentRow& person = current.residents.rows[row];
      if (!person.wait.has_value() || person.wait->kind != WaitKind::kPassengerAwaitsCart) {
        continue;
      }
      // AND WITH THE WORK IT SERVED (0.37.183): a job finished or released
      // ends his walk to the cart — the wait has nothing left to wait for.
      // Until then the record outlived it, and the dog «found» it: 29 firings
      // in 0.37.182's canon, all «work gone», stood nought hours.
      const bool work_gone = person.work.kind == WorkKind::kNone ||
                             person.work.rides_cart_of.value != person.wait->target.resident.value;
      const bool came =
          now >= person.wait->due &&
          PassengersCartCame(current, current.residents.row_ids[row], person.wait->target.resident);
      if (day_ends || work_gone || came) {
        person.wait.reset();
      }
    }
  }

  /// What a carter's hour is to his load in today's plan (CartShareOf).
  enum class CartHour : std::uint8_t {
    kAsBefore,  ///< not priced by the plan: drained by his hours, as before C3
    kPlanned,   ///< drained by the shares of the hour (CartShare)
    kUndrawn,   ///< his cart's plan names his load in no leg it carts
  };

  /// A carter's hour by the plan's minutes (CartShareOf).
  struct CartShare {
    CartHour kind = CartHour::kAsBefore;
    float road_hours = 0.0F;   ///< on legs of his load: the cart's clock drains
    float dwell_hours = 0.0F;  ///< between legs next to one of his load: his
                               ///< loading and unloading, by his efficiency
  };

  /// @brief THE PLAN IS THE CARTING (#1056 C3 step 1, S1', T1; core-boss-oats-
  ///        b-pair [87]-[102]): what this hour is, in today's plan, to the load
  ///        of carter `row`, a hauler on a horse. kAsBefore when his load has
  ///        no trip facts (trip_model below 3, a load priced on foot), when its
  ///        round trip is longer than today's light (a BEYOND-THE-DAY load, the
  ///        district lot's 67-84 h: one day's plan cannot draw it - L2, a named
  ///        class), or when no plan stands for today. kUndrawn when no leg of
  ///        his cart names it but a kept one (KeepInTheChain), or he has no
  ///        cart. Otherwise kPlanned, with the hour's overlap, by the legs'
  ///        minutes (save 178), with the legs naming his load (road) and with
  ///        the gaps between two legs one of which names it (dwell: the
  ///        loading before a loaded leg, the unloading after it -
  ///        logistics_system.cpp, SomeCartFree, reads the loading the same
  ///        way). Both nought: riding to another load, on it, or home.
  static CartShare CartShareOf(const WorldState& current, std::uint32_t row) {
    const WorkAssignment& work = current.residents.rows[row].work;
    const std::uint32_t task_row = TaskRowOfWork(current, work);
    if (task_row == kNoRow || current.groom_plan.day != current.calendar.day) {
      return CartShare{};
    }
    const TaskTripTerms& terms = current.logistics_tasks.rows[task_row].trip_terms;
    if (!(terms.trip_hours > 0.0F) || terms.trip_hours > current.weather.daylight_hours) {
      return CartShare{};
    }
    const std::uint32_t task = current.logistics_tasks.row_ids[task_row].value;
    const ResidentId driver = current.residents.row_ids[row];
    const Tick day_start = static_cast<Tick>(current.groom_plan.day) * kTicksPerDay;
    const int hour_from = static_cast<int>(current.calendar.tick - day_start) * 60;
    const auto his = [task](const CartLeg& leg) {
      return leg.task.value == task && leg.kept_in_chain == 0;
    };
    const auto overlap = [](int from, int to, int hour_from, int hour_to) {
      const int start = std::max(from, hour_from);
      const int end = std::min(to, hour_to);
      return end > start ? static_cast<float>(end - start) / 60.0F : 0.0F;
    };
    for (const CartPlan& cart : current.groom_plan.carts) {
      if (cart.driver.value != driver.value || cart.on_foot) {
        continue;
      }
      if (!std::ranges::any_of(cart.legs, his)) {
        return CartShare{.kind = CartHour::kUndrawn};
      }
      CartShare share{.kind = CartHour::kPlanned};
      for (std::size_t index = 0; index < cart.legs.size(); ++index) {
        const CartLeg& leg = cart.legs[index];
        if (his(leg)) {
          share.road_hours +=
              overlap(leg.depart_minute, leg.arrive_minute, hour_from, hour_from + 60);
        }
        if (index + 1 < cart.legs.size() && (his(leg) || his(cart.legs[index + 1]))) {
          share.dwell_hours += overlap(
              leg.arrive_minute, cart.legs[index + 1].depart_minute, hour_from, hour_from + 60);
        }
      }
      return share;
    }
    return CartShare{.kind = CartHour::kUndrawn};
  }

  /// @brief S1': a carter whose load the fresh plan does not draw goes on to
  ///        the first load of his cart's chain that still has carting left,
  ///        as FollowThePlan sends one whose load is carted. Returns whether
  ///        he was sent; false when his cart names no such load (he is then
  ///        released, RunHour).
  static bool FollowHisChain(WorldState& current, std::uint32_t row) {
    WorkAssignment& work = current.residents.rows[row].work;
    const ResidentId driver = current.residents.row_ids[row];
    for (const CartPlan& cart : current.groom_plan.carts) {
      if (cart.driver.value != driver.value || cart.on_foot) {
        continue;
      }
      for (const CartLeg& leg : cart.legs) {
        const std::uint32_t task_row = FindRow(current.logistics_tasks, leg.task);
        if (task_row == kNoRow) {
          continue;
        }
        const LogisticsTaskRow& task = current.logistics_tasks.rows[task_row];
        const float* const seam = WorkSeamOf(current, HaulingWorkOf(task));
        if (seam != nullptr && *seam > 0.0F) {
          RetargetWork(work, task);
          return true;
        }
      }
      return false;
    }
    return false;
  }

  /// @brief A carter whose load is carted (its seam empty) or gone moves on
  ///        to the first load of his chain in today's plan that still has
  ///        carting left (B4) — on a horse, and since B4b on foot too (the
  ///        nearest loads of his level, logistics_plan.h). His horse, his road
  ///        and his day's pay stay: the way between two loads is the empty
  ///        half of the new load's first trip, priced in its seam (0.37.139).
  ///        Since 0.37.312 a site whose works still await is not «carted» at
  ///        its leg change: it lets him go on only to its own level or above.
  ///        Off a site he still follows down the levels: «never to a
  ///        lower level» was tried and left the heaps in the field at the
  ///        frost (+15 % canon; core-boss-oats-b-pair [55]).
  ///        With no plan for today, no chain, or no load left, nothing
  ///        changes.
  static void FollowThePlan(WorldState& current, std::uint32_t row) {
    WorkAssignment& work = current.residents.rows[row].work;
    if (work.kind != WorkKind::kHauling || current.groom_plan.day != current.calendar.day) {
      return;
    }
    const float* const seam = WorkSeamOf(current, work);
    if (seam != nullptr && *seam > 0.0F) {
      return;  // still carting his load
    }
    // A SITE'S SEAM IS ITS FRONT LEG'S ALONE (unit_state.h, supply_days_
    // remaining): it stands empty the hour one leg is carted and the next is
    // not yet written, while the works still await the rest. That hour is not
    // «carted». Until 0.37.312 it read as carted, and at a house's leg changes
    // two of its three carters followed their chains to level-3 manure doses:
    // the house waited 1.79 t of clay through the spring's field days and
    // stood five days late (boss, core-boss-oats-b-pair [52]-[53]). The task
    // stands till tomorrow's hour 0 even when its load is gone (logistics_
    // system.cpp, SyncTasks); no task names it, nothing holds him.
    const std::uint32_t own_row = TaskRowOfWork(current, work);
    // And it lets him go on only to a load of its own level or a more urgent
    // one: to the same-level heaps he goes as before - the meadows' hay the
    // site's carters fetch, which rots every night it lies (holding him there
    // too cost hay +6 % rot canon, +9 % novice; core-boss-oats-b-pair [59]).
    bool site_holds = false;
    std::uint8_t site_level = 0;
    if (own_row != kNoRow) {
      const LogisticsTaskRow& own = current.logistics_tasks.rows[own_row];
      if (own.load_kind == LogisticsLoadKind::kSiteSupply) {
        const std::uint32_t site = FindRow(current.units, own.unit);
        if (site != kNoRow && AnythingAwaits(current.units.rows[site].construction)) {
          site_holds = true;
          site_level = static_cast<std::uint8_t>(own.level);
        }
      }
    }
    const ResidentId driver = current.residents.row_ids[row];
    for (const CartPlan& cart : current.groom_plan.carts) {
      if (cart.driver.value != driver.value) {
        continue;
      }
      for (const CartLeg& leg : cart.legs) {
        const std::uint32_t task_row = FindRow(current.logistics_tasks, leg.task);
        if (task_row == kNoRow) {
          continue;  // its load was carted and its task ended
        }
        const LogisticsTaskRow& task = current.logistics_tasks.rows[task_row];
        if (site_holds && static_cast<std::uint8_t>(task.level) > site_level) {
          continue;
        }
        const float* const next = WorkSeamOf(current, HaulingWorkOf(task));
        if (next != nullptr && *next > 0.0F) {
          RetargetWork(work, task);
          return;
        }
      }
      return;
    }
  }

  /// The seam this assignment drains — core_common/work_seam.h, shared with
  /// whoever asks what a man is doing this hour. It lived here until
  /// 2026-09-05, and the activity needed the same seven cases to tell "no
  /// order" from "an order and nothing to work with".
  static float* WorkSeam(WorldState& current, const WorkAssignment& work) {
    return WorkSeamOf(current, work);
  }

  static bool WorkPlace(const WorldState& current, const WorkAssignment& work, Vec2& place) {
    return WorkPlaceOf(current, work, place);
  }

  // -- the day's close -----------------------------------------------------

  void CloseDay(WorldState& current) const {
    for (ResidentRow& resident : current.residents.rows) {
      if (resident.work.kind != WorkKind::kNone) {
        PayDay(current, resident);
      }
    }
    const bool day_off = IsDayOffIn(current, current.calendar.day);
    for (ResidentRow& resident : current.residents.rows) {
      // The daily rest balance of decision 107: a day worked is the drain
      // already charged hour by hour and nothing back; a day at home on a
      // working day is worth +4, a whole day off +8. (The +15 with leisure
      // and the +20 of a holiday wait for clubs and events.)
      const bool worked = resident.work.hours_away_today > 0.0F;
      // The month's worked days, read and cleared by the drinking's month
      // turn (core_residents/alcoholism.h). Held at the byte's top, which a
      // four-day month never reaches.
      if (worked && resident.days_worked_this_month < UINT8_MAX) {
        ++resident.days_worked_this_month;
      }
      // A WALK-OFF IS A DAY AT HOME (leisure design, the day's table: «День
      // дома без наряда | +4 | Декрет, болезнь, ушёл за предел»; 0.35.14). It
      // counted as worked until then — he was out an hour — and a man who
      // walked off in his first hour never rested back. The walk-off leaves
      // him at or under the limit, and nobody else ends a day there: since
      // 0.35.11 a man past his limit is not sent at all.
      const bool walked_off = worked && resident.rest <= config_.rest_walkoff_threshold;
      if (!worked || walked_off) {
        resident.rest += day_off ? config_.rest_recovery_day_off : config_.rest_recovery_idle_day;
        resident.rest = resident.rest > kMetricMax ? kMetricMax : resident.rest;
      }
      // Living under the spent threshold costs health, a point a week
      // (decision 107). Nothing else in phase 1 punishes exhaustion.
      if (resident.rest < config_.efficiency.rest_step_spent) {
        resident.health -= config_.health_loss_per_spent_day;
        resident.health = resident.health < kMetricMin ? kMetricMin : resident.health;
      }
      resident.work = WorkAssignment{};
    }
    CloseRushDay(current);
  }

  /// Turns a day of delivered norm-days into trudodni on the FAMILY account
  /// (labor-payment §2: the account is the household's) and closes the
  /// assignment. Called at the day's end and at a walk-off, which ends the
  /// working day early — a trudoden is a work norm, not attendance, so what
  /// he did deliver is paid.
  void PayDay(WorldState& current, ResidentRow& resident) const {
    // What the avral and the worked day off cost him, while his assignment
    // still says what he did (rush.h).
    BookRushAtPay(config_, current, resident);
    const auto kind_index = static_cast<std::uint32_t>(resident.work.kind);
    // The ledger books the DELIVERED work whatever becomes of the pay:
    // man-days per kind are what the reconciliation compares against the
    // agronomy norms, and a worker whose household has since dissolved
    // still ploughed.
    if (kind_index < current.ledger.current.work_days_by_kind.size()) {
      current.ledger.current.work_days_by_kind[kind_index] += resident.work.worked_norm_days_today;
    }
    // THE REAPING OF THE ARABLE, apart from the meadow cut that shares its
    // kind: the pace the harvest-will-not-be-gathered alarm reads.
    if (resident.work.kind == WorkKind::kHarvest) {
      const std::uint32_t field_row = FindRow(current.fields, resident.work.field);
      if (field_row != kNoRow && current.fields.rows[field_row].kind == LandKind::kArable) {
        current.ledger.current.reaping_today += resident.work.worked_norm_days_today;
        // One reaper more under today's light (ledger_state.h, `hours`): how
        // much of the village the queue put on the reaping. Today's light and
        // not yesterday's: a man is paid within his day.
        current.ledger.current.reaping_today_hours += DaylightHoursOfDay(current.calendar.day);
      }
    }
    if (kind_index < config_.rates.size() && resident.work.worked_norm_days_today > 0.0F) {
      const float trudodni =
          config_.rates[kind_index].trudodni_rate * resident.work.worked_norm_days_today;
      const std::uint32_t family_row = FindRow(current.families, resident.family);
      if (family_row != kNoRow) {
        const auto hundredths =
            static_cast<TrudodniHundredths>(std::lround(trudodni * kTrudodniScale));
        current.families.rows[family_row].trudodni_account += hundredths;
        // THE NEW HOUSEHOLD'S OWN SUM (#170; FamilyRow::earned_since_formed):
        // what it has earned since it was formed, kept apart from the account
        // because the account burns at the economic year's turn.
        if (current.families.rows[family_row].formed_day != kNotFormedInPlay) {
          current.families.rows[family_row].earned_since_formed += hundredths;
        }
        current.ledger.current.trudodni_accrued += hundredths;
        // And on its day of the year: next year's issue norm forecasts the
        // trudodni to a harvest from these (ledger_state.h, save 117).
        current.ledger.current.trudodni_by_day[current.calendar.day % kDaysPerYear] += hundredths;
      }
    }
    resident.work.kind = WorkKind::kNone;
    resident.work.field = FieldId{};
    resident.work.herd = HerdId{};
    resident.work.worked_norm_days_today = 0.0F;
    resident.work.travel_hours = -1.0F;
  }

  // Household hours used to be settled here, as the bare remainder of the
  // day. Since stage 6 the whole number — remainder plus the factors of
  // household design §1 — is computed in the metrics phase
  // (core_residents/household_plot.cpp), at hour 22, while the day's orders
  // are still alive. One writer, one place of truth.

  LaborConfig config_;

  /// The activity's thresholds, filled once from config_ (ActivityRulesOfConfig).
  ActivityRules activity_rules_;
};

}  // namespace

std::unique_ptr<ILaborSystem> CreateLaborSystem(
    const ITableSet& tables,
    StubTables stubs,
    std::uint32_t growing_season_last_day,
    std::function<Grams(const WorldState&, const FieldRow&)> standing_crop_grams,
    const RainDayShares& rain_day_shares,
    std::function<bool(const WorldState&, std::uint32_t)> stored_hay_short,
    const SnowLainShares& snow_lain_shares,
    std::function<std::vector<ShortRickUnit>(const WorldState&, std::uint32_t)> short_ricks) {
  // THE DEFAULTS ARE LEGITIMATE AND THEIR SILENCE WAS NOT
  // (core_tables/stub_tables.h). A caller that has not said it wants
  // this module's documented defaults is refused by name, so that a
  // table set which is merely INCOMPLETE cannot pass for one that is
  // as its author meant it.
  //
  // THE LIST IS THE WHOLE READ SET (core_tables/required_tables.h): the last
  // five joined it on 2026-09-08, having been read by ParseLaborConfig and
  // silently defaulted when absent.
  if (!RequireTables(tables,
                     stubs,
                     "labor",
                     {"labor",
                      "professions",
                      "unit_types",
                      "transport",
                      "life",
                      "livestock",
                      "crops",
                      "unit_staff"},
                     nullptr)) {
    return nullptr;
  }

  LaborConfig config;
  std::string error;
  if (!ParseLaborConfig(tables, config, error)) {
    LogError(error);
    return nullptr;
  }
  config.growing_season_last_day = growing_season_last_day;
  config.standing_crop_grams = std::move(standing_crop_grams);
  config.rain_day_shares = rain_day_shares;
  config.stored_hay_short = std::move(stored_hay_short);
  config.short_ricks = std::move(short_ricks);
  config.early_snow_last_day =
      EarlySnowLastDay(snow_lain_shares, config.early_snow_share, growing_season_last_day);
  return std::make_unique<LaborSystem>(std::move(config));
}

}  // namespace core
