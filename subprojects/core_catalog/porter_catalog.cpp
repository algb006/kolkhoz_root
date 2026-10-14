/// @file
/// @brief Implementation of porter_catalog.h.

#include "core_catalog/porter_catalog.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core_catalog/table_value.h"
#include "core_common/cargo_form.h"
#include "core_common/haul.h"
#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_tables/tables.h"

namespace core {

bool ReadCargoForms(const ITable& resources, std::vector<CargoForm>& forms, std::string& error) {
  forms.assign(resources.RowCount(), CargoForm::kNone);
  const std::uint32_t column = resources.FindColumn("cargo_form");
  if (column == kNoTableColumn) {
    return true;
  }
  for (std::uint32_t row = 0; row < resources.RowCount(); ++row) {
    const std::string_view word = resources.CellText(row, column);
    const CargoForm form = word == "bulk"     ? CargoForm::kBulk
                           : word == "manure" ? CargoForm::kManure
                           : word == "packed" ? CargoForm::kPacked
                           : word == "rick"   ? CargoForm::kRick
                           : word == "long"   ? CargoForm::kLong
                           : word == "live"   ? CargoForm::kLive
                                              : CargoForm::kNone;
    if (form == CargoForm::kNone) {
      error = "resources: cargo_form: row " + std::string(resources.CellText(row, 0)) + ": «" +
              std::string(word) + "» is not a cargo form";
      return false;
    }
    forms[row] = form;
  }
  return true;
}

bool ReadPorterRules(const ITableSet& tables, PorterRules& rules, std::string& error) {
  float adult_kg = 20.0F;
  float rick_kg = -1.0F;  // absent: the adult's (a table set before the key)
  float pair_kg = -1.0F;  // absent: two adults'
  if (const ITable* const labor = tables.FindTable("labor")) {
    struct Cell {
      std::string_view key;
      float* value;
      Range range;
    };

    const Cell cells[] = {
        {.key = "carry_kg_adult", .value = &adult_kg, .range = {.low = 0.1F, .high = 1000.0F}},
        {.key = "carry_kg_rick", .value = &rick_kg, .range = {.low = 0.1F, .high = 1000.0F}},
        {.key = "carry_kg_stretcher_pair",
         .value = &pair_kg,
         .range = {.low = 0.2F, .high = 2000.0F}},
        {.key = "log_pair_max_leg_m",
         .value = &rules.log_pair_max_leg_m,
         .range = {.low = 0.0F, .high = 100000.0F}},
        {.key = "log_pair_loaded_pace",
         .value = &rules.log_pair_loaded_pace,
         .range = {.low = 0.05F, .high = 1.0F}},
    };
    for (const Cell& cell : cells) {
      if (!OptionalValue(*labor, cell.key, cell.range, *cell.value, error)) {
        PrefixError("labor", cell.key, error);
        return false;
      }
    }
  }
  rules.carry.adult = GramsFromKilograms(adult_kg);
  rules.carry.rick = GramsFromKilograms(rick_kg > 0.0F ? rick_kg : adult_kg);
  rules.carry.stretcher_pair = GramsFromKilograms(pair_kg > 0.0F ? pair_kg : 2.0F * adult_kg);
  const ITable* const resources = tables.FindTable("resources");
  if (resources == nullptr) {
    return true;
  }
  if (!ReadCargoForms(*resources, rules.cargo_form, error)) {
    return false;
  }
  // THE SHARE OF A CART'S TONNAGE BY RESOURCE (resources.csv `load_share`;
  // 0.37.252): an empty cell is the whole cart, a table with no such column
  // leaves the list empty - the whole cart for every load.
  rules.load_share.clear();
  const std::uint32_t share_column = resources->FindColumn("load_share");
  if (share_column != kNoTableColumn) {
    rules.load_share.assign(resources->RowCount(), 1.0F);
    for (std::uint32_t row = 0; row < resources->RowCount(); ++row) {
      if (!OptionalCell(*resources,
                        row,
                        share_column,
                        Range{.low = 0.05F, .high = 1.0F},
                        rules.load_share[row],
                        error)) {
        PrefixError("resources", "load_share", error);
        return false;
      }
    }
  }
  const std::uint32_t log_row = resources->FindRowByKey("log");
  if (log_row != kNoTableRow) {
    rules.log = ResourceId{static_cast<std::uint16_t>(log_row)};
    float piece_kg = 0.0F;
    if (!OptionalCell(*resources,
                      log_row,
                      resources->FindColumn("kg_per_unit"),
                      Range{.low = 0.0F, .high = 100000.0F},
                      piece_kg,
                      error)) {
      PrefixError("resources", "kg_per_unit", error);
      return false;
    }
    rules.log_piece_grams = GramsFromKilograms(piece_kg);
  }
  return true;
}

}  // namespace core
