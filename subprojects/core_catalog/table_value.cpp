// The table-value policy of table_value.h.

#include "core_catalog/table_value.h"

#include <algorithm>
#include <cassert>
#include <optional>
#include <string>
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
  // THE THREE KINDS OF NOTHING, TOLD APART (task A6, 2026-09-07). They used
  // to share one answer, and the caller who wanted the difference had to ask
  // the table again on its own — which made "is this column even here" a
  // question every reader had to remember to re-ask, and only one did.
  // BOTH WAYS A ROW CAN BE MISSING, and the second was found by the cycle
  // rather than by me: CellText returns an empty view for an index past the
  // end as well as for a blank cell, so a row beyond RowCount() used to fall
  // through into kEmpty — and the refusal built on kEmpty would then have
  // named a row number that does not exist. The sentinel alone is not "no
  // such row"; it is only the way a lookup SAYS so.
  if (row == kNoTableRow || row >= table.RowCount()) {
    return CellState::kNoRow;
  }
  if (column == kNoTableColumn) {
    return CellState::kNoColumn;
  }
  if (table.CellText(row, column).empty()) {
    return CellState::kEmpty;
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

bool RequiredCell(const ITable& table,
                  std::string_view table_name,
                  std::string_view column_name,
                  std::uint32_t row,
                  std::uint32_t column,
                  Range range,
                  float& value,
                  std::string& error) {
  const CellState state = ReadCell(table, row, column, range, value, error);
  if (state == CellState::kNoRow) {
    // A ROW THAT IS NOT THERE IS REFUSED TOO, and the first draft of this
    // wrapper let it pass in silence. A missing COLUMN is a table this build
    // was not given, which is a conversation held elsewhere; a missing ROW is
    // a caller asking about something that does not exist, and answering
    // "fine" to that is the same silent shape this wrapper was written
    // against.
    error = std::string(table_name) + ": " + std::string(column_name) + " was required of row " +
            std::to_string(row) + ", and there is no such row";
    return false;
  }
  if (state == CellState::kEmpty) {
    // Named in full, because the reader of this message is looking at a
    // spreadsheet and needs to be told which cell to fill, not which rule
    // was broken.
    error = std::string(table_name) + ": " + std::string(column_name) + " is empty in row " +
            std::to_string(row) + " — a column that is there must answer for every row";
    return false;
  }
  return state != CellState::kBad;
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
  if (IsAbsent(state)) {
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
  if (IsAbsent(state)) {
    error = "no such key, no value column, or the cell is blank";
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

bool CheckDeclaredReaders(const ITable& table,
                          std::string_view table_name,
                          std::span<const std::string_view> known,
                          std::string& error) {
  const std::uint32_t key_column = table.FindColumn("key");
  const std::uint32_t reader_column = table.FindColumn("reader");
  if (key_column == kNoTableColumn) {
    error = std::string(table_name) + ": no 'key' column";
    return false;
  }
  if (reader_column == kNoTableColumn) {
    error = std::string(table_name) +
            ": no 'reader' column — a table that does not say who reads each knob cannot tell a "
            "misspelt core key from a layer one";
    return false;
  }
  for (std::uint32_t row = 0; row < table.RowCount(); ++row) {
    const std::string_view key = table.CellText(row, key_column);
    const std::string_view reader = table.CellText(row, reader_column);
    if (reader != "core" && reader != "layer" && reader != "both") {
      error = std::string(table_name) + ": row '" + std::string(key) + "' declares reader '" +
              std::string(reader) + "' — it must be core, layer or both";
      return false;
    }
    if (reader == "layer") {
      continue;  // the graphics layer's knob; the core carries it, unread
    }
    if (std::find(known.begin(), known.end(), key) == known.end()) {
      error = std::string(table_name) + ": row '" + std::string(key) +
              "' is declared for the core, and the core has no such knob — a misspelt key, or a "
              "knob whose reader moved";
      return false;
    }
  }
  return true;
}

}  // namespace core
