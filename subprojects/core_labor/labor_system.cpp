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
#include "core_common/emit_event.h"
#include "core_common/event_state.h"
#include "core_common/family_state.h"
#include "core_common/geometry.h"
#include "core_common/herd_state.h"
#include "core_common/ids.h"
#include "core_common/labor_state.h"
#include "core_common/land_state.h"
#include "core_common/ledger_state.h"
#include "core_common/quantities.h"
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
#include "work_orders.h"

namespace core {
namespace {

/// @brief Whole days left until the end of `month_end`, capped at 254 (255
/// is the "no window" value of AssignmentJob).
std::uint8_t DaysLeftInWindow(const CalendarState& calendar, std::uint8_t month_end) {
  const std::uint32_t day_of_year = calendar.day % kDaysPerYear;
  const std::uint32_t window_end = (static_cast<std::uint32_t>(month_end) + 1U) * kDaysPerMonth;
  if (window_end <= day_of_year) {
    // THE WINDOW HAS CLOSED, AND THIS ZERO IS UNDER QUESTION (2026-09-12).
    // It says "as urgent as it gets", and OrderJobs ranks the smallest first,
    // so work that can no longer produce anything this year outranks work
    // whose window is still open. On the shipped seventy hectares it never
    // shows. On a hundred and sixty-three it is a ratchet: the village goes
    // on ploughing ground it can no longer sow, never cuts the hay, and the
    // team starves — measured, and the counterfactual (this line returning
    // 254) brings the same run back to fifty-two horses and a green arm.
    // Reported to boss; the repair is his call, because "closed" and "due
    // today" wanting different values is a design decision, not a typo.
    return 0;
  }
  const std::uint32_t left = window_end - day_of_year - 1U;
  return left > 254U ? 254U : static_cast<std::uint8_t>(left);
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
    RunHour(current, hour);
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
    ApplyStandingWork(current, current, IsDayOff(current.calendar.weekday, current.epoch));
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
  void AssignPostHolders(WorldState& current) const {
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
    const bool day_off = IsDayOff(current.calendar.weekday, current.epoch);
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
        job.window_days_left = FieldWindow(current.calendar, field, kind);
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
        job.window_days_left = HaulWindow(current);
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
      job.window_days_left = 0;  // undone barn work expires tonight
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
        if (unit.construction.labor_days_remaining <= 0.0F) {
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
    return jobs;
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
  std::uint8_t HaulWindow(const WorldState& current) const {
    const std::uint32_t day_of_year = current.calendar.day % kDaysPerYear;
    const std::uint32_t left = kDaysPerYear - day_of_year - 1U;
    return left > 254U ? 254U : static_cast<std::uint8_t>(left);
  }

  /// Urgency of a field job: days until the crop's own window closes. The
  /// crop is the one in the ground, or — while the field is still being
  /// prepared — the one the rotation plans for this year.
  std::uint8_t FieldWindow(const CalendarState& calendar,
                           const FieldRow& field,
                           WorkKind kind) const {
    const CropId crop = field.crop.value != kInvalidDefIdValue ? field.crop : field.rotation_year0;
    if (crop.value >= config_.crops.size()) {
      return 255;  // an unknown crop has no window; anything else outranks it
    }
    const CropWindows& windows = config_.crops[crop.value];
    return DaysLeftInWindow(
        calendar, kind == WorkKind::kHarvest ? windows.harvest_to_month : windows.sow_to_month);
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
      if (resident.post.profession.value != kInvalidDefIdValue) {
        continue;  // he has a place of his own; the accountant does not touch him
      }
      AssignmentCandidate candidate;
      candidate.resident_row = row;
      candidate.home = home;
      candidate.efficiency = ResidentEfficiency(config_, resident, age, aging_from);
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
    params.walk_hours_per_km = HoursPerKm(config_, WorkKind::kSowing);
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
      const float travel = TravelHours(home, target, HoursPerKm(config_, kind));
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
      const float efficiency =
          ResidentEfficiency(config_, resident, age, AgingFromYears(config_, current));
      float delivered = worked * efficiency / config_.standard_day_hours;
      delivered = delivered > *seam ? *seam : delivered;
      if (delivered <= 0.0F) {
        continue;  // the job is done for today; he stands about, unpaid
      }
      *seam -= delivered;
      resident.work.worked_norm_days_today += delivered;
      const float drain = RestDrain(config_, resident, kind, delivered);
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
    const bool day_off = IsDayOff(current.calendar.weekday, current.epoch);
    for (ResidentRow& resident : current.residents.rows) {
      // The daily rest balance of decision 107: a day worked is the drain
      // already charged hour by hour and nothing back; a day at home on a
      // working day is worth +4, a whole day off +8. (The +15 with leisure
      // and the +20 of a holiday wait for clubs and events.)
      const bool worked = resident.work.hours_away_today > 0.0F;
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
  }

  /// Turns a day of delivered norm-days into trudodni on the FAMILY account
  /// (labor-payment §2: the account is the household's) and closes the
  /// assignment. Called at the day's end and at a walk-off, which ends the
  /// working day early — a trudoden is a work norm, not attendance, so what
  /// he did deliver is paid.
  void PayDay(WorldState& current, ResidentRow& resident) const {
    const auto kind_index = static_cast<std::uint32_t>(resident.work.kind);
    // The ledger books the DELIVERED work whatever becomes of the pay:
    // man-days per kind are what the reconciliation compares against the
    // agronomy norms, and a worker whose household has since dissolved
    // still ploughed.
    if (kind_index < current.ledger.current.work_days_by_kind.size()) {
      current.ledger.current.work_days_by_kind[kind_index] += resident.work.worked_norm_days_today;
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

std::unique_ptr<ILaborSystem> CreateLaborSystem(const ITableSet& tables, StubTables stubs) {
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
  return std::make_unique<LaborSystem>(std::move(config));
}

}  // namespace core
