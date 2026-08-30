// Parsing of the stage-6 food configuration (food_config.h): tables/food.csv
// plus the two columns the seed fund needs from crops.csv and the resource
// roster that sizes everything dense by ResourceId.
//
// food.csv has the labor.csv shape: scalar knobs read their `value` cell,
// edible resources read their own columns. The two kinds of row share one
// file because they are one balance topic — what a person eats and what the
// kolkhoz hands out for it — and one file is one reload.
//
// Months in the table are human 1..12, like every other table of the core;
// the 0-based Month enum is internal, so parsing subtracts one.

#include "food_config.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/ids.h"
#include "core_tables/tables.h"
#include "table_read.h"

namespace core {
namespace {

/// @brief The design's own nine-category roster, in FoodCategory order. A
/// cell naming none of them leaves the resource uneaten rather than
/// guessing — but an unreadable cell is an error, so a typo is loud.
constexpr std::array<std::string_view, static_cast<std::uint32_t>(FoodCategory::kCount)>
    kCategoryNames = {"bread",
                      "potato",
                      "vegetables",
                      "dairy",
                      "meat_and_egg",
                      "fish",
                      "fruit_and_berries",
                      "sweet",
                      "fats"};

bool ParseCategory(std::string_view text, FoodCategory& category, std::string& error) {
  for (std::uint32_t index = 0; index < kCategoryNames.size(); ++index) {
    if (kCategoryNames[index] == text) {
      category = static_cast<FoodCategory>(index);
      return true;
    }
  }
  error = "unknown food category '";
  error += std::string(text);
  error += "'";
  return false;
}

/// @brief A month knob written 1..12 in the file and 0-based in the struct.
bool ReadMonth(const ITable& table, std::string_view key, std::uint8_t& month, std::string& error) {
  auto human = static_cast<float>(month) + 1.0F;
  if (!OptionalValue(table, key, 1.0F, 12.0F, human, error)) {
    PrefixError("food", key, error);
    return false;
  }
  month = static_cast<std::uint8_t>(human - 1.0F);
  return true;
}

bool ParseConsumptionAndSatiety(const ITable& table, FoodConfig& config, std::string& error) {
  ConsumptionConfig& eat = config.consumption;
  SatietyConfig& satiety = config.satiety;
  auto heavy_mask = static_cast<float>(eat.heavy_kinds_mask);
  const std::array<ScalarKnob, 18> knobs = {{
      {.key = "adult_kg_grain_eq_per_year",
       .value = &eat.adult_kg_grain_eq_per_year,
       .low = 1.0F,
       .high = 5000.0F},
      {.key = "eat_from_bio_years", .value = &eat.eat_from_bio_years, .low = 0.0F, .high = 20.0F},
      {.key = "adult_from_bio_years",
       .value = &eat.adult_from_bio_years,
       .low = 1.0F,
       .high = 40.0F},
      {.key = "elderly_from_bio_years",
       .value = &eat.elderly_from_bio_years,
       .low = 1.0F,
       .high = 120.0F},
      {.key = "elderly_factor", .value = &eat.elderly_factor, .low = 0.1F, .high = 2.0F},
      {.key = "heavy_work_factor", .value = &eat.heavy_work_factor, .low = 1.0F, .high = 3.0F},
      {.key = "heavy_kinds_mask", .value = &heavy_mask, .low = 0.0F, .high = 4.0e9F},
      {.key = "grain_reference_kcal_per_gram",
       .value = &eat.grain_reference_kcal_per_gram,
       .low = 0.1F,
       .high = 10.0F},
      {.key = "satiety_drift_per_day",
       .value = &satiety.drift_per_day,
       .low = 0.0F,
       .high = 100.0F},
      {.key = "missing_category_penalty",
       .value = &satiety.missing_category_penalty,
       .low = 0.0F,
       .high = 100.0F},
      {.key = "categories_norm_epoch_1",
       .value = satiety.categories_norm_by_epoch.data(),
       .low = 0.0F,
       .high = 9.0F},
      {.key = "categories_norm_epoch_2",
       .value = &satiety.categories_norm_by_epoch[1],
       .low = 0.0F,
       .high = 9.0F},
      {.key = "categories_norm_epoch_3",
       .value = &satiety.categories_norm_by_epoch[2],
       .low = 0.0F,
       .high = 9.0F},
      {.key = "health_loss_satiety_threshold",
       .value = &satiety.health_loss_satiety_threshold,
       .low = 0.0F,
       .high = 100.0F},
      {.key = "health_loss_per_week",
       .value = &satiety.health_loss_per_week,
       .low = 0.0F,
       .high = 100.0F},
      {.key = "health_recovery_satiety_threshold",
       .value = &satiety.health_recovery_satiety_threshold,
       .low = 0.0F,
       .high = 100.0F},
      {.key = "health_recovery_per_week",
       .value = &satiety.health_recovery_per_week,
       .low = 0.0F,
       .high = 100.0F},
      {.key = "ration_satiety_threshold",
       .value = &config.distribution.ration_satiety_threshold,
       .low = 0.0F,
       .high = 100.0F},
  }};
  if (!ReadKnobs(table, "food", knobs, error)) {
    return false;
  }
  eat.heavy_kinds_mask = static_cast<std::uint32_t>(heavy_mask);
  return true;
}

bool ParseDistribution(const ITable& table, FoodConfig& config, std::string& error) {
  DistributionConfig& distribution = config.distribution;
  auto period = static_cast<float>(distribution.period_days);
  auto ration_auto = static_cast<float>(distribution.ration_auto);
  auto reserve_seed = static_cast<float>(distribution.reserve_seed_fund);
  const std::array<ScalarKnob, 3> knobs = {{
      {.key = "distribution_period_days",
       .value = &period,
       .low = 1.0F,
       .high = static_cast<float>(kDaysPerYear)},
      {.key = "ration_auto", .value = &ration_auto, .low = 0.0F, .high = 1.0F},
      {.key = "reserve_seed_fund", .value = &reserve_seed, .low = 0.0F, .high = 1.0F},
  }};
  if (!ReadKnobs(table, "food", knobs, error)) {
    return false;
  }
  distribution.period_days = static_cast<std::uint32_t>(period);
  distribution.ration_auto = static_cast<std::uint8_t>(ration_auto);
  distribution.reserve_seed_fund = static_cast<std::uint8_t>(reserve_seed);
  return true;
}

bool ParsePlot(const ITable& table, FoodConfig& config, std::string& error) {
  PlotConfig& plot = config.plot;
  const std::array<ScalarKnob, 15> knobs = {{
      {.key = "plot_full_yield_hours", .value = &plot.full_yield_hours, .low = 0.1F, .high = 24.0F},
      {.key = "plot_no_worker_base_hours",
       .value = &plot.no_worker_base_hours,
       .low = 0.0F,
       .high = 24.0F},
      {.key = "plot_elders_hours", .value = &plot.elders_hours, .low = 0.0F, .high = 12.0F},
      {.key = "plot_elder_from_bio_years",
       .value = &plot.elder_from_bio_years,
       .low = 1.0F,
       .high = 120.0F},
      {.key = "plot_schoolchild_hours",
       .value = &plot.schoolchild_hours,
       .low = 0.0F,
       .high = 12.0F},
      {.key = "plot_schoolchild_summer_hours",
       .value = &plot.schoolchild_summer_hours,
       .low = 0.0F,
       .high = 12.0F},
      {.key = "plot_schoolchild_hours_cap",
       .value = &plot.schoolchild_hours_cap,
       .low = 0.0F,
       .high = 12.0F},
      {.key = "plot_drinker_hours", .value = &plot.drinker_hours, .low = 0.0F, .high = 12.0F},
      {.key = "plot_drinker_alcoholism_threshold",
       .value = &plot.drinker_alcoholism_threshold,
       .low = 0.0F,
       .high = 100.0F},
      {.key = "plot_sickness_hours", .value = &plot.sickness_hours, .low = 0.0F, .high = 12.0F},
      {.key = "plot_sickness_health_threshold",
       .value = &plot.sickness_health_threshold,
       .low = 0.0F,
       .high = 100.0F},
      {.key = "potato_kg_per_yard_year",
       .value = &plot.potato_kg_per_yard_year,
       .low = 0.0F,
       .high = 1.0e5F},
      {.key = "vegetables_kg_per_yard_year",
       .value = &plot.vegetables_kg_per_yard_year,
       .low = 0.0F,
       .high = 1.0e5F},
      {.key = "fish_kg_per_yard_year_epoch_1",
       .value = plot.fish_kg_per_yard_year.data(),
       .low = 0.0F,
       .high = 1.0e5F},
      {.key = "fish_kg_per_yard_year_epoch_2",
       .value = &plot.fish_kg_per_yard_year[1],
       .low = 0.0F,
       .high = 1.0e5F},
  }};
  if (!ReadKnobs(table, "food", knobs, error)) {
    return false;
  }
  // The tail sits outside the run above only because the knob array has a
  // fixed size; these are the same kind of knob.
  const std::array<ScalarKnob, 4> tail = {{
      {.key = "fish_kg_per_yard_year_epoch_3",
       .value = &plot.fish_kg_per_yard_year[2],
       .low = 0.0F,
       .high = 1.0e5F},
      {.key = "plot_schoolchild_from_bio_years",
       .value = &plot.schoolchild_from_bio_years,
       .low = 0.0F,
       .high = 30.0F},
      {.key = "plot_schoolchild_to_bio_years",
       .value = &plot.schoolchild_to_bio_years,
       .low = 0.0F,
       .high = 30.0F},
  }};
  return ReadKnobs(table, "food", tail, error) &&
         ReadMonth(table, "plot_summer_from_month", plot.summer_from_month, error) &&
         ReadMonth(table, "plot_summer_to_month", plot.summer_to_month, error) &&
         ReadMonth(table, "growing_from_month", plot.growing_from_month, error) &&
         ReadMonth(table, "growing_to_month", plot.growing_to_month, error) &&
         ReadMonth(table, "garden_harvest_month", plot.garden_harvest_month, error) &&
         ReadMonth(table, "hay_harvest_month", plot.hay_harvest_month, error);
}

/// One row per edible resource. The roster is resources.csv; a food.csv row
/// whose key is not in it is a leftover and is skipped without complaint —
/// the balancer may keep a line for a resource of a later epoch.
bool ParseResourceRows(const ITable& food,
                       const ITable& resources,
                       FoodConfig& config,
                       std::string& error) {
  config.resources.assign(resources.RowCount(), FoodResourceDef{});
  const std::uint32_t key_column = resources.FindColumn("key");
  if (key_column == kNoTableColumn) {
    error = "resources: no key column";
    return false;
  }
  const std::uint32_t kcal_column = food.FindColumn("kcal_per_gram");
  const std::uint32_t category_column = food.FindColumn("category");
  const std::uint32_t issue_column = food.FindColumn("issue_kg_per_trudoden");
  const std::uint32_t ration_column = food.FindColumn("ration_kg_per_day");
  for (std::uint32_t row = 0; row < resources.RowCount(); ++row) {
    const std::uint32_t food_row = food.FindRowByKey(resources.CellText(row, key_column));
    if (food_row == kNoTableRow) {
      continue;  // not eaten
    }
    FoodResourceDef& def = config.resources[row];
    if (!OptionalCell(food, food_row, kcal_column, 0.0F, 10.0F, def.kcal_per_gram, error) ||
        !OptionalCell(
            food, food_row, issue_column, 0.0F, 100.0F, def.issue_kg_per_trudoden, error) ||
        !OptionalCell(food, food_row, ration_column, 0.0F, 100.0F, def.ration_kg_per_day, error)) {
      PrefixError("food", resources.CellText(row, key_column), error);
      return false;
    }
    const std::string_view category_text = food.CellText(food_row, category_column);
    if (!category_text.empty() && !ParseCategory(category_text, def.category, error)) {
      PrefixError("food", resources.CellText(row, key_column), error);
      return false;
    }
  }
  return true;
}

/// The seed fund reads two columns of crops.csv directly (food_config.h,
/// SeedNormDef): what the seed of a crop is, and how much of it a hectare
/// swallows. The crop's `resource` cell is a key of resources.csv, so the
/// roster turns it into the dense id everything else uses.
bool ParseSeedNorms(const ITable& crops,
                    const ITable* resources,
                    FoodConfig& config,
                    std::string& error) {
  config.seed_norms.assign(crops.RowCount(), SeedNormDef{});
  const std::uint32_t resource_column = crops.FindColumn("resource");
  const std::uint32_t norm_column = crops.FindColumn("sowing_norm_kg_per_ha");
  for (std::uint32_t row = 0; row < crops.RowCount(); ++row) {
    if (!OptionalCell(crops,
                      row,
                      norm_column,
                      0.0F,
                      1.0e5F,
                      config.seed_norms[row].sowing_norm_kg_per_ha,
                      error)) {
      PrefixError("crops", "sowing_norm_kg_per_ha", error);
      return false;
    }
    if (resources == nullptr || resource_column == kNoTableColumn) {
      continue;  // no roster to resolve against: the crop reserves nothing
    }
    const std::uint32_t resource_row =
        resources->FindRowByKey(crops.CellText(row, resource_column));
    if (resource_row != kNoTableRow) {
      config.seed_norms[row].resource = ResourceId{static_cast<std::uint16_t>(resource_row)};
    }
  }
  return true;
}

/// @brief Row of the resource roster by key, as the dense id; invalid when
/// the roster has no such row (a hand-built world, an older table set).
ResourceId ResourceByKey(const ITable* resources, std::string_view key) {
  if (resources == nullptr) {
    return ResourceId{};
  }
  const std::uint32_t row = resources->FindRowByKey(key);
  return row == kNoTableRow ? ResourceId{} : ResourceId{static_cast<std::uint16_t>(row)};
}

}  // namespace

FoodConfig ParseFoodConfig(const ITableSet& tables, std::string* error) {
  FoodConfig config;
  std::string message;
  const ITable* resources = tables.FindTable("resources");
  if (const ITable* food = tables.FindTable("food")) {
    const bool ok = ParseConsumptionAndSatiety(*food, config, message) &&
                    ParseDistribution(*food, config, message) &&
                    ParsePlot(*food, config, message) &&
                    (resources == nullptr || ParseResourceRows(*food, *resources, config, message));
    if (!ok) {
      if (error != nullptr) {
        *error = message;
      }
      return FoodConfig{};
    }
  }
  if (const ITable* crops = tables.FindTable("crops")) {
    if (!ParseSeedNorms(*crops, resources, config, message)) {
      if (error != nullptr) {
        *error = message;
      }
      return FoodConfig{};
    }
  }
  // Sleep is labor's number, read where it lives rather than copied into a
  // second table (the labor precedent: labor reads life.csv the same way).
  if (const ITable* labor = tables.FindTable("labor")) {
    const std::array<ScalarKnob, 1> knobs = {{
        {.key = "sleep_hours", .value = &config.plot.sleep_hours, .low = 0.0F, .high = 16.0F},
    }};
    if (!ReadKnobs(*labor, "labor", knobs, message)) {
      if (error != nullptr) {
        *error = message;
      }
      return FoodConfig{};
    }
  }
  config.potato_resource = ResourceByKey(resources, "potato");
  config.vegetables_resource = ResourceByKey(resources, "vegetables");
  config.fish_resource = ResourceByKey(resources, "fish");
  config.hay_resource = ResourceByKey(resources, "hay");
  return config;
}

}  // namespace core
