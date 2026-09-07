/// @file
/// @brief Implementation of table_lookup.h.

#include "core_catalog/table_lookup.h"

#include <string>

#include "core_tables/tables.h"

namespace core {
KeyState LookupRow(const ITable* roster,
                   const ITable& table,
                   std::uint32_t row,
                   std::uint32_t column,
                   std::uint32_t& row_index) {
  if (roster == nullptr) {
    return KeyState::kNoRoster;
  }
  const std::string_view key = table.CellText(row, column);
  if (key.empty()) {
    return KeyState::kEmpty;
  }
  const std::uint32_t found = roster->FindRowByKey(key);
  if (found == kNoTableRow) {
    return KeyState::kNotFound;
  }
  row_index = found;
  return KeyState::kFound;
}

bool RequiredRow(const ITable* roster,
                 const ITable& table,
                 std::uint32_t row,
                 std::uint32_t column,
                 std::string_view column_name,
                 bool allow_empty,
                 std::uint32_t& row_index,
                 std::string& error) {
  const KeyState state = LookupRow(roster, table, row, column, row_index);
  if (state == KeyState::kFound || state == KeyState::kNoRoster) {
    return true;
  }
  // THE ROW NUMBER IS THE FILE'S, not the parser's: a person opens the csv
  // and counts from the first data line, and the header is not one of them.
  const std::string where = "row " + std::to_string(row + 1) + ": " + std::string(column_name);
  if (state == KeyState::kEmpty) {
    if (allow_empty) {
      return true;
    }
    error = where + " is empty";
    return false;
  }
  error = where + " '" + std::string(table.CellText(row, column)) + "' is unknown";
  return false;
}

bool RequiredResource(const ITable* roster,
                      const ITable& table,
                      std::uint32_t row,
                      std::uint32_t column,
                      std::string_view column_name,
                      bool allow_empty,
                      ResourceId& id,
                      std::string& error) {
  std::uint32_t row_index = kNoTableRow;
  if (!RequiredRow(roster, table, row, column, column_name, allow_empty, row_index, error)) {
    return false;
  }
  if (row_index != kNoTableRow) {
    id = DefIdFromRow<ResourceIdTag>(row_index);
  }
  return true;
}

}  // namespace core
