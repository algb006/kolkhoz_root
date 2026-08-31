// The derived projections of the completed state (signals.h).

#include "signals.h"

#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/day_window.h"
#include "core_common/geometry.h"
#include "core_common/labor_state.h"
#include "core_common/state_table_ops.h"

namespace core {
namespace {

/// Counts fit a household and a field crew many times over, but the state is
/// not the boundary's to trust blindly: a count is saturated rather than
/// wrapped, because a wrapped 65536 would read as an empty house.
constexpr std::uint32_t kCountCeiling = 0xFFFFU;

std::uint16_t Saturate16(std::uint32_t count) {
  return static_cast<std::uint16_t>(count < kCountCeiling ? count : kCountCeiling);
}

std::uint8_t Saturate8(std::uint32_t count) {
  constexpr std::uint32_t kByteCeiling = 0xFFU;
  return static_cast<std::uint8_t>(count < kByteCeiling ? count : kByteCeiling);
}

/// @brief Biological age in years: the clock of a person runs life_speedup
/// times faster than the calendar (demography design §2). Negative for a
/// resident born after `day`, which a well-formed world never holds.
float BiologicalAgeYears(float life_speedup, std::int32_t birth_day, SimDay day) {
  const float game_years = static_cast<float>(static_cast<std::int32_t>(day) - birth_day) /
                           static_cast<float>(kDaysPerYear);
  return game_years * life_speedup;
}

/// @brief True for the four kinds that are worked on a field; kHerdCare is
/// the barn's and kNone is nobody's.
bool IsFieldWork(WorkKind kind) {
  return kind != WorkKind::kNone && kind != WorkKind::kHerdCare;
}

/// @brief Does the hour of this tick overlap the daylight window at all?
/// The same test the labor sub-step makes of a working hour, asked as a
/// yes/no: a tick covers [hour, hour + 1).
bool InsideDaylight(std::uint32_t hour, const DayWindow& window) {
  const auto tick_start = static_cast<float>(hour);
  return tick_start + 1.0F > window.sunrise && tick_start < window.sunset;
}

/// @brief Position of a unit, or the origin when it is gone.
Vec2 UnitPosition(const WorldState& world, UnitId unit) {
  const std::uint32_t row = FindRow(world.units, unit);
  return row == kNoRow ? Vec2{} : world.units.rows[row].position;
}

}  // namespace

UnitSignals DeriveUnitSignals(const BoundaryConfig& config, const WorldState& world, UnitId unit) {
  UnitSignals signals;
  const std::uint32_t unit_row = FindRow(world.units, unit);
  if (unit_row == kNoRow) {
    return signals;  // no such unit: the invalid id and neutral fields
  }
  signals.unit = unit;

  // Who lives here: the members of the household this house holds. A unit
  // nobody lives in keeps both counts at zero, which is the whole of the
  // rule "one family, one house" seen from this side.
  const FamilyId household = world.units.rows[unit_row].household;
  std::uint32_t living = 0;
  std::uint32_t infants = 0;
  if (household.value != kInvalidEntityIdValue) {
    for (const ResidentRow& resident : world.residents.rows) {
      if (resident.family.value != household.value) {
        continue;
      }
      ++living;
      const float age =
          BiologicalAgeYears(config.life_speedup, resident.birth_day, world.calendar.day);
      if (age < config.infant_age_bio_years) {
        ++infants;
      }
    }
  }
  signals.residents_living = Saturate16(living);
  signals.infants = Saturate8(infants);

  // Who works here. A unit is not a field: the only work that happens AT a
  // unit today is the barn's, and a resident reaches it through the herd
  // that stands here (labor_state.h: kHerdCare names a herd, never a unit).
  std::uint32_t working = 0;
  for (const ResidentRow& resident : world.residents.rows) {
    if (resident.work.kind != WorkKind::kHerdCare) {
      continue;
    }
    const std::uint32_t herd_row = FindRow(world.herds, resident.work.herd);
    if (herd_row != kNoRow && world.herds.rows[herd_row].unit.value == unit.value) {
      ++working;
    }
  }
  signals.residents_working = Saturate16(working);

  // The outdoor temperature stands in for the indoor one until heating
  // exists: a cold house and a warm one are the same house to the core
  // today, and saying so with the real number keeps the presentation's
  // arithmetic honest (manual/70-boundary.md §10).
  signals.indoor_temperature_celsius = world.weather.air_temperature_celsius;

  // wear, paused, prank_marks: STUB at their neutral values until A5, the
  // kPauseUnit consumer in core_production and project phase 3 fill them.
  return signals;
}

FieldSignals DeriveFieldSignals(const WorldState& world, FieldId field) {
  FieldSignals signals;
  if (FindRow(world.fields, field) == kNoRow) {
    return signals;
  }
  signals.field = field;
  std::uint32_t working = 0;
  for (const ResidentRow& resident : world.residents.rows) {
    if (IsFieldWork(resident.work.kind) && resident.work.field.value == field.value) {
      ++working;
    }
  }
  signals.residents_working = Saturate16(working);
  return signals;
}

ResidentWhereabouts DeriveWhereabouts(const WorldState& world, ResidentId resident) {
  ResidentWhereabouts where;
  const std::uint32_t row = FindRow(world.residents, resident);
  if (row == kNoRow) {
    return where;  // kUnknown: no such person
  }
  where.resident = resident;
  const ResidentRow& person = world.residents.rows[row];

  // The house is where he is when he is not at work, and it is also the
  // fallback address: a resident whose household has no house yet (genesis
  // before units, a wedding the same step) is at home at no address rather
  // than nowhere — the place is known, only its coordinates are not.
  UnitId house;
  const std::uint32_t family_row = FindRow(world.families, person.family);
  if (family_row != kNoRow) {
    house = world.families.rows[family_row].house;
  }

  const DayWindow window = SolarWindow(world.weather.daylight_hours);
  const bool at_work = person.work.kind != WorkKind::kNone &&
                       InsideDaylight(HourFromTick(world.calendar.tick), window);
  if (!at_work) {
    where.place = Whereabouts::kAtHome;
    where.unit = house;
    where.to = UnitPosition(world, house);
    return where;
  }

  where.place = Whereabouts::kAtWork;
  if (person.work.kind == WorkKind::kHerdCare) {
    where.herd = person.work.herd;
    const std::uint32_t herd_row = FindRow(world.herds, person.work.herd);
    if (herd_row != kNoRow) {
      // A herd stands either at a unit or at a family's yard (herd_state.h);
      // in the second case the address is that yard's house.
      const HerdRow& herd = world.herds.rows[herd_row];
      if (herd.unit.value != kInvalidEntityIdValue) {
        where.unit = herd.unit;
      } else if (const std::uint32_t yard = FindRow(world.families, herd.household);
                 yard != kNoRow) {
        where.unit = world.families.rows[yard].house;
      }
    }
    where.to = UnitPosition(world, where.unit);
    return where;
  }

  where.field = person.work.field;
  const std::uint32_t field_row = FindRow(world.fields, person.work.field);
  if (field_row != kNoRow) {
    where.to = world.fields.rows[field_row].center;
  }
  return where;
}

void CollectAlarms(const WorldState& /*world*/, std::vector<Alarm>& alarms) {
  // STUB, and deliberately not empty of intent: the roster is task A3's, and
  // when it lands every predicate is appended HERE, in kind order — that half
  // of the promised order (session.h: by kind, then by subject id) comes free
  // from the order the predicates are written in.
  //
  // The other half does NOT come free, and this is the note A3 must not miss:
  // a table's row order is NOT id order. Removal is swap-with-last, so row
  // order follows the history of deaths and demolitions (state_table.h says
  // so outright, and tells order-sensitive consumers to sort by id first). A
  // predicate that simply sweeps rows would therefore hand the presentation a
  // list that reshuffles whenever an unrelated entity dies — deterministic,
  // and still wrong. Each predicate sorts its own findings by subject id
  // before appending, or sweeps by id; the cheapest place to do it is here,
  // per kind, while the group is small.
  alarms.clear();
}

}  // namespace core
