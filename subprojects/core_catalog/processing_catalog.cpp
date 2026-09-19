// The processing catalogue (core_catalog/processing_catalog.h).

#include "core_catalog/processing_catalog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core_catalog/table_value.h"

namespace core {
namespace {

constexpr std::array<std::string_view, 7> kProcessingWorldParamKeys = {"barrel_capacity_kg",
                                                                       "barrel_wear_per_year",
                                                                       "sauerkraut_fresh_share",
                                                                       "sauerkraut_from_month",
                                                                       "sauerkraut_to_month",
                                                                       "cooperage_from_month",
                                                                       "cooperage_to_month"};

/// The recipes the design gives a rule of their own (processing_catalog.h).
constexpr std::string_view kPicklingKey = "pickling";
constexpr std::string_view kCooperageKey = "cooperage";

/// Past this a batch's man-days or a recipe's places are a typo.
constexpr float kMostLaborDaysPerBatch = 100.0F;
constexpr float kMostPlaces = 255.0F;

/// An amount per batch in the resource's own measure: a tonne of cabbage, a
/// barrel, a cubic metre of firewood. Past this it is a typo.
constexpr float kMostAmount = 1000.0F;

ProcessingRule RuleOfKey(std::string_view key) {
  if (key == kPicklingKey) {
    return ProcessingRule::kPickling;
  }
  if (key == kCooperageKey) {
    return ProcessingRule::kCooperage;
  }
  return ProcessingRule::kPlain;
}

/// Resource by key, and the grams of one unit of its measure.
bool ResourceMass(const ITable& resources,
                  std::string_view key,
                  ResourceId& resource,
                  float& kg_per_unit,
                  std::string& error) {
  const std::uint32_t row = resources.FindRowByKey(key);
  if (row == kNoTableRow) {
    error = "names resource '" + std::string(key) + "' that resources has not";
    return false;
  }
  resource = DefIdFromRow<ResourceIdTag>(row);
  return RequiredCell(resources,
                      "resources",
                      "kg_per_unit",
                      row,
                      resources.FindColumn("kg_per_unit"),
                      Range{.low = 0.0001F, .high = 100000.0F},
                      kg_per_unit,
                      error);
}

bool ParseResourceProperties(const ITable& resources,
                             ProcessingCatalog& catalog,
                             std::string& error) {
  const std::uint32_t count = resources.RowCount();
  catalog.space_factor.assign(count, 1.0F);
  catalog.in_barrel.assign(count, 0);
  catalog.very_fast.assign(count, 0);
  const std::uint32_t space_col = resources.FindColumn("space_factor");
  const std::uint32_t barrel_col = resources.FindColumn("in_barrel");
  const std::uint32_t spoilage_col = resources.FindColumn("spoilage");
  for (std::uint32_t row = 0; row < count; ++row) {
    if (spoilage_col != kNoTableColumn) {
      catalog.very_fast[row] = resources.CellText(row, spoilage_col) == "very_fast" ? 1U : 0U;
    }
    // Above nought: a resource that takes no room would fit a store without
    // end. One at most: nothing here takes more room than its mass says.
    if (!OptionalCell(resources,
                      row,
                      space_col,
                      Range{.low = 0.05F, .high = 1.0F},
                      catalog.space_factor[row],
                      error)) {
      PrefixError("resources", "space_factor", error);
      return false;
    }
    float in_barrel = 0.0F;
    if (!OptionalCell(
            resources, row, barrel_col, Range{.low = 0.0F, .high = 1.0F}, in_barrel, error)) {
      PrefixError("resources", "in_barrel", error);
      return false;
    }
    catalog.in_barrel[row] = in_barrel >= 0.5F ? 1U : 0U;
  }
  const std::uint32_t barrel_row = resources.FindRowByKey("barrel");
  catalog.barrel_resource = DefIdFromRow<ResourceIdTag>(barrel_row);
  if (barrel_row != kNoTableRow) {
    float barrel_kg = 0.0F;
    if (!RequiredCell(resources,
                      "resources",
                      "kg_per_unit",
                      barrel_row,
                      resources.FindColumn("kg_per_unit"),
                      Range{.low = 0.1F, .high = 1000.0F},
                      barrel_kg,
                      error)) {
      return false;
    }
    catalog.barrel_grams = GramsFromKilograms(barrel_kg);
  }
  return true;
}

/// The recipe's lines of production_io.csv, its inputs and outputs, in grams.
bool ParseRecipeLines(const ITable& io,
                      const ITable& resources,
                      ProcessingRecipe& recipe,
                      std::string& error) {
  const std::uint32_t production_col = io.FindColumn("production");
  const std::uint32_t resource_col = io.FindColumn("resource");
  const std::uint32_t amount_col = io.FindColumn("amount");
  const std::uint32_t direction_col = io.FindColumn("direction");
  for (std::uint32_t line = 0; line < io.RowCount(); ++line) {
    if (io.CellText(line, production_col) != recipe.key) {
      continue;
    }
    ProcessingAmount amount;
    float kg_per_unit = 0.0F;
    if (!ResourceMass(
            resources, io.CellText(line, resource_col), amount.resource, kg_per_unit, error)) {
      PrefixError("production_io", recipe.key, error);
      return false;
    }
    float in_measure = 0.0F;
    if (!RequiredCell(io,
                      "production_io",
                      "amount",
                      line,
                      amount_col,
                      Range{.low = 0.0F, .high = kMostAmount},
                      in_measure,
                      error)) {
      return false;
    }
    amount.grams = GramsFromKilograms(in_measure * kg_per_unit);
    const std::string_view direction = io.CellText(line, direction_col);
    if (direction == "in") {
      recipe.inputs.push_back(amount);
    } else if (direction == "out") {
      recipe.outputs.push_back(amount);
    } else {
      error = "production_io '" + recipe.key + "': direction '" + std::string(direction) +
              "' is neither in nor out";
      return false;
    }
  }
  return true;
}

bool ParseRecipes(const ITable& production,
                  const ITable& io,
                  const ITable& unit_types,
                  const ITable& resources,
                  ProcessingCatalog& catalog,
                  std::string& error) {
  const std::uint32_t key_col = production.FindColumn("key");
  const std::uint32_t unit_col = production.FindColumn("unit");
  const std::uint32_t labor_col = production.FindColumn("labor_days");
  const std::uint32_t places_col = production.FindColumn("places");
  if (key_col == kNoTableColumn || unit_col == kNoTableColumn ||
      io.FindColumn("production") == kNoTableColumn ||
      io.FindColumn("resource") == kNoTableColumn || io.FindColumn("amount") == kNoTableColumn ||
      io.FindColumn("direction") == kNoTableColumn) {
    error =
        "production and production_io need key, unit and production, resource, amount, "
        "direction";
    return false;
  }
  catalog.recipes.clear();
  for (std::uint32_t row = 0; row < production.RowCount(); ++row) {
    ProcessingRecipe recipe;
    recipe.key = std::string(production.CellText(row, key_col));
    const std::string_view unit_key = production.CellText(row, unit_col);
    const std::uint32_t unit_row = unit_types.FindRowByKey(unit_key);
    if (unit_row == kNoTableRow) {
      error = "production '" + recipe.key + "' names unit '" + std::string(unit_key) +
              "' that unit_types has not";
      return false;
    }
    recipe.unit_type = DefIdFromRow<UnitTypeIdTag>(unit_row);
    recipe.rule = RuleOfKey(recipe.key);
    float places = 1.0F;
    if (!OptionalCell(production,
                      row,
                      labor_col,
                      Range{.low = 0.0F, .high = kMostLaborDaysPerBatch},
                      recipe.labor_days,
                      error) ||
        !OptionalCell(
            production, row, places_col, Range{.low = 1.0F, .high = kMostPlaces}, places, error)) {
      PrefixError("production", recipe.key, error);
      return false;
    }
    recipe.places = static_cast<std::uint16_t>(std::floor(places));
    if (!ParseRecipeLines(io, resources, recipe, error)) {
      return false;
    }
    // THE SAWMILL AND ANY RECIPE WITHOUT BOTH SIDES is not a shop here: its
    // own door works it, or nothing does (processing_catalog.h).
    if (recipe.inputs.empty() || recipe.outputs.empty() || !(recipe.labor_days > 0.0F)) {
      continue;
    }
    catalog.recipes.push_back(std::move(recipe));
  }
  return true;
}

bool ParseLevelCosts(const ITable& costs,
                     const ITable& unit_types,
                     const ITable& resources,
                     ProcessingCatalog& catalog,
                     std::string& error) {
  const std::uint32_t unit_col = costs.FindColumn("unit");
  const std::uint32_t level_col = costs.FindColumn("level");
  const std::uint32_t resource_col = costs.FindColumn("resource");
  const std::uint32_t amount_col = costs.FindColumn("amount");
  catalog.level_costs.clear();
  for (std::uint32_t row = 0; row < costs.RowCount(); ++row) {
    const std::uint32_t unit_row = unit_types.FindRowByKey(costs.CellText(row, unit_col));
    if (unit_row == kNoTableRow) {
      continue;  // the construction parser owns this table's errors
    }
    LevelCost cost;
    cost.unit_type = DefIdFromRow<UnitTypeIdTag>(unit_row);
    float level = 0.0F;
    float amount = 0.0F;
    float kg_per_unit = 0.0F;
    if (!OptionalCell(costs, row, level_col, Range{.low = 0.0F, .high = 255.0F}, level, error) ||
        !OptionalCell(costs, row, amount_col, Range{.low = 0.0F, .high = 1.0e6F}, amount, error) ||
        !ResourceMass(
            resources, costs.CellText(row, resource_col), cost.resource, kg_per_unit, error)) {
      PrefixError("unit_level_cost", costs.CellText(row, unit_col), error);
      return false;
    }
    cost.level = static_cast<std::uint8_t>(level);
    cost.grams = GramsFromKilograms(amount * kg_per_unit);
    catalog.level_costs.push_back(cost);
  }
  return true;
}

}  // namespace

std::span<const std::string_view> ProcessingWorldParamKeys() {
  return kProcessingWorldParamKeys;
}

bool ParseProcessingCatalog(const ITableSet& tables,
                            ProcessingCatalog& catalog,
                            std::string& error) {
  if (const ITable* const world = tables.FindTable("world_params")) {
    float capacity_kg = static_cast<float>(catalog.barrel_capacity_grams) / 1000.0F;
    // Human months 1..12 in the table, 0-based below, as the pasture's are.
    float from_month = static_cast<float>(catalog.pickling_from_month) + 1.0F;
    float to_month = static_cast<float>(catalog.pickling_to_month) + 1.0F;
    float cooper_from = static_cast<float>(catalog.cooperage_from_month) + 1.0F;
    float cooper_to = static_cast<float>(catalog.cooperage_to_month) + 1.0F;
    const Range months{.low = 1.0F, .high = 12.0F};
    const std::array<ScalarKnob, kProcessingWorldParamKeys.size()> knobs = {{
        {.key = kProcessingWorldParamKeys[0],
         .value = &capacity_kg,
         .range = {.low = 1.0F, .high = 1000.0F}},
        {.key = kProcessingWorldParamKeys[1],
         .value = &catalog.barrel_wear_per_year,
         .range = {.low = 0.0F, .high = 1.0F}},
        {.key = kProcessingWorldParamKeys[2],
         .value = &catalog.sauerkraut_fresh_share,
         .range = {.low = 0.0F, .high = 1.0F}},
        {.key = kProcessingWorldParamKeys[3], .value = &from_month, .range = months},
        {.key = kProcessingWorldParamKeys[4], .value = &to_month, .range = months},
        {.key = kProcessingWorldParamKeys[5], .value = &cooper_from, .range = months},
        {.key = kProcessingWorldParamKeys[6], .value = &cooper_to, .range = months},
    }};
    if (!ReadKnobs(*world, "world_params", knobs, error)) {
      return false;
    }
    catalog.barrel_capacity_grams = GramsFromKilograms(capacity_kg);
    catalog.pickling_from_month = static_cast<std::uint8_t>(from_month - 1.0F);
    catalog.pickling_to_month = static_cast<std::uint8_t>(to_month - 1.0F);
    catalog.cooperage_from_month = static_cast<std::uint8_t>(cooper_from - 1.0F);
    catalog.cooperage_to_month = static_cast<std::uint8_t>(cooper_to - 1.0F);
  }
  const ITable* const resources = tables.FindTable("resources");
  if (resources == nullptr) {
    return true;
  }
  if (!ParseResourceProperties(*resources, catalog, error)) {
    return false;
  }
  const ITable* const unit_types = tables.FindTable("unit_types");
  if (unit_types == nullptr) {
    return true;
  }
  if (const ITable* const costs = tables.FindTable("unit_level_cost")) {
    if (!ParseLevelCosts(*costs, *unit_types, *resources, catalog, error)) {
      return false;
    }
  }
  const ITable* const production = tables.FindTable("production");
  const ITable* const io = tables.FindTable("production_io");
  if (production == nullptr || io == nullptr) {
    return true;  // no shops in this world: nothing processes (the stub worlds)
  }
  return ParseRecipes(*production, *io, *unit_types, *resources, catalog, error);
}

bool WorksTheSameDay(const ProcessingCatalog& catalog, const ProcessingRecipe& recipe) {
  if (recipe.inputs.empty()) {
    return false;
  }
  const std::uint32_t main = recipe.inputs.front().resource.value;
  return main < catalog.very_fast.size() && catalog.very_fast[main] != 0;
}

std::uint32_t ProcessingPlaces(const ProcessingCatalog& catalog, UnitTypeId type) {
  std::uint32_t places = 0;
  for (const ProcessingRecipe& recipe : catalog.recipes) {
    if (recipe.unit_type.value == type.value) {
      places = std::max<std::uint32_t>(places, recipe.places);
    }
  }
  return places;
}

Grams RoomTaken(const ProcessingCatalog& catalog, ResourceId resource, Grams grams) {
  if (resource.value >= catalog.space_factor.size() || grams <= 0) {
    return grams;
  }
  const float factor = catalog.space_factor[resource.value];
  return static_cast<Grams>(std::llround(static_cast<double>(grams) * static_cast<double>(factor)));
}

Grams GramsFitting(const ProcessingCatalog& catalog, ResourceId resource, Grams room) {
  // An outline's room is unbounded (Grams max, FreeRoomGrams) and stays so:
  // divided by a factor below one it would pass int64.
  if (resource.value >= catalog.space_factor.size() || room <= 0 ||
      room == std::numeric_limits<Grams>::max()) {
    return room;
  }
  const float factor = catalog.space_factor[resource.value];
  if (!(factor > 0.0F)) {
    return room;
  }
  // Down, never up: a gram that does not fit is not delivered.
  return static_cast<Grams>(std::floor(static_cast<double>(room) / static_cast<double>(factor)));
}

}  // namespace core
