// The groom's logistics' numbers (logistics_config.h).

#include "logistics_config.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "core_catalog/table_value.h"
#include "core_tables/tables.h"

namespace core {
namespace {

/// The level a cell may name: 0 urgent to 3 background.
constexpr Range kLevelRange{.low = 0.0F, .high = static_cast<float>(kLogisticsLevelCount - 1U)};

/// The ageing's days: a game year at most, a bound against a slipped digit.
constexpr Range kAgeRange{.low = 1.0F, .high = 48.0F};

bool ParseLevels(const ITable& table, LogisticsConfig& config, std::string& error) {
  constexpr std::array<std::string_view, kLogisticsLoadKindCount> kKeys = {
      "level_field_heap",
      "level_stand_logs",
      "level_site_dig",
      "level_district_lot",
      "level_store_transfer",
  };
  for (std::uint32_t kind = 0; kind < kLogisticsLoadKindCount; ++kind) {
    float level = static_cast<float>(config.default_level[kind]);
    if (!OptionalValue(table, kKeys[kind], kLevelRange, level, error)) {
      PrefixError("logistics", kKeys[kind], error);
      return false;
    }
    config.default_level[kind] = static_cast<LogisticsLevel>(static_cast<std::uint8_t>(level));
  }
  return true;
}

bool ParseKnobs(const ITable& table, LogisticsConfig& config, std::string& error) {
  const std::array<ScalarKnob, 4> knobs = {{
      ScalarKnob{
          .key = "age_background_days", .value = &config.age_background_days, .range = kAgeRange},
      ScalarKnob{
          .key = "age_ordinary_days", .value = &config.age_ordinary_days, .range = kAgeRange},
      ScalarKnob{
          .key = "urgent_spoil_share", .value = &config.urgent_spoil_share, .range = Range::Unit()},
      ScalarKnob{.key = "urgent_spoil_tonnes",
                 .value = &config.urgent_spoil_tonnes,
                 .range = Range{.low = 0.0F, .high = 1000.0F}},
  }};
  for (const ScalarKnob& knob : knobs) {
    if (!OptionalValue(table, knob.key, knob.range, *knob.value, error)) {
      PrefixError("logistics", knob.key, error);
      return false;
    }
  }
  return true;
}

/// What spoils and what feeds, by resource — the cells production reads
/// (production_config.cpp), by the same rules: an empty spoil_days keeps for
/// ever, a feed_value above nought feeds.
bool ParseResources(const ITable& resources, LogisticsConfig& config, std::string& error) {
  config.spoil_days.assign(resources.RowCount(), 0.0F);
  config.feed.assign(resources.RowCount(), 0);
  const std::uint32_t days_column = resources.FindColumn("spoil_days");
  const std::uint32_t feed_column = resources.FindColumn("feed_value");
  for (std::uint32_t row = 0; row < resources.RowCount(); ++row) {
    if (!CellOrDefault(resources,
                       row,
                       days_column,
                       Range{.low = 0.0F, .high = 100000.0F},
                       0.0F,
                       config.spoil_days[row],
                       error)) {
      error = "resources: spoil_days: " + error;
      return false;
    }
    float feed_value = 0.0F;
    if (!CellOrDefault(
            resources, row, feed_column, Range::NonNegative(), 0.0F, feed_value, error)) {
      error = "resources: feed_value: " + error;
      return false;
    }
    config.feed[row] = feed_value > 0.0F ? 1U : 0U;
  }
  return true;
}

}  // namespace

bool ParseLogisticsConfig(const ITableSet& tables, LogisticsConfig& config, std::string& error) {
  if (const ITable* const logistics = tables.FindTable("logistics")) {
    if (!ParseLevels(*logistics, config, error) || !ParseKnobs(*logistics, config, error)) {
      return false;
    }
  }
  if (const ITable* const resources = tables.FindTable("resources")) {
    if (!ParseResources(*resources, config, error)) {
      return false;
    }
  }
  if (const ITable* const world = tables.FindTable("world_params")) {
    if (!OptionalValue(*world,
                       "field_heap_keeping_factor",
                       Range{.low = 0.01F, .high = 1.0F},
                       config.field_heap_keeping_factor,
                       error)) {
      PrefixError("world_params", "field_heap_keeping_factor", error);
      return false;
    }
  }
  return true;
}

}  // namespace core
