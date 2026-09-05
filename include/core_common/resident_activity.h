/// @file
/// @brief What a resident is doing right now: one state, never two.
/// @threading PARALLEL_READONLY
/// Plain data with no state of its own, derived from a completed world
/// between steps and read from anywhere afterwards. Nothing in the
/// simulation reads it back, which is what keeps it from becoming a second
/// truth beside the one it was read from.
///
/// ONE OF THESE AND NEVER TWO, and that exclusivity is the whole value of
/// the column: a set of flags would let a man be idle and working at once,
/// and the question "what is he doing" would have no answer. Two states can
/// describe the same instant honestly — a sick man with no work order is
/// both ill and unassigned — and the tie is broken by a PRIORITY that is a
/// total order, so that the answer never depends on the order the code
/// happens to test in. The order is the declaration order below, and
/// resident_activities.csv carries the same numbers; the config refuses a
/// table set where the two disagree.
///
/// TWO STATES WERE TAKEN OUT OF THE ROSTER ON 2026-09-05, and the reason is
/// worth keeping. "Too young" and "not a worker" were answering the question
/// "why does the idleness signal not apply to him" while sitting in a list
/// that answers "what is he doing" — one word for two questions, which is
/// the defect this project spent the day finding in its own data. The
/// condition moved to where it belongs: kIdle holds only for a worker in
/// working hours. A two-year-old is then at home and asleep, which is the
/// truth, and no idleness is charged to the player because THERE IS NOTHING
/// TO CHARGE, not because the child has a state of his own.
///
/// THREE OF THEM ARE A REPROACH TO THE PLAYER, not a property of the
/// person: kIdle, kBlocked and kTruant. They are counted in man-days
/// because that is the unit the reproach is measured in — a village of
/// eight hundred standing about for one day is not the same trouble as one
/// man standing about for eight hundred.
///
/// kIdle AND kBlocked MUST NOT BE MERGED. "No order" and "an order and
/// nothing to work with" are different failures with different cures, and
/// the cause is the only part the player can fix. One word for both would
/// hide exactly that part.

#ifndef CORE_COMMON_RESIDENT_ACTIVITY_H_
#define CORE_COMMON_RESIDENT_ACTIVITY_H_

#include <cstdint>

#include "core_common/day_window.h"
#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/world_state.h"

namespace core {

/// @brief The fourteen states, in PRIORITY ORDER: the first that applies
/// wins, and the table's `priority` column says the same thing in numbers.
enum class ResidentActivity : std::uint8_t {
  /// Being treated. STUB: the core has no illness, only `health` below a
  /// threshold, and none of the four places exist as units yet.
  kTreated = 0,

  /// Away in the district: a messenger, the market, the raikom, a fair.
  /// STUB: nothing in the core sends anybody, and the order that would is
  /// the quest layer's, not this module's.
  kAway,

  /// Off work without leave. Half of it is real — a man who walks off from
  /// fatigue already raises kWalkOff — and oversleeping and a bender have
  /// no source.
  kTruant,

  /// An order stands and there is nothing to work with: no tool, no
  /// material, nowhere to put the output, waiting his turn.
  kBlocked,

  /// Of working age, fit, and nobody gave him anything to do.
  kIdle,

  /// Working his assignment, this hour, inside the daylight window.
  kWorking,

  /// On the road: the travel margin at either end of the working day.
  kWalking,

  /// Eating. STUB: the family meal is a DAILY quantity and the model has no
  /// hour of dinner, so there is nothing to point at when asked "now".
  kEating,

  /// At school, on courses, or reading on his own.
  kStudying,

  /// Working the household plot. STUB for the same reason as kEating: the
  /// yard's hours are a share of the day, not a place in it.
  kLph,

  /// Resting: fishing, foraging, bathing, culture, sport, drinking. STUB —
  /// none of the six has a mechanic behind it.
  kResting,

  /// At home, asleep or awake. LAST ON PURPOSE, and that is a rule rather
  /// than a placement: THE REMAINDER MUST NOT SWALLOW WHAT IS EXPLAINED
  /// BETTER. The less a state explains, the lower it stands — and this one
  /// explains least, because anybody not doing something else is at home.
  ///
  /// It stood twelfth of fourteen until 2026-09-05 and made the two below
  /// it unreachable; the census found that on its first run. Standing last
  /// it can make nothing unreachable ever again.
  kAtHome,

  /// NOT A VALUE, and never written to a save or read from one: the codecs
  /// range-check 0..kResidentActivityCount-1 and this is what they check
  /// against. Values are appended BEFORE it.
  kResidentActivityCount,
};

/// @brief Where a resident is, or is headed, when the activity carries a
/// place (`has_target` in the table).
///
/// THE POINT IS THE ANSWER THAT ALWAYS EXISTS and the unit is the one that
/// sometimes does: a man crossing a field stands at no unit at all, and a
/// quest that wants to meet him — or draw a line to where he is going —
/// needs a coordinate either way. Which kind of thing the id names follows
/// from the activity, so no second field says it.
struct ActivityPlace {
  /// Metres from the map's south-west corner, as in start_layout.csv.
  /// Meaningless when `off_map` is set.
  Vec2 point;

  /// The unit he is at or headed for; invalid when he is at none.
  UnitId unit;

  /// 1: he is in the district — market, hospital, the raikom — and HAS NO
  /// COORDINATE ON THIS MAP. An empty point must not be read as "somewhere
  /// unknown": the truth is the opposite, he is known exactly and he is not
  /// here, and a quest has to tell the two apart before drawing a line.
  std::uint8_t off_map = 0;
};

/// @brief One resident's answer to "what is he doing".
struct ResidentActivityState {
  ResidentActivity activity = ResidentActivity::kIdle;

  /// Index into this ACTIVITY's own list of details
  /// (resident_activity_details.csv, in table order); kNoActivityDetail
  /// when the activity has none or the core cannot yet say which.
  ///
  /// NOT ONE SHARED ENUM, because it is a different question per activity:
  /// "where to" for kWalking, "how" for kResting, "why" for kBlocked. A
  /// single roster of forty would have to be read against the activity
  /// anyway, and would invite a detail of one activity to be assigned to
  /// another.
  std::uint8_t detail = 0xFF;

  ActivityPlace place;
};

/// @brief "This activity has no detail, or none the core can name yet."
inline constexpr std::uint8_t kNoActivityDetail = 0xFF;

/// @brief The handful of numbers the answer needs that are not in the state.
///
/// A PARAMETER STRUCT AND NOT A CONFIG OF ITS OWN, the same shape as
/// PlotRules in plot.h and for the same reason: these thresholds already
/// have owners — the working age is the labour model's, the sickness line is
/// core_residents' — and copying them into a home of their own is how one
/// fact comes to have two values that drift. The caller fills this from the
/// configs that own the numbers, and this module holds none.
struct ActivityRules {
  /// Biological years below which a person is kTooYoung: not a worker, and
  /// not the player's failure either.
  float work_from_bio_years = 16.0F;

  /// Biological years at or above which he is kNotWorker by age.
  float work_to_bio_years = 70.0F;

  /// Health below this makes him kNotWorker by health, and below
  /// `treated_health` he is kTreated.
  float fit_health = 20.0F;

  float treated_health = 10.0F;

  /// The school band, from the household-plot table's own figures.
  float school_from_bio_years = 7.0F;

  float school_to_bio_years = 17.0F;

  /// Hours of the game day given to sleep, for telling kAtHome asleep from
  /// kAtHome awake. Not a mechanic: the model has no sleep, and this is the
  /// convention that lets the detail be answered at all.
  float sleep_hours = 8.0F;

  /// How many biological years a game year is worth (life.csv
  /// `life_speedup`, 4 as shipped).
  ///
  /// IT IS HERE SO THAT NO CALLER CAN GET THE AGE WRONG, and one already
  /// did. This function used to take the age as an argument, and the census
  /// handed it CALENDAR years against thresholds written in BIOLOGICAL
  /// ones — a village of adults read as a village of children, and every
  /// idleness number it published was twenty-five times too small (host,
  /// 2026-09-05). The excuse was in the comment above the arithmetic: "the
  /// exact speed-up belongs to the configs that own it".
  ///
  /// BiologicalAgeYears in calendar.h had already gathered five copies of
  /// that formula; the census was the sixth, and it was written the day
  /// after. So the parameter is gone: the answer is computed here, from the
  /// one home, and the only thing a caller can pass wrong is a number that
  /// has a name and a range.
  float life_speedup = 4.0F;

  /// The hour dinner is eaten: ONE hour in the middle of the daylight
  /// working day (boss's number, 2026-09-05, not a convention of this
  /// module's — the field canteen is bought precisely so that this hour is
  /// not spent walking home). Midday by default, which is the middle of any
  /// solar window this model builds.
  float meal_hour = 12.0F;

  /// Rest at or below which a man ends his own working day and goes home
  /// (labour config `rest_walkoff_threshold`, decision 107). What tells a
  /// truant from a man whose job vanished under him: both are unassigned
  /// with hours already spent away, and only one of them chose it.
  float walkoff_rest = 10.0F;

  /// Game hours of one-way travel per kilometre, on foot and behind a
  /// harness (labour config, `transport.csv` through HoursPerKm). The
  /// ANSWER is computed here from these and the two places; it used to be
  /// passed in as one ready number, and every caller passed the same
  /// half-hour for the whole village.
  ///
  /// THAT WAS THE LAST LIE LEFT IN THIS FUNCTION, and it is the same shape
  /// as the age was: a value the caller must derive by somebody else's
  /// formula, quietly replaced by a plausible constant. A man four hours
  /// from his field was counted as working, because the window was narrowed
  /// by thirty minutes instead of by eight hours — and "the crew is working
  /// and the site does not move" is exactly the picture that produces
  /// (host's measurement, 2026-09-05, quoting my number back at me).
  float walk_hours_per_km = 2.4F;

  float harness_hours_per_km = 1.0F;
};

/// @brief What one resident is doing at the hour the world stands at.
/// @param world     The COMPLETED state; nothing here writes to it.
/// @param row       Row of `world.residents`.
/// @param rules     The thresholds their owners hold (see ActivityRules).
/// @return The state, its detail where the core can name one, and its place
///         where the activity carries one.
/// @note Every predicate is evaluated and the answer is the one of HIGHEST
///       PRIORITY among those that hold — declaration order above, which
///       resident_activities.csv repeats in numbers. Testing them in
///       sequence and returning early would make the answer depend on the
///       order somebody happened to write the tests in, which is the one
///       thing the priority column exists to prevent.
ResidentActivityState ActivityOfResident(const WorldState& world,
                                         std::uint32_t row,
                                         const ActivityRules& rules);

}  // namespace core

#endif  // CORE_COMMON_RESIDENT_ACTIVITY_H_
