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
#include <utility>
#include <vector>

#include "assignment.h"
#include "core_common/calendar.h"
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
#include "core_common/world_state.h"
#include "core_log/log.h"
#include "core_tables/tables.h"
#include "labor_config.h"
#include "labor_day.h"

namespace core {
namespace {

/// @brief The work a field in this phase is waiting for; kNone when the
/// field is growing, idle or fallow.
constexpr WorkKind KindOfPhase(FieldPhase phase) {
  switch (phase) {
    case FieldPhase::kPlowing:
      return WorkKind::kPlowing;
    case FieldPhase::kHarrowing:
      return WorkKind::kHarrowing;
    case FieldPhase::kSowing:
      return WorkKind::kSowing;
    case FieldPhase::kHarvest:
      return WorkKind::kHarvest;
    case FieldPhase::kIdle:
    case FieldPhase::kGrowing:
      return WorkKind::kNone;
  }
  return WorkKind::kNone;
}

/// @brief Whole days left until the end of `month_end`, capped at 254 (255
/// is the "no window" value of AssignmentJob).
std::uint8_t DaysLeftInWindow(const CalendarState& calendar, std::uint8_t month_end) {
  const std::uint32_t day_of_year = calendar.day % kDaysPerYear;
  const std::uint32_t window_end = (static_cast<std::uint32_t>(month_end) + 1U) * kDaysPerMonth;
  if (window_end <= day_of_year) {
    return 0;  // the window has closed; the work is as urgent as it gets
  }
  const std::uint32_t left = window_end - day_of_year - 1U;
  return left > 254U ? 254U : static_cast<std::uint8_t>(left);
}

/// @brief Position a resident's day starts and ends at: his family's house.
/// Returns false for a family without a house — genesis gives every family
/// one, so this is the guard for hand-built worlds, not a normal case.
bool HomePosition(const WorldState& world, FamilyId family, Vec2& home) {
  const std::uint32_t family_row = FindRow(world.families, family);
  if (family_row == kNoRow) {
    return false;
  }
  const std::uint32_t house_row = FindRow(world.units, world.families.rows[family_row].house);
  if (house_row == kNoRow) {
    return false;
  }
  home = world.units.rows[house_row].position;
  return true;
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
    }
  }

 private:
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
    const std::vector<AssignmentJob> jobs = CollectJobs(current);
    if (jobs.empty()) {
      return;
    }
    const std::vector<AssignmentCandidate> candidates = CollectCandidates(current);
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

  /// Everyone of working age whose day can start somewhere. Children are
  /// left out entirely: child labor (life-cycle §7) is deferred.
  std::vector<AssignmentCandidate> CollectCandidates(const WorldState& current) const {
    const std::vector<bool> horse_locked = MarkHorseHosts(current);
    const float aging_from = AgingFromYears(config_, current);
    std::vector<AssignmentCandidate> candidates;
    candidates.reserve(current.residents.rows.size());
    for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
      const ResidentRow& resident = current.residents.rows[row];
      const float age = BiologicalAgeYears(config_, resident.birth_day, current.calendar.day);
      Vec2 home;
      if (age < config_.adult_age_years || !HomePosition(current, resident.family, home)) {
        continue;
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
    if (config_.horse_kind.value == kInvalidDefIdValue) {
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
        PayDay(current, resident);
      }
    }
  }

  /// The seam this assignment drains, or nullptr when its target is gone.
  static float* WorkSeam(WorldState& current, const WorkAssignment& work) {
    if (work.kind == WorkKind::kHerdCare) {
      const std::uint32_t row = FindRow(current.herds, work.herd);
      return row == kNoRow ? nullptr : &current.herds.rows[row].care_days_remaining;
    }
    if (work.kind == WorkKind::kConstruction) {
      const std::uint32_t row = FindRow(current.units, work.unit);
      // A site that finished or was demolished since the morning: the crew
      // simply has nothing to drain, exactly as with a field production has
      // moved on.
      return row == kNoRow ? nullptr : &current.units.rows[row].construction.labor_days_remaining;
    }
    const std::uint32_t row = FindRow(current.fields, work.field);
    if (row == kNoRow || KindOfPhase(current.fields.rows[row].phase) != work.kind) {
      return nullptr;  // production has moved the field on since the morning
    }
    return &current.fields.rows[row].work_days_remaining;
  }

  static bool WorkPlace(const WorldState& current, const WorkAssignment& work, Vec2& place) {
    if (work.kind == WorkKind::kHerdCare) {
      const std::uint32_t row = FindRow(current.herds, work.herd);
      return row != kNoRow && HerdPosition(current, current.herds.rows[row], place);
    }
    if (work.kind == WorkKind::kConstruction) {
      const std::uint32_t row = FindRow(current.units, work.unit);
      if (row == kNoRow) {
        return false;
      }
      place = current.units.rows[row].position;
      return true;
    }
    const std::uint32_t row = FindRow(current.fields, work.field);
    if (row == kNoRow) {
      return false;
    }
    place = current.fields.rows[row].center;
    return true;
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

std::unique_ptr<ILaborSystem> CreateLaborSystem(const ITableSet& tables) {
  LaborConfig config;
  std::string error;
  if (!ParseLaborConfig(tables, config, error)) {
    LogError(error);
    return nullptr;
  }
  return std::make_unique<LaborSystem>(std::move(config));
}

}  // namespace core
