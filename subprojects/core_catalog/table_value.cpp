// The table-value policy of table_value.h.

#include "core_catalog/table_value.h"

#include <cassert>
#include <optional>
#include <utility>

namespace core {

void PrefixError(std::string_view table, std::string_view key, std::string& error) {
  std::string message(table);
  message += ": ";
  if (!key.empty()) {
    message += key;
    message += ": ";
  }
  message += error;
  error = std::move(message);
}

CellState ReadCell(const ITable& table,
                   std::uint32_t row,
                   std::uint32_t column,
                   Range range,
                   float& value,
                   std::string& error) {
  if (row == kNoTableRow || column == kNoTableColumn || table.CellText(row, column).empty()) {
    return CellState::kAbsent;
  }
  const std::optional<float> cell = table.CellReal(row, column);
  if (!cell) {
    error = "a cell is not a number";
    return CellState::kBad;
  }
  // Positive test, so NaN and infinity fail it — see the note in the header.
  if (!(*cell >= range.low && *cell <= range.high)) {
    error = "a cell is out of range";
    return CellState::kBad;
  }
  value = *cell;
  return CellState::kRead;
}

bool OptionalCell(const ITable& table,
                  std::uint32_t row,
                  std::uint32_t column,
                  Range range,
                  float& value,
                  std::string& error) {
  return ReadCell(table, row, column, range, value, error) != CellState::kBad;
}

bool CellOrDefault(const ITable& table,
                   std::uint32_t row,
                   std::uint32_t column,
                   Range range,
                   float fallback,
                   float& value,
                   std::string& error) {
  const CellState state = ReadCell(table, row, column, range, value, error);
  if (state == CellState::kAbsent) {
    value = fallback;
  }
  return state != CellState::kBad;
}

bool OptionalValue(
    const ITable& table, std::string_view key, Range range, float& value, std::string& error) {
  return ReadCell(table, table.FindRowByKey(key), table.FindColumn("value"), range, value, error) !=
         CellState::kBad;
}

bool RequiredValue(const ITable& table,
                   std::string_view table_name,
                   std::string_view key,
                   Range range,
                   float& value,
                   std::string& error) {
  const CellState state =
      ReadCell(table, table.FindRowByKey(key), table.FindColumn("value"), range, value, error);
  if (state == CellState::kRead) {
    return true;
  }
  if (state == CellState::kAbsent) {
    error = "no such row, or no value column";
  }
  PrefixError(table_name, key, error);
  return false;
}

bool ReadKnobs(const ITable& table,
               std::string_view table_name,
               std::span<const ScalarKnob> knobs,
               std::string& error) {
  for (const ScalarKnob& knob : knobs) {
    assert(knob.value != nullptr && "ScalarKnob with no destination — array declared too large");
    if (!OptionalValue(table, knob.key, knob.range, *knob.value, error)) {
      PrefixError(table_name, knob.key, error);
      return false;
    }
  }
  return true;
}

}  // namespace core
