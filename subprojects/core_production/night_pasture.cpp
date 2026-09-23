// The night pasture (night_pasture.h).

#include "night_pasture.h"

#include <cstdint>

#include "core_common/away_in_district.h"
#include "core_common/calendar.h"
#include "core_common/emit_event.h"
#include "core_common/random.h"

namespace core {
namespace {

/// The random stream the camp's place is drawn from. Its own stream id, so
/// drawing it never shifts the draws of any other system of the world.
constexpr std::uint64_t kNightPastureStream = 0x4E49474854ULL;  // "NIGHT"

/// School is out: the months outside the school year, which is the design's
/// «каникулы» and not the pasture season.
///
/// THE TWO ARE DIFFERENT WINDOWS AND WERE NEVER THE SAME QUESTION. The
/// pasture season runs five months (May to September, farming.csv) and the
/// holidays three (June, July, August). Until 2026-09-17 the horses' summer
/// feed discount stood on the FIRST of the two and on nothing else — no
/// yard, no order, no children — which gave the village five months of a
/// three-month gain and gave it from day zero.
bool SchoolIsOut(const ProductionConfig& config, std::uint8_t month) {
  const std::uint8_t from = config.farming.school_year_end_month;
  const std::uint8_t to = config.farming.school_year_start_month;
  if (from == to) {
    return false;  // a year with no break in it has no holidays
  }
  // The school year wraps the new year (September..May), so its holidays do
  // not: they are the plain run between its end and its next beginning.
  return from < to ? (month >= from && month < to) : (month >= from || month < to);
}

/// Children of the senior school band — the ones the design gives the night
/// pasture to: old enough to be trusted with the team, too young to be sent
/// to work (`age_school_senior_from_years` .. `age_adult_from_years`).
std::uint32_t ChildrenOfTheBand(const ProductionConfig& config, const WorldState& world) {
  std::uint32_t count = 0;
  for (const ResidentRow& person : world.residents.rows) {
    const float age =
        BiologicalAgeYears(config.farming.life_speedup, person.birth_day, world.calendar.day);
    count += age >= config.farming.senior_school_from_years &&
                     age < config.farming.adult_from_years &&
                     !AwayInDistrict(person, world.calendar.tick)
                 ? 1U
                 : 0U;
  }
  return count;
}

/// A point on a floodplain meadow, drawn from the world's own generator.
///
/// THE CORE DRAWS IT AND NOT THE LAYER, and the design says why in as many
/// words: geometry is the layer's constant and the layer does not choose
/// within it, so a point the layer drew would part company with the save on
/// the first evening.
bool DrawCampPlace(WorldState& current, Vec2& place) {
  std::uint32_t meadows = 0;
  for (const FieldRow& field : current.fields.rows) {
    meadows += field.kind == LandKind::kFloodplainMeadow ? 1U : 0U;
  }
  if (meadows == 0) {
    return false;
  }
  RngState rng = SeedRngState(current.rng.state, kNightPastureStream);
  const std::uint32_t chosen = NextRandomBelow(rng, meadows);
  std::uint32_t seen = 0;
  for (const FieldRow& field : current.fields.rows) {
    if (field.kind != LandKind::kFloodplainMeadow) {
      continue;
    }
    if (seen == chosen) {
      place = field.center;
      return true;
    }
    ++seen;
  }
  return false;
}

/// THE THREE CONDITIONS, IN ONE PLACE. They are asked at two different
/// moments — when the order is given, and on every evening after — and
/// writing them out twice would be one rule with two houses. The damage run
/// that found this predicted two reddened checks and got one: the gate had
/// been switched off in the order and was still standing in the night, which
/// is exactly what a second house looks like from outside.
bool ConditionsHold(const ProductionConfig& config, const WorldState& world) {
  if (world.chairman.horses_stabled == 0) {
    return false;  // «пока лошади стоят по личным дворам, уводить некого и некому»
  }
  if (!SchoolIsOut(config, static_cast<std::uint8_t>(world.calendar.date.month))) {
    return false;  // school is in; the children are at their desks
  }
  if (ChildrenOfTheBand(config, world) == 0) {
    return false;
  }
  return true;
}

}  // namespace

bool TeamOutTonight(const ProductionConfig& config, const WorldState& world) {
  return world.chairman.night_pasture_ordered != 0 && ConditionsHold(config, world);
}

OrderRefusal OrderNightPasture(const ProductionConfig& config, WorldState& current) {
  // THE THREE CONDITIONS, and each of them refuses the same way on purpose:
  // the repair for every one is the calendar or a building, never a different
  // order, so none of them earns a refusal word of its own.
  if (!ConditionsHold(config, current)) {
    return OrderRefusal::kRuleForbids;
  }
  Vec2 place;
  if (!DrawCampPlace(current, place)) {
    return OrderRefusal::kRuleForbids;  // no floodplain meadow to keep them on
  }
  current.chairman.night_pasture_ordered = 1;
  current.chairman.night_pasture_place = place;
  return OrderRefusal::kNone;
}

void RunNightPasture(const ProductionConfig& config, WorldState& current) {
  if (current.chairman.night_pasture_begun != 0 || !TeamOutTonight(config, current)) {
    return;
  }
  std::uint32_t heads = 0;
  for (const HerdRow& herd : current.herds.rows) {
    if (herd.household_owned == 0 && herd.kind.value == config.horse_kind.value) {
      heads += herd.adult_count;
    }
  }
  current.chairman.night_pasture_begun = 1;
  SimEvent& event = EmitEvent(current, EventKind::kNightPastureBegan, EventSeverity::kNotable);
  event.amount = static_cast<std::int64_t>(heads);
}

}  // namespace core
