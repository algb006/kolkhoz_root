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
constexpr std::uint8_t kWalkingToWork = 0;
constexpr std::uint8_t kWalkingHome = 1;
constexpr std::uint8_t kBlockedNoMaterial = 1;
constexpr std::uint8_t kBlockedWaiting = 3;
constexpr std::uint8_t kStudyingSchool = 0;
constexpr std::uint8_t kLphGarden = 0;
constexpr std::uint8_t kAtHomeAsleep = 0;
constexpr std::uint8_t kAtHomeAwake = 1;
constexpr std::uint8_t kNotWorkerAge = 0;
constexpr std::uint8_t kNotWorkerHealth = 1;

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
  // STUB: kTruant. A man who walks off from fatigue raises kWalkOff, but
  // that is an event of the step and not a state of the world — deriving a
  // state from it would give a different truant after a save and a load,
  // which is exactly the trap a derived value has to avoid.
  if (nothing_to_work_with && Inside(hour, starts, stops)) {
    set(ResidentActivity::kBlocked);
  }
  if (!assigned && of_working_age && fit) {
    set(ResidentActivity::kIdle);
  }
  if (assigned && !nothing_to_work_with && Inside(hour, starts, stops)) {
    set(ResidentActivity::kWorking);
  }
  if (assigned && (Inside(hour, leaves, starts) || Inside(hour, stops, returns))) {
    set(ResidentActivity::kWalking);
  }
  // STUB: kEating. The family meal is a quantity of the DAY; the model has
  // no hour of dinner, so there is nothing to point at when asked "now".
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
  set(ResidentActivity::kAtHome);  // the floor: everybody is somewhere
  if (!of_working_age && age_years >= rules.work_from_bio_years) {
    set(ResidentActivity::kNotWorker);
  }
  if (age_years >= rules.work_from_bio_years && !fit) {
    set(ResidentActivity::kNotWorker);
  }
  if (age_years < rules.work_from_bio_years) {
    set(ResidentActivity::kTooYoung);
  }

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
      // Only two of the four causes are knowable today: a site still
      // waiting for its recipe says so in its phase, and everything else
      // is "waiting his turn". No tool and nowhere to put it have no source
      // — a wrong cause is worse than an unnamed one, because the cause is
      // the part the player acts on.
      answer.detail = kBlockedWaiting;
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
    case ResidentActivity::kStudying:
      answer.detail = kStudyingSchool;
      break;
    case ResidentActivity::kLph:
      answer.detail = kLphGarden;
      break;
    case ResidentActivity::kAtHome:
      answer.detail = asleep ? kAtHomeAsleep : kAtHomeAwake;
      break;
    case ResidentActivity::kNotWorker:
      answer.detail = fit ? kNotWorkerAge : kNotWorkerHealth;
      break;
    case ResidentActivity::kTreated:
    case ResidentActivity::kAway:
    case ResidentActivity::kTruant:
    case ResidentActivity::kIdle:
    case ResidentActivity::kEating:
    case ResidentActivity::kResting:
    case ResidentActivity::kTooYoung:
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
