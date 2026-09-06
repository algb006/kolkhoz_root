// Wear, collapse terms and the stink zone (unit_decay.h) — everything that
// happens to a unit with nobody ordering it.

#include "unit_decay.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/geometry.h"
#include "core_common/state_table_ops.h"

namespace core {
bool TypeIsOldHouse(const ConstructionConfig& config, UnitTypeId type) {
  return config.old_house_type.value != kInvalidDefIdValue &&
         type.value == config.old_house_type.value;
}

/// @brief How much faster than its class's term this unit wears: the
///        type's own pace times the step's.
///
/// TWO FACTS, TWO COLUMNS, ONE PRODUCT. The type says what the nature of
/// the unit does to it — a byre is damp and full of ammonia, a mill
/// shakes; the step says what it stands on — a timber frame on wooden
/// stools lives shorter than the same frame on stone. Both answer "how
/// much faster", and a unit that is both damp and badly founded is worse
/// than one that is either, so they multiply (boss, 2026-09-05).
///
/// THE OLD HOUSE IS NOT AN EXCEPTION, and it used to be. Its own pace is
/// empty — a peasant hut has no nature that ages it — but its stools are
/// the worst in the village, and the step now says so. Its collapse term
/// answers a different question (an event of the story, not a term of the
/// wear), which is exactly why applying the step's pace to it is not the
/// same fact counted twice.
float WearPace(const BuildType& type, std::uint8_t level) {
  const std::size_t index = static_cast<std::size_t>(level) - 1;
  const float step = index < type.levels.size() ? type.levels[index].wear_factor : 1.0F;
  return type.wear_factor * step;
}

/// @brief The amortization term of the level a unit stands at, in game years.
float WearYears(const BuildType& type, std::uint8_t level, bool in_use) {
  const std::size_t index = static_cast<std::size_t>(level) - 1;
  if (index >= type.levels.size()) {
    return 0.0F;
  }
  const BuildLevel& step = type.levels[index];
  return in_use ? step.wear_years_in_use : step.wear_years_idle;
}

/// Is anybody living or working in this unit today? A household in it, or
/// a resident assigned to a herd that stands here — the two facts the
/// state carries. A BUILDING CREW DOES NOT COUNT: a site is not working,
/// it is being worked on, and it does not wear because level 0 is skipped
/// above anyway. When unit cycles arrive, "in use" becomes their flag.
bool InUse(const WorldState& world, std::uint32_t row) {
  const UnitRow& unit = world.units.rows[row];
  if (unit.household.value != kInvalidEntityIdValue) {
    return true;
  }
  // A unit with something in it is not empty either (boss, 2026-09-03): a
  // granary holding grain is walked round, propped and patched every week,
  // and calling it abandoned is the untruth behind which the player would
  // watch a barn full of bread decay.
  for (const Grams amount : unit.stock) {
    if (amount > 0) {
      return true;
    }
  }
  const UnitId id = world.units.row_ids[row];
  for (const ResidentRow& resident : world.residents.rows) {
    if (resident.work.kind != WorkKind::kHerdCare) {
      continue;
    }
    const std::uint32_t herd_row = FindRow(world.herds, resident.work.herd);
    if (herd_row != kNoRow && world.herds.rows[herd_row].unit.value == id.value) {
      return true;
    }
  }
  return false;
}

/// @brief Hours of road, one way, from the NEAREST dwelling to `place`;
///        0 when the settlement has no house at all.
///
/// The nearest and not the middle: a homestead two kilometres out is
/// legitimate and reachable by its own household, and measuring against
/// the far side of the village would forbid people to spread out.
float NearestDwellingHours(const ConstructionConfig& config,
                           const WorldState& completed,
                           const Vec2& place) {
  float best = -1.0F;
  for (const UnitRow& unit : completed.units.rows) {
    if (unit.level == 0 || unit.type.value >= config.definitions.units.is_housing.size() ||
        config.definitions.units.is_housing[unit.type.value] == 0) {
      continue;
    }
    const float hours = TravelHoursBetween(unit.position, place, config.walk_hours_per_km);
    if (best < 0.0F || hours < best) {
      best = hours;
    }
  }
  return best < 0.0F ? 0.0F : best;
}

/// @brief How far this source reaches when it has fully built up, at the
/// level it stands at now.
///
/// THE LADDER NARROWS IT, and the floor keeps the core. A unit at level 1
/// reaches the full radius of its strength; every rung after that
/// multiplies by stink_step_factor, and nothing goes below
/// stink_core_fraction of the widest — right up against the byre it
/// smells whatever you do.
///
/// WHERE THIS CAN BE SEEN AT ALL, counted rather than assumed, and the
/// count is smaller than it first looks. Of the thirteen sources the
/// tables carry, ELEVEN have a single rung in unit_levels.csv; only the
/// cattle yard (three) and the silage trench (two) have a step to climb.
///
/// AND ON THE SHIPPED START THE ANSWER IS NONE, which the first version of
/// this comment got wrong by saying "two". Genesis places every unit at
/// level 1 — start_layout.csv has no level column and UnitRow's default is
/// 1 — and the loop below runs zero times at level 1. So a reader who took
/// the old sentence at its word would measure the designed village, find
/// nothing, and conclude the rule was broken. It bites only after the
/// player pays for an upgrade, which is exactly what it is for.
float FullRadiusFor(const ConstructionConfig& config, const BuildType& type, std::uint8_t level) {
  const float widest = config.stink_radius_m[static_cast<std::size_t>(type.stink)];
  float radius = widest;
  for (std::uint8_t rung = 1; rung < level; ++rung) {
    radius *= config.stink_step_factor;
  }
  const float floor_radius = widest * config.stink_core_fraction;
  return radius < floor_radius ? floor_radius : radius;
}

// Not part of the header: the two seam answers below share it, and nothing
// outside this file has any business walking the sources itself.
namespace {
/// THE STINK FIELD, walked over the units (construction_system.h).
///
/// IT SITS HERE AND NOT ABOVE WearDeadline, and the reason is a defect
/// this delta committed and the cycle caught: dropped in there, it stood
/// between the alarm paragraphs and the function they belong to, so the
/// prose about kSiteWithoutMaterials came to document the stink walk and
/// WearDeadline was left bare. THIRD TIME IN TWO CYCLES that inserting a
/// declaration stole the comment above it — kNeverMownDay took FieldRow's
/// @brief the same morning. A doc comment documents whatever FOLLOWS it,
/// and an insertion point is therefore never neutral: the question to ask
/// before adding a declaration is not "does it belong in this class" but
/// "whose comment am I standing under".
///
/// Distances are compared SQUARED: the answer is "is it inside", and a
/// square root on the way to a comparison buys nothing but a chance for
/// two callers to round differently.
/// The two seam answers share one walk and differ in ONE line — which
/// radius a source is measured by. Written as one body with a flag rather
/// than two, because two bodies would be two chances for the edge cases
/// (a site at level 0, a working-only source, the worse-of-two rule) to
/// drift apart, and the drift would show as the cough disagreeing with
/// the placement preview about the same yard.
StinkStrength StinkWalk(const ConstructionConfig& config,
                        const WorldState& completed,
                        Vec2 point,
                        bool full) {
  StinkStrength worst = StinkStrength::kNone;
  for (const UnitRow& unit : completed.units.rows) {
    // Level 0 is a site or a unit coming down, and it holds nothing yet:
    // the one rule that replaces a flag in every table (unit_state.h).
    if (unit.level == 0 || unit.type.value >= config.types.size()) {
      continue;
    }
    const BuildType& type = config.types[unit.type.value];
    if (type.stink == StinkStrength::kNone || type.stink <= worst) {
      continue;  // nothing to add, or nothing WORSE to add
    }
    // The half that has nothing to read: a source that smells only while
    // it works cannot be asked, because no unit in this core works yet.
    // STUB — see the contract in construction_system.h.
    if (type.stink_when == StinkWhen::kWorking) {
      continue;
    }
    // THE PLAN IS JUDGED ON THE WIDEST THE SOURCE EVER IS, which is its
    // FIRST rung: the ladder only narrows, so level 1 is the worst case
    // and the design asks the preview to show exactly that — "the full
    // radius is drawn, with all the modules still to come", so that a spot
    // chosen today does not turn out to stink tomorrow. Today's zone,
    // below, is the one that knows what level the unit actually stands at.
    const float radius =
        full ? config.stink_radius_m[static_cast<std::size_t>(type.stink)] : unit.stink_radius_m;
    if (!(radius > 0.0F)) {
      continue;  // today's zone has not started, or has gone out
    }
    const float dx = point.x - unit.position.x;
    const float dy = point.y - unit.position.y;
    if ((dx * dx) + (dy * dy) <= radius * radius) {
      worst = WorseStink(worst, type.stink);
    }
  }
  return worst;
}
}  // namespace

Deadline WearDeadline(const ConstructionConfig& config, const WorldState& completed, UnitId unit) {
  if (!config.wear_column_present) {
    // The tables carry no has_wear column at all, so nothing here knows
    // whether anything wears. "Nothing wears" and "nobody said" arrive at
    // this code as the same zero, and they are not the same answer.
    return NoDeadline(DeadlineKind::kNoData);
  }
  const std::uint32_t row = FindRow(completed.units, unit);
  if (row == kNoRow) {
    return NoDeadline(DeadlineKind::kNotApplicable);  // no such unit: no question
  }
  const UnitRow& built = completed.units.rows[row];
  if (built.type.value >= config.types.size()) {
    return NoDeadline(DeadlineKind::kNotApplicable);  // no type, no term
  }
  if (built.level == 0) {
    // A SITE IS "NEVER", NOT "NOT APPLICABLE". Pegs and string do not wear
    // — but this row keeps its id and starts wearing the day it is built,
    // and kNotApplicable tells the reader to drop the question for good
    // (deadline.h). The rate is what changes here, which is kNever's whole
    // meaning.
    return NoDeadline(DeadlineKind::kNever);
  }
  const BuildType& type = config.types[built.type.value];
  if (type.has_wear == 0) {
    // A stack, a heap, a trench. Not "no deadline yet" — no wear, ever.
    return NoDeadline(DeadlineKind::kNotApplicable);
  }
  if (built.wear >= kWearScale) {
    // Asked BEFORE the pause, because a unit already at the end is at the
    // end whether it is running or not — "never" would be a promise about
    // a limit it has already reached.
    return DeadlineInDays(0);
  }
  if (built.paused != 0) {
    // Stopped units do not wear (unit rules §15). An answer that changes
    // the moment somebody starts it, which is exactly what kNever means.
    return NoDeadline(DeadlineKind::kNever);
  }
  const bool is_old_house = TypeIsOldHouse(config, built.type);
  const float years = is_old_house ? config.old_house_collapse_years
                                   : WearYears(type, built.level, InUse(completed, row));
  if (!(years > 0.0F)) {
    // The ladder names no term for this level. Documented as "does not
    // wear" (construction_config.h), so the question does not arise —
    // and it is told apart from a missing column by the check above.
    return NoDeadline(DeadlineKind::kNotApplicable);
  }
  // The same daily share AgeUnits adds, read forward instead of applied.
  const float pace = WearPace(type, built.level);
  const float per_day = kWearScale * pace / (years * static_cast<float>(kDaysPerYear));
  if (!(per_day > 0.0F)) {
    return NoDeadline(DeadlineKind::kNever);  // a pace of nothing wears nothing
  }
  // COUNTED THE WAY THE WORLD COUNTS IT, day by day, and not by dividing.
  //
  // AgeUnits ADDS the daily share; dividing the remainder by it answers a
  // different question and answers it wrong twice over. It truncates,
  // so the forecast named a day one earlier than the world reaches — and
  // on the last-but-one day it returned 0, which this contract defines as
  // "the limit is reached now". And the accumulated float sum does not
  // land where the division says anyway: a twenty-year term takes 961
  // additions, not 960.
  //
  // Adding the same shares in the same order agrees by CONSTRUCTION rather
  // than by argument, and it also removes a float-to-int cast that a
  // denormal pace could have driven out of range.
  float wear = built.wear;
  std::int32_t days = 0;
  while (wear < kWearScale && days < kWearForecastHorizonDays) {
    wear += per_day;
    ++days;
  }
  // Saturation, said out loud: a term so long that a century of game days
  // does not reach the end of the scale reports the horizon, and a reader
  // takes it as "at least this". The loop needs a bound anyway — a
  // denormal share would otherwise run for ever.
  return DeadlineInDays(days);
}

StinkStrength StinkFullAt(const ConstructionConfig& config,
                          const WorldState& completed,
                          Vec2 point) {
  return StinkWalk(config, completed, point, true);
}

StinkStrength StinkNowAt(const ConstructionConfig& config,
                         const WorldState& completed,
                         Vec2 point) {
  return StinkWalk(config, completed, point, false);
}

/// FOURTH THEFT OF A COMMENT IN THIS FILE, and this one was committed
/// directly beneath the paragraph that warns about it (see StinkWalk, a
/// hundred and seventy lines up). FullRadiusFor was inserted between this
/// prose and the function it describes, so Doxygen bound the whole block
/// forward and MoveStinkZones shipped with no @brief at all.
///
/// The rule was written down, read twice today, and did not fire — because
/// it asks to be REMEMBERED at the moment of insertion. So it is now
/// checked by a script instead (claude/tools/orphan_doc.py): a doc block
/// followed by another doc block is a doc block that has lost its subject,
/// and that shape is mechanical.
/// THE ZONE OF A DAY (water design §4). Every source's reach moves one
/// step towards where it belongs: out to the full radius of its strength
/// while it emits, in towards nothing while it does not.
///
/// A SPEED AND NOT A TERM, and the difference is the whole rule. Give the
/// decay a length — "a zone goes out in three days" — and you must then
/// explain why the forge's three days are not the tannery's; give it a
/// speed and there is nothing left to explain, because the big zone has
/// further to shrink. The design tells both stories and asks for one
/// rule, and this is the rule that tells both.
///
/// WHAT DOES NOT MOVE YET: a source that smells only WHILE IT WORKS never
/// grows, because this core has no work at a production unit to read
/// (construction_system.h). Its zone stays at nothing, which is the same
/// answer the two seam queries give for it, so the STUB is consistent in
/// both places rather than only in one.
void MoveStinkZones(const ConstructionConfig& config, WorldState& current) {
  for (UnitRow& unit : current.units.rows) {
    if (unit.type.value >= config.types.size()) {
      continue;
    }
    const BuildType& type = config.types[unit.type.value];
    const bool emitting = unit.level > 0 && type.stink != StinkStrength::kNone &&
                          type.stink_when == StinkWhen::kAlways;
    const float full = emitting ? FullRadiusFor(config, type, unit.level) : 0.0F;
    if (unit.stink_radius_m < full) {
      unit.stink_radius_m = std::min(full, unit.stink_radius_m + config.stink_growth_m_per_day);
    } else if (unit.stink_radius_m > full) {
      unit.stink_radius_m = std::max(full, unit.stink_radius_m - config.stink_decay_m_per_day);
    }
  }
}
}  // namespace core
