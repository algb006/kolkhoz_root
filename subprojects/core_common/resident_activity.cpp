// What a resident is doing right now (resident_activity.h).
//
// EVERY PREDICATE IS EVALUATED AND THE HIGHEST-PRIORITY ONE WINS, rather
// than a chain of ifs returning early. The two read the same on a good day
// and differ on the day somebody inserts a case in the middle: a chain
// silently changes every answer below the insertion, and this cannot,
// because the order is the enum's and the enum's order is the table's. That
// is the whole of what resident_activities.csv's `priority` column buys.
//
// FIVE OF THE FOURTEEN HAVE NO SOURCE IN THE MODEL and are marked STUB one
// by one below. They are not guessed at: a state nothing can produce stays
// unproducible and says so, because a stub that fires on a guess is worse
// than one that never fires — the first is wrong and the second is merely
// absent. What tells the two apart from outside is the roll-call in
// tests/run/activity_census, which names the activities no run ever reached.

#include "core_common/resident_activity.h"

#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/state_table_ops.h"
#include "core_common/work_seam.h"

namespace core {
namespace {

/// Detail indices, by the order of resident_activity_details.csv WITHIN
/// each activity. Named here so that a reader of the code sees the word
/// rather than the number; the table is still the roster.
constexpr std::uint8_t kTruantWalkedOff = 1;
constexpr std::uint8_t kWalkingToWork = 0;
constexpr std::uint8_t kWalkingHome = 1;
constexpr std::uint8_t kBlockedNoMaterial = 1;
constexpr std::uint8_t kBlockedNoRoom = 2;
constexpr std::uint8_t kBlockedWaiting = 3;
constexpr std::uint8_t kStudyingSchool = 0;
constexpr std::uint8_t kLphGarden = 0;
constexpr std::uint8_t kAtHomeAsleep = 0;
constexpr std::uint8_t kAtHomeAwake = 1;

/// Whether `hour` falls inside [from, to). Half-open so that an hour
/// belongs to exactly one band and the bands can be laid end to end.
bool Inside(float hour, float from, float to) {
  return hour >= from && hour < to;
}

}  // namespace

ResidentActivityState ActivityOfResident(const WorldState& world,
                                         std::uint32_t row,
                                         float age_years,
                                         const ActivityRules& rules) {
  ResidentActivityState answer;
  if (row >= world.residents.rows.size()) {
    return answer;
  }
  const ResidentRow& resident = world.residents.rows[row];
  const auto hour = static_cast<float>(HourFromTick(world.calendar.tick));
  const DayWindow window = SolarWindow(world.weather.daylight_hours);
  const float sleep_half = rules.sleep_hours * 0.5F;
  const bool asleep = hour < sleep_half || hour >= 24.0F - sleep_half;

  Vec2 home{};
  const bool has_home = HomePositionOf(world, resident.family, home);
  Vec2 target{};
  const bool assigned = resident.work.kind != WorkKind::kNone;
  const bool has_target = assigned && WorkPlaceOf(world, resident.work, target);
  const float* seam = assigned ? WorkSeamOf(world, resident.work) : nullptr;

  // "Nothing to work with" is the seam being GONE or standing at zero. The
  // labour hour says the same in its own words — "the job is done for
  // today; he stands about, unpaid" — and this is that sentence asked as a
  // question instead of written as a comment.
  const bool nothing_to_work_with = assigned && (seam == nullptr || *seam <= 0.0F);
  const float leaves = window.sunrise;
  const float starts = window.sunrise + rules.travel_hours;
  const float stops = window.sunset - rules.travel_hours;
  const float returns = window.sunset;
  const bool of_working_age =
      age_years >= rules.work_from_bio_years && age_years < rules.work_to_bio_years;
  const bool fit = resident.health >= rules.fit_health;

  // -- the fourteen predicates, each on its own ----------------------------
  bool holds[static_cast<std::size_t>(ResidentActivity::kResidentActivityCount)] = {};
  const auto set = [&holds](ResidentActivity activity) {
    holds[static_cast<std::size_t>(activity)] = true;
  };

  // STUB: the core has no illness. Health below the treatment line is the
  // nearest true thing it can say, and it cannot say WHERE he is treated —
  // none of the four places exists as a unit.
  if (resident.health < rules.treated_health) {
    set(ResidentActivity::kTreated);
  }
  // STUB: kAway. Nothing in the core sends anybody to the district, and the
  // order that would belongs to the quest layer.
  // OFF WORK WITHOUT LEAVE, and the world says it without a new field.
  //
  // A man who walks off from fatigue is settled on the spot: PayDay books
  // his hours and CLEARS HIS ASSIGNMENT (labor_system.cpp), and the day's
  // wipe only comes at midnight. So between the walk-off and the end of the
  // day he is a man with no order who has nonetheless been out — hours
  // already spent away — which nobody else in the village is.
  //
  // THAT ALONE WOULD ACCUSE THE INNOCENT. PayDay is also called when the
  // job vanishes from under a man: the crew finished the phase this very
  // hour and production moved the field on. He is unassigned with hours
  // away too, and none of it was his doing. What separates them is that the
  // walk-off has a cause the state still carries — he is at or under the
  // rest he broke off at, and the other man is not.
  //
  // "Overslept" and "went on a bender" have no source and are not guessed
  // at: a wrong cause is worse than an unnamed one, and truancy is an
  // accusation.
  //
  // NOTHING WAS ADDED TO THE STATE FOR THIS, which is the point. The event
  // kWalkOff is a fact of the step and dies with it; deriving from it would
  // hand back a different truant after a save. `hours_away_today` and
  // `rest` are both saved, so this answer survives a load — and the census
  // probe is what says so rather than my saying it.
  if (!assigned && resident.work.hours_away_today > 0.0F && resident.rest <= rules.walkoff_rest) {
    set(ResidentActivity::kTruant);
  }
  if (nothing_to_work_with && Inside(hour, starts, stops)) {
    set(ResidentActivity::kBlocked);
  }
  // IDLENESS IS CHARGED TO A WORKER IN WORKING HOURS AND TO NOBODY ELSE.
  // The child and the old man are not exempted by a state of their own —
  // there is simply nothing to charge them with, which is the same answer
  // arrived at without a second question in the list.
  if (!assigned && of_working_age && fit && Inside(hour, leaves, returns)) {
    set(ResidentActivity::kIdle);
  }
  if (assigned && !nothing_to_work_with && Inside(hour, starts, stops)) {
    set(ResidentActivity::kWorking);
  }
  if (assigned && (Inside(hour, leaves, starts) || Inside(hour, stops, returns))) {
    set(ResidentActivity::kWalking);
  }
  // ONE HOUR IN THE MIDDLE OF THE LIGHT WORKING DAY — boss's number of
  // 2026-09-05, not a convention invented here. The family meal is still a
  // quantity of the DAY (family_meal.cpp); this says WHEN, which the day's
  // quantity cannot.
  //
  // WHICH of the four places he eats in has no source: a canteen, a field
  // canteen, home and a bundle are four different units the model does not
  // know he has. The detail is left unnamed rather than guessed.
  if (Inside(hour, rules.meal_hour, rules.meal_hour + 1.0F)) {
    set(ResidentActivity::kEating);
  }
  if (age_years >= rules.school_from_bio_years && age_years < rules.school_to_bio_years &&
      Inside(hour, window.sunrise, window.sunset)) {
    set(ResidentActivity::kStudying);
  }
  // INVENTED SCHEDULE, and it is invented rather than missing. The yard's
  // hours are a share of the day (household_plot.h) and the question here
  // is a moment, so the two are bridged by a convention: THE PLOT IS
  // WORKED FROM THE END OF THE WORKING DAY UNTIL SLEEP. Nothing in the
  // design says that; it is written here so that the next reader does not
  // take it for a decision. When a real source arrives the schedule goes,
  // and it goes visibly.
  if (of_working_age && !asleep && hour >= returns) {
    set(ResidentActivity::kLph);
  }
  // STUB: kResting. Six details — fishing, foraging, bathing, culture,
  // sport, drinking — and no mechanic behind any of them.
  // The remainder, and it stands LAST so that it can swallow nothing that
  // is explained better.
  set(ResidentActivity::kAtHome);

  // -- the highest priority that holds -------------------------------------
  for (std::size_t index = 0; index < std::size(holds); ++index) {
    if (holds[index]) {
      answer.activity = static_cast<ResidentActivity>(index);
      break;
    }
  }

  // -- the detail and the place, which follow from the answer ---------------
  answer.detail = kNoActivityDetail;
  switch (answer.activity) {
    case ResidentActivity::kWalking:
      answer.detail = Inside(hour, leaves, starts) ? kWalkingToWork : kWalkingHome;
      answer.place.point = answer.detail == kWalkingToWork ? target : home;
      answer.place.off_map = 0;
      break;
    case ResidentActivity::kWorking:
      answer.place.point = target;
      if (resident.work.kind == WorkKind::kConstruction) {
        answer.place.unit = resident.work.unit;
      }
      break;
    case ResidentActivity::kBlocked:
      // Three of the four causes are knowable; only "no tool" is not, and
      // tools are not a thing the model has at all. A WRONG CAUSE IS WORSE
      // THAN AN UNNAMED ONE, because the cause is the part the player acts
      // on, so what cannot be told is left as "waiting his turn".
      answer.detail = kBlockedWaiting;
      // A man carrying a load off a field stops for exactly one reason, and
      // it is not his turn in a queue: there is nowhere to put what he is
      // carrying. And the world says so without anybody having to be asked
      // — production sizes the hauling demand by the free room of all the
      // stores (field_haul.cpp, ReceivableRoom), so A STANDING LOAD WITH NO
      // DEMAND AGAINST IT MEANS THE DOORS ARE SHUT. The run's own chairman
      // reads the same predicate for the same reason (fixture_policy.h).
      if (resident.work.kind == WorkKind::kHauling) {
        const std::uint32_t field_row = FindRow(world.fields, resident.work.field);
        if (field_row != kNoRow && world.fields.rows[field_row].reaped_grams > 0 &&
            world.fields.rows[field_row].haul_days_remaining <= 0.0F) {
          answer.detail = kBlockedNoRoom;
        }
      }
      if (resident.work.kind == WorkKind::kConstruction) {
        const std::uint32_t site = FindRow(world.units, resident.work.unit);
        if (site != kNoRow &&
            world.units.rows[site].construction.phase == ConstructionPhase::kDelivering) {
          answer.detail = kBlockedNoMaterial;
        }
        answer.place.unit = resident.work.unit;
      }
      if (has_target) {
        answer.place.point = target;
      }
      break;
    case ResidentActivity::kTruant:
      answer.detail = kTruantWalkedOff;
      break;
    case ResidentActivity::kStudying:
      answer.detail = kStudyingSchool;
      break;
    case ResidentActivity::kLph:
      answer.detail = kLphGarden;
      break;
    case ResidentActivity::kAtHome:
      answer.detail = asleep ? kAtHomeAsleep : kAtHomeAwake;
      break;
    case ResidentActivity::kTreated:
    case ResidentActivity::kAway:
    case ResidentActivity::kIdle:
    case ResidentActivity::kEating:
    case ResidentActivity::kResting:
    case ResidentActivity::kResidentActivityCount:
      break;
  }
  // A state with no place of its own still happens somewhere, and home is
  // where the model puts a man it is not otherwise tracking.
  if (answer.place.point.x == 0.0F && answer.place.point.y == 0.0F && has_home) {
    answer.place.point = home;
  }
  return answer;
}

}  // namespace core
