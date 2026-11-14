#include "core_common/flood_state.h"

#include <cstdint>

#include "core_common/land_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"

namespace core {
namespace {

/// The day of the year of a month's first day (0-based month).
constexpr SimDay FirstDayOf(std::uint8_t month) {
  return static_cast<SimDay>(month) * kDaysPerMonth;
}

}  // namespace

SimDay ProjectedFloodDay(const WorldState& world) {
  const SimDay of_year = world.calendar.day % kDaysPerYear;
  const SimDay year_start = world.calendar.day - of_year;
  // From 1 December the flood ahead is next spring's: next year is the second at least.
  if (of_year >= FirstDayOf(kFloodLampFirstMonth)) {
    return year_start + kDaysPerYear + FirstDayOf(kFloodFirstMonth);
  }
  const auto year = static_cast<std::uint16_t>((world.calendar.day / kDaysPerYear) + 1U);
  if (year < 2 || world.flood.last_flood_year == year) {
    return kNoFloodDay;
  }
  if (of_year < FirstDayOf(kFloodFirstMonth)) {
    return year_start + FirstDayOf(kFloodFirstMonth);
  }
  if (of_year <= FirstDayOf(kFloodFallbackMonth)) {
    return world.calendar.day;
  }
  return kNoFloodDay;
}

bool FieldFloods(const WorldState& world, FieldId field) {
  const std::uint32_t row = FindRow(world.fields, field);
  return row != kNoRow && world.fields.rows[row].kind == LandKind::kFloodplainMeadow;
}

SimDay FloodDeadlineOfField(const WorldState& world, FieldId field) {
  return FieldFloods(world, field) ? ProjectedFloodDay(world) : kNoFloodDay;
}

}  // namespace core
