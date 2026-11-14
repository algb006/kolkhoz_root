#include "spring_flood.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core_common/land_state.h"
#include "core_common/ledger_state.h"
#include "core_common/meadow_mark.h"
#include "core_common/obstacle_raster.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"

namespace core {
namespace {

/// The day of the year of a month's first day (0-based month).
constexpr SimDay FirstDayOf(std::uint8_t month) {
  return static_cast<SimDay>(month) * kDaysPerMonth;
}

/// Whether `resource` is one the flood takes: the hay or the straw.
bool Floodable(const ProductionConfig& config, ResourceId resource) {
  return (config.hay_resource.value != kInvalidDefIdValue &&
          resource.value == config.hay_resource.value) ||
         (config.straw_resource.value != kInvalidDefIdValue &&
          resource.value == config.straw_resource.value);
}

/// The calendar year (1-based) of `world`.
std::uint16_t YearOf(const WorldState& world) {
  return static_cast<std::uint16_t>((world.calendar.day / kDaysPerYear) + 1U);
}

/// Whether a flood may still come this year: from year 2, not yet come.
bool FloodStillAhead(const WorldState& world) {
  const std::uint16_t year = YearOf(world);
  return year >= 2 && world.flood.last_flood_year != year;
}

/// The obstacle raster of the config's map; null without one.
const ObstacleRaster* RasterOf(const ProductionConfig& config) {
  if (!config.meadow_ground) {
    return nullptr;
  }
  return config.meadow_ground(true).raster;
}

/// Whether a unit's cell is floodplain on `raster` (null: never).
bool UnitRowFloods(const ObstacleRaster* raster, const UnitRow& unit) {
  return raster != nullptr && raster->HasMap() &&
         (raster->FlagsAt(unit.position) & kObstacleFloodplain) != 0;
}

void AddGrams(ResourceAmounts& amounts, ResourceId resource, Grams grams) {
  if (amounts.size() <= resource.value) {
    amounts.resize(static_cast<std::size_t>(resource.value) + 1U, 0);
  }
  amounts[resource.value] += grams;
}

}  // namespace

bool UnitFloods(const ProductionConfig& config, const WorldState& world, UnitId unit) {
  const std::uint32_t row = FindRow(world.units, unit);
  return row != kNoRow && UnitRowFloods(RasterOf(config), world.units.rows[row]);
}

FloodplainFodder FloodplainFodderOf(const ProductionConfig& config, const WorldState& world) {
  FloodplainFodder fodder;
  for (const FieldRow& field : world.fields.rows) {
    if (field.kind == LandKind::kFloodplainMeadow && field.reaped_grams > 0 &&
        Floodable(config, field.reaped_resource)) {
      AddGrams(fodder.grams, field.reaped_resource, field.reaped_grams);
      fodder.total += field.reaped_grams;
      ++fodder.meadow_heaps;
    }
  }
  const ObstacleRaster* const raster = RasterOf(config);
  for (const UnitRow& unit : world.units.rows) {
    Grams held = 0;
    for (const ResourceId resource : {config.hay_resource, config.straw_resource}) {
      if (resource.value != kInvalidDefIdValue && resource.value < unit.stock.size()) {
        held += unit.stock[resource.value];
      }
    }
    if (held <= 0) {
      continue;
    }
    if (!UnitRowFloods(raster, unit)) {
      ++fodder.stacks_above;
      continue;
    }
    ++fodder.stacks_on_flood;
    for (const ResourceId resource : {config.hay_resource, config.straw_resource}) {
      if (resource.value != kInvalidDefIdValue && resource.value < unit.stock.size() &&
          unit.stock[resource.value] > 0) {
        AddGrams(fodder.grams, resource, unit.stock[resource.value]);
        fodder.total += unit.stock[resource.value];
      }
    }
  }
  return fodder;
}

bool IsFloodDay(const WorldState& previous, const WorldState& current) {
  if (!FloodStillAhead(current)) {
    return false;
  }
  const SimDay of_year = current.calendar.day % kDaysPerYear;
  if (of_year < FirstDayOf(kFloodFirstMonth)) {
    return false;
  }
  // THE STUB FALLBACK (boss [2] p. 1): a cover that never clears floods on 1 May.
  if (of_year >= FirstDayOf(kFloodFallbackMonth)) {
    return true;
  }
  if (current.weather.snow_cover_days != 0) {
    return false;
  }
  // The cover's clearing today; or 1 March with the cover gone already.
  return previous.weather.snow_cover_days > 0 || of_year == FirstDayOf(kFloodFirstMonth);
}

void RunSpringFlood(const ProductionConfig& config,
                    const WorldState& previous,
                    WorldState& current) {
  if (!IsFloodDay(previous, current)) {
    return;
  }
  ResourceAmounts& lost = current.ledger.current.lost_to_flood;
  for (FieldRow& field : current.fields.rows) {
    if (field.kind == LandKind::kFloodplainMeadow && field.reaped_grams > 0 &&
        Floodable(config, field.reaped_resource)) {
      AddGrams(lost, field.reaped_resource, field.reaped_grams);
      field.reaped_grams = 0;
    }
  }
  const ObstacleRaster* const raster = RasterOf(config);
  for (UnitRow& unit : current.units.rows) {
    if (!UnitRowFloods(raster, unit)) {
      continue;
    }
    for (const ResourceId resource : {config.hay_resource, config.straw_resource}) {
      if (resource.value != kInvalidDefIdValue && resource.value < unit.stock.size() &&
          unit.stock[resource.value] > 0) {
        AddGrams(lost, resource, unit.stock[resource.value]);
        unit.stock[resource.value] = 0;
      }
    }
  }
  current.flood.last_flood_year = YearOf(current);
  current.flood.water_until_day = current.calendar.day + config.farming.flood_water_days;
}

void CollectFloodAlarms(const ProductionConfig& config,
                        const WorldState& world,
                        std::vector<Alarm>& alarms) {
  // From 1 December to the flood: ProjectedFloodDay names no day from 1 May to the end of
  // November, and from 1 December names next spring's.
  const SimDay projected = ProjectedFloodDay(world);
  if (projected == kNoFloodDay) {
    return;
  }
  const FloodplainFodder fodder = FloodplainFodderOf(config, world);
  if (fodder.total <= 0) {
    return;
  }
  Alarm alarm;
  alarm.kind = AlarmKind::kFloodplainFodderAhead;
  alarm.lamp = 0;  // the colour is painted from days_to_loss with every lamp (alarm_state.cpp)
  alarm.resource = config.hay_resource;
  alarm.amount = fodder.total;
  const auto days = static_cast<std::int64_t>(projected - world.calendar.day);
  alarm.days_ahead = static_cast<std::uint16_t>(std::clamp<std::int64_t>(days, 0, 0xFFFF));
  alarm.days_to_loss = DaysToLossOf(days);
  alarms.push_back(alarm);
}

}  // namespace core
