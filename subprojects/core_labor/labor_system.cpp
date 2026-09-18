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
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "assignment.h"
#include "core_common/alarm_state.h"
#include "core_common/calendar.h"
#include "core_common/day_off.h"
#include "core_common/emit_event.h"
#include "core_common/event_state.h"
#include "core_common/family_state.h"
#include "core_common/geometry.h"
#include "core_common/herd_state.h"
#include "core_common/ids.h"
#include "core_common/labor_state.h"
#include "core_common/land_state.h"
#include "core_common/ledger_state.h"
#include "core_common/module_rules.h"
#include "core_common/quantities.h"
#include "core_common/reaping_pace.h"
#include "core_common/resident_state.h"
#include "core_common/state_table.h"
#include "core_common/state_table_ops.h"
#include "core_common/work_seam.h"
#include "core_common/world_state.h"
#include "core_log/log.h"
#include "core_tables/required_tables.h"
#include "core_tables/tables.h"
#include "labor_config.h"
#include "labor_day.h"
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

class LaborSystem final : public ILaborSystem {
 public:
  explicit LaborSystem(LaborConfig config) : config_(std::move(config)) {}

  void RunAssignmentDecisions(const WorldState& /*previous*/, WorldState& current) override {
    const std::uint32_t hour = HourFromTick(current.calendar.tick);
    if (hour == 0) {
      StartDay(current);
    }
    if (hour == 1) {
      TopUpDay(current);
    }
    RunHour(current, hour);
    // The night posts go on at sunset (posts.h).
    AnnounceNightShifts(config_, current);
    if (hour + 1U >= kTicksPerDay) {
      CloseDay(current);
      ApplyPostOrders(current);
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
    for (const ResidentRow& resident : state.residents.rows) {
      if (!Employable(state, resident)) {
        continue;
      }
      ++count.employable;
      count.idle += resident.work.kind == WorkKind::kNone ? 1U : 0U;
    }
    return count;
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
  void StartDay(WorldState& current) const {
    for (ResidentRow& resident : current.residents.rows) {
      resident.work = WorkAssignment{};
    }
    StandDownRushes(current);
    // The reaping pace rolls over: yesterday's whole day of hand reaping on
    // the arable is a candidate for the season's best (ledger_state.h).
    // Its daylight goes with it (save 64): the best day's pace is only
    // readable against the sun it was reaped under.
    YearLedger& book = current.ledger.current;
    if (book.reaping_today > book.reaping_best_day) {
      book.reaping_best_day = book.reaping_today;
      book.reaping_best_day_daylight = book.reaping_today_daylight;
    }
    book.reaping_today = 0.0F;
    book.reaping_today_daylight = 0.0F;
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
    if (!jobs.empty()) {
      const std::vector<AssignmentCandidate> candidates = CollectCandidates(current);
      if (!candidates.empty()) {
        const std::vector<std::uint32_t> plan =
            PlanDayAssignments(jobs, candidates, DayParams(current));
        for (std::uint32_t index = 0; index < candidates.size(); ++index) {
          if (plan[index] == kNoJobAssigned) {
            continue;
          }
          const AssignmentJob& job = jobs[plan[index]];
          WorkAssignment& work = current.residents.rows[candidates[index].resident_row].work;
          work.kind = job.kind;
          work.field = job.field;
          work.herd = job.herd;
          work.unit = job.unit;
          work.stand = job.stand;
          work.extraction_site = job.extraction_site;
        }
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
    ApplyStandingWork(current, current, IsDayOffIn(current, current.calendar.day));
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
  void TopUpDay(WorldState& current) const {
    std::vector<AssignmentJob> jobs = CollectJobs(current);
    const auto crewed = [&current](const AssignmentJob& job) {
      return std::ranges::any_of(current.residents.rows, [&job](const ResidentRow& person) {
        const WorkAssignment& work = person.work;
        return work.kind == job.kind && work.field.value == job.field.value &&
               work.herd.value == job.herd.value && work.unit.value == job.unit.value &&
               work.stand.value == job.stand.value &&
               work.extraction_site.value == job.extraction_site.value;
      });
    };
    std::erase_if(jobs, crewed);
    if (jobs.empty()) {
      return;
    }
    std::vector<AssignmentCandidate> candidates = CollectCandidates(current);
    std::erase_if(candidates, [&current](const AssignmentCandidate& candidate) {
      return current.residents.rows[candidate.resident_row].work.kind != WorkKind::kNone;
    });
    if (candidates.empty()) {
      return;
    }
    const std::vector<std::uint32_t> plan =
        PlanDayAssignments(jobs, candidates, DayParams(current));
    for (std::uint32_t index = 0; index < candidates.size(); ++index) {
      if (plan[index] == kNoJobAssigned) {
        continue;
      }
      const AssignmentJob& job = jobs[plan[index]];
      WorkAssignment& work = current.residents.rows[candidates[index].resident_row].work;
      work.kind = job.kind;
      work.field = job.field;
      work.herd = job.herd;
      work.unit = job.unit;
      work.stand = job.stand;
      work.extraction_site = job.extraction_site;
    }
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
      if (resident.post.profession.value == kInvalidDefIdValue) {
        continue;
      }
      for (std::uint32_t row = 0; row < current.herds.rows.size(); ++row) {
        const HerdRow& herd = current.herds.rows[row];
        if (herd.unit.value != resident.post.unit.value || herd.care_days_remaining <= 0.0F) {
          continue;
        }
        resident.work.kind = WorkKind::kHerdCare;
        resident.work.herd = current.herds.row_ids[row];
        break;
      }
      if (resident.work.kind != WorkKind::kNone || day_off) {
        continue;
      }
      PutOnModuleWork(current, resident, places_taken);
    }
  }

  /// The first module of the holder's unit, in row order, that has work
  /// today and a free place. Whether it can work at all is asked of the same
  /// seam the working hour drains (WorkSeamOf): a paused sawmill or a yard
  /// that fell still answers nullptr there, and one rule serves both.
  void PutOnModuleWork(WorldState& current,
                       ResidentRow& resident,
                       std::vector<std::uint32_t>& places_taken) const {
    if (resident.post.unit.value == kInvalidEntityIdValue) {
      return;
    }
    for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
      const UnitRow& unit = current.units.rows[row];
      if (unit.parent.value != resident.post.unit.value ||
          places_taken[row] >= UnitWorkPlaces(config_.timber, unit.type)) {
        continue;
      }
      WorkAssignment work;
      work.kind = WorkKind::kUnitWork;
      work.unit = current.units.row_ids[row];
      const float* const seam = WorkSeamOf(current, work);
      if (seam == nullptr || *seam <= 0.0F) {
        continue;
      }
      resident.work = work;
      ++places_taken[row];
      return;
    }
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
        AssignmentJob job;
        job.kind = kind;
        job.field = current.fields.row_ids[row];
        job.position = field.center;
        job.work_days_remaining = field.work_days_remaining;
        job.window = FieldWindow(current.calendar, field, kind);
        if (config_.standing_crop_grams && InSnowLastDays(current.calendar, field, kind)) {
          job.grams_at_risk = config_.standing_crop_grams(current, field);
        }
        job.prepares_winter_crop = PreparesWinterCrop(field, kind);
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
        job.window = HaulWindow(current);
        jobs.push_back(job);
      }
    }
    // The timber stands (timber design §8a, 2026-09-13): felling, windowless
    // and capped by the tools in the stores, and the carting of the logs lying
    // there, exactly as a field's load is carted.
    if (!day_off) {
      const std::uint32_t crew_cap =
          FellingCrewCap(config_.timber, TotalHeld(current, config_.timber.tool_resource));
      for (std::uint32_t row = 0; row < current.stands.rows.size(); ++row) {
        const TimberStandRow& stand = current.stands.rows[row];
        if (stand.marked_m3 > 0.0F && stand.work_days_remaining > 0.0F && crew_cap > 0) {
          AssignmentJob job;
          job.kind = WorkKind::kFelling;
          job.stand = current.stands.row_ids[row];
          job.position = stand.position;
          job.work_days_remaining = stand.work_days_remaining;
          job.max_crew = static_cast<std::uint8_t>(crew_cap);
          jobs.push_back(job);
        }
        if (stand.load_grams > 0 && stand.haul_days_remaining > 0.0F) {
          AssignmentJob job;
          job.kind = WorkKind::kHauling;
          job.stand = current.stands.row_ids[row];
          job.position = stand.position;
          job.work_days_remaining = stand.haul_days_remaining;
          job.harnessed = DraughtHorses(current) > 0;
          job.window = HaulWindow(current);
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
          job.window = HaulWindow(current);
          jobs.push_back(job);
        }
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
        if (unit.construction.labor_days_remaining <= 0.0F || !ModuleParentSound(current, unit) ||
            unit.paused != 0) {
          continue;
        }
        AssignmentJob job;
        job.kind = WorkKind::kConstruction;
        job.unit = current.units.row_ids[row];
        job.position = unit.position;
        job.work_days_remaining = unit.construction.labor_days_remaining;
        job.max_crew = unit.construction.max_crew;
        jobs.push_back(job);
      }
    }
    MarkTheReapingsTheSnowWillTake(current, jobs);
    return jobs;
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
  /// THE CAPACITY IS THE ALARM'S PACE (core_common/reaping_pace.h; boss,
  /// core-host-l1-stage1 seq 28) times the working days to the snow
  /// inclusive. It was every employable hand at today's daylight over the
  /// standard day, efficiency one, until 2026-09-19, and host's seed 9 with
  /// the reaping made three times longer showed what that costs: 46 hands
  /// reaped 22.7 norm-days a November day against about 39 counted, both
  /// fields read as finishable, and the first step of the rule could not
  /// tell them apart. Still an optimist's: the light keeps falling to the
  /// snow and the pace is today's.
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
    std::uint32_t working_days = 0;
    for (std::uint32_t day = today; day <= config_.growing_season_last_day; ++day) {
      const SimDay sim_day = current.calendar.day - today + day;
      working_days += IsDayOffIn(current, sim_day) ? 0U : 1U;
    }
    double capacity =
        ReapingPacePerDay(current.ledger.current, current.weather.daylight_hours, hands) *
        static_cast<double>(working_days);
    for (const std::uint32_t index : reapings) {
      AssignmentJob& job = jobs[index];
      if (static_cast<double>(job.work_days_remaining) <= capacity) {
        capacity -= static_cast<double>(job.work_days_remaining);
      } else {
        job.beyond_the_snow = true;
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
  static Deadline HaulWindow(const WorldState& current) {
    const std::uint32_t day_of_year = current.calendar.day % kDaysPerYear;
    return DeadlineInDays(static_cast<std::int32_t>(kDaysPerYear - day_of_year - 1U));
  }

  /// The crop a field job works toward: the one opened on the field, else
  /// this year's in the rotation, else — a fallow year — next year's.
  static CropId JobCrop(const FieldRow& field) {
    if (field.crop.value != kInvalidDefIdValue) {
      return field.crop;
    }
    return field.rotation_year0.value != kInvalidDefIdValue ? field.rotation_year0
                                                            : field.rotation_year1;
  }

  /// Whether this field job — ploughing, harrowing or sowing — works toward a
  /// WINTER crop of next year's slot: the fallow ploughed for it in spring,
  /// and the autumn's ploughing and sowing of it. The job then takes that
  /// crop's window and ranks below every job with a window of its own
  /// (assignment.h, prepares_winter_crop). A winter crop already of this
  /// year's slot is standing and only reaped, so it never qualifies.
  bool PreparesWinterCrop(const FieldRow& field, WorkKind kind) const {
    const CropId crop = JobCrop(field);
    return kind != WorkKind::kHarvest && crop.value < config_.crops.size() &&
           config_.crops[crop.value].is_winter != 0 && crop.value != field.rotation_year0.value;
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
    CropId crop = field.crop.value != kInvalidDefIdValue ? field.crop : field.rotation_year0;
    if (PreparesWinterCrop(field, kind)) {
      crop = JobCrop(field);
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
        (harvest || (ploughed && !PreparesWinterCrop(field, kind)))) {
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
  bool Employable(const WorldState& state, const ResidentRow& resident) const {
    Vec2 home;
    const float age = BiologicalAgeYears(config_, resident.birth_day, state.calendar.day);
    return age >= config_.adult_age_years && HomePosition(state, resident.family, home);
  }

  /// Everyone of working age whose day can start somewhere. Children are
  /// left out entirely: child labor (life-cycle §7) is deferred.
  std::vector<AssignmentCandidate> CollectCandidates(const WorldState& current) const {
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
      AssignmentCandidate candidate;
      candidate.resident_row = row;
      candidate.home = home;
      // Ranked before any work is chosen, so the sober factor is the general
      // one: the three spared works (AlcoholSparesWork) are spared where the
      // work is delivered, below, and not in who comes first to the list.
      candidate.efficiency = ResidentEfficiency(
          config_, resident, age, aging_from, current.calendar.date.year == 0, false);
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

  /// Adult kolkhoz horses, wherever they stand: the shared draught pool of
  /// the day (a horse at a private yard is still a kolkhoz horse).
  std::uint32_t DraughtHorses(const WorldState& current) const {
    if (config_.horse_kind.value == kInvalidDefIdValue) {
      return 0;
    }
    std::uint32_t horses = 0;
    for (const HerdRow& herd : current.herds.rows) {
      if (herd.kind.value == config_.horse_kind.value) {
        horses += herd.adult_count;
      }
    }
    return horses;
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
    params.travel_limit_hours = config_.travel_limit_hours;
    // The queue advances by one a day and wraps on the roster.
    params.rotation = static_cast<std::uint32_t>(current.calendar.day);
    params.roster = static_cast<std::uint32_t>(current.residents.rows.size());
    params.min_usable_hours = config_.min_usable_hours;
    params.standard_day_hours = config_.standard_day_hours;
    params.draught_horses = DraughtHorses(current);
    params.placement_level = config_.placement_level;
    return params;
  }

  // -- the working hour ----------------------------------------------------

  /// One hour of work for everybody who has an assignment and is inside his
  /// own window. Row order, sequentially: two workers draining the same
  /// seam must always drain it in the same order.
  void RunHour(WorldState& current, std::uint32_t hour) const {
    const DayWindow window = SolarWindow(current.weather.daylight_hours);
    for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
      const WorkKind kind = current.residents.rows[row].work.kind;
      if (kind == WorkKind::kNone) {
        continue;
      }
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
      // Asked of the ASSIGNMENT and not of the kind: a meadow's cut rides and
      // a strip's harvest walks, one kind between them (work_seam.h).
      const WorkKind road_kind = WorkRidesOut(current, current.residents.rows[row].work)
                                     ? WorkKind::kPlowing
                                     : WorkKind::kHarvest;
      const float travel = TravelHours(home, target, HoursPerKm(config_, road_kind));
      const float worked = HoursInside(hour, window.sunrise + travel, window.sunset - travel);
      if (worked <= 0.0F) {
        continue;
      }
      ResidentRow& resident = current.residents.rows[row];
      if (resident.work.hours_away_today <= 0.0F) {
        resident.work.hours_away_today = 2.0F * travel;  // the round trip, booked once
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
      delivered = delivered > *seam ? *seam : delivered;
      if (delivered <= 0.0F) {
        continue;  // the job is done for today; he stands about, unpaid
      }
      *seam -= delivered;
      resident.work.worked_norm_days_today += delivered;
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
      if (!worked) {
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
        current.ledger.current.reaping_today_daylight = current.weather.daylight_hours;
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
        current.ledger.current.trudodni_accrued += hundredths;
      }
    }
    resident.work.kind = WorkKind::kNone;
    resident.work.field = FieldId{};
    resident.work.herd = HerdId{};
    resident.work.worked_norm_days_today = 0.0F;
  }

  // Household hours used to be settled here, as the bare remainder of the
  // day. Since stage 6 the whole number — remainder plus the factors of
  // household design §1 — is computed in the metrics phase
  // (core_residents/household_plot.cpp), at hour 22, while the day's orders
  // are still alive. One writer, one place of truth.

  LaborConfig config_;
};

}  // namespace

std::unique_ptr<ILaborSystem> CreateLaborSystem(
    const ITableSet& tables,
    StubTables stubs,
    std::uint32_t growing_season_last_day,
    std::function<Grams(const WorldState&, const FieldRow&)> standing_crop_grams) {
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
  return std::make_unique<LaborSystem>(std::move(config));
}

}  // namespace core
