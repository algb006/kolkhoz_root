// The base conventions of world_params.csv (world_conventions.h): the
// calendar checked against the build, the biology factor read from its home.

#include "core_catalog/world_conventions.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "core_catalog/table_value.h"
#include "core_common/calendar.h"
#include "core_tables/tables.h"

namespace core {
namespace {

constexpr std::string_view kLifeSpeedupKey = "life_speedup";
constexpr Range kLifeSpeedupRange{.low = 0.1F, .high = 100.0F};

constexpr std::uint32_t kMinutesPerHour = 60;

/// A calendar key and the constant of the build it must equal.
struct CalendarConvention {
  std::string_view key;
  std::uint32_t build_value = 0;
};

// The last is derived rather than declared: an hour of the game on ×1 is
// kClockScale times shorter than a real one, so a day of it takes
// 24 × 60 / 12 = 120 real minutes (root rules §9). A table that says 120
// with a clock scale of 12 and 24 hours agrees with itself and the build.
constexpr std::array<CalendarConvention, 6> kCalendarConventions = {{
    {.key = "clock_scale", .build_value = kClockScale},
    {.key = "months_per_year", .build_value = kMonthsPerYear},
    {.key = "days_per_month", .build_value = kDaysPerMonth},
    {.key = "hours_per_day", .build_value = kTicksPerDay},
    {.key = "days_per_week", .build_value = kDaysPerWeek},
    {.key = "real_minutes_per_day_x1", .build_value = kTicksPerDay * kMinutesPerHour / kClockScale},
}};

constexpr std::array<std::string_view, 7> kConventionKeys = {kCalendarConventions[0].key,
                                                             kCalendarConventions[1].key,
                                                             kCalendarConventions[2].key,
                                                             kCalendarConventions[3].key,
                                                             kCalendarConventions[4].key,
                                                             kCalendarConventions[5].key,
                                                             kLifeSpeedupKey};

/// One home's row: read when present, left empty when absent.
bool ReadHome(const ITable* table,
              std::string_view table_name,
              std::optional<float>& value,
              std::string& error) {
  value.reset();
  if (table == nullptr) {
    return true;
  }
  float read = 0.0F;
  const CellState state = ReadCell(*table,
                                   table->FindRowByKey(kLifeSpeedupKey),
                                   table->FindColumn("value"),
                                   kLifeSpeedupRange,
                                   read,
                                   error);
  if (state == CellState::kBad) {
    PrefixError(table_name, kLifeSpeedupKey, error);
    return false;
  }
  if (state == CellState::kRead) {
    value = read;
  }
  return true;
}

}  // namespace

std::span<const std::string_view> ConventionWorldParamKeys() {
  return kConventionKeys;
}

bool FindLifeSpeedup(const ITableSet& tables, std::optional<float>& value, std::string& error) {
  value.reset();
  std::optional<float> from_world;
  std::optional<float> from_life;
  if (!ReadHome(tables.FindTable("world_params"), "world_params", from_world, error) ||
      !ReadHome(tables.FindTable("life"), "life", from_life, error)) {
    return false;
  }
  if (from_world.has_value() && from_life.has_value() && *from_world != *from_life) {
    error = "life_speedup: world_params says " + std::to_string(*from_world) + ", life says " +
            std::to_string(*from_life) +
            " — one factor with two homes; world_params is its home, life.csv carries it only "
            "until it becomes an export";
    return false;
  }
  value = from_world.has_value() ? from_world : from_life;
  return true;
}

float LifeSpeedupOr(const ITableSet& tables, float fallback) {
  std::optional<float> value;
  std::string error;
  return FindLifeSpeedup(tables, value, error) ? value.value_or(fallback) : fallback;
}

bool CheckWorldConventions(const ITableSet& tables, std::string& error) {
  if (const ITable* const world = tables.FindTable("world_params")) {
    // The widest the calendar could honestly be: the check is for EQUALITY,
    // the range only keeps a typo from reaching the integer compare.
    constexpr Range kAnyCount{.low = 1.0F, .high = 10000.0F};
    for (const CalendarConvention& convention : kCalendarConventions) {
      float read = 0.0F;
      const CellState state = ReadCell(*world,
                                       world->FindRowByKey(convention.key),
                                       world->FindColumn("value"),
                                       kAnyCount,
                                       read,
                                       error);
      if (state == CellState::kBad) {
        PrefixError("world_params", convention.key, error);
        return false;
      }
      if (state == CellState::kRead && read != static_cast<float>(convention.build_value)) {
        error = "world_params: " + std::string(convention.key) + " is " + std::to_string(read) +
                " and the build's is " + std::to_string(convention.build_value) +
                " — a base convention is the game's structure, not a knob: it sizes arrays, "
                "times schedules and is in every save, so it changes with the build";
        return false;
      }
    }
  }
  std::optional<float> speedup;
  return FindLifeSpeedup(tables, speedup, error);
}

}  // namespace core
