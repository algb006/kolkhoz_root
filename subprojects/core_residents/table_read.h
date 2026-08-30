/// @file
/// @brief Reading optional cells out of a balance table, with ranges.
/// @threading SINGLE_THREADED
/// Factory-time code only: every config parser of core_residents runs once,
/// on the setup thread, before the simulation exists.
///
/// The policy these helpers implement is the one every subsystem factory
/// shares (labor_config.cpp says it first): a MISSING table or key keeps the
/// canonical default — a unit test's world has no tables at all — while a
/// PRESENT cell that cannot be read or falls out of range is an error. A
/// half-understood balance is worse than none.
///
/// Not a bag of utilities: this header does one thing, and it is included
/// only by the two config parsers of this module.

#ifndef CORE_RESIDENTS_TABLE_READ_H_
#define CORE_RESIDENTS_TABLE_READ_H_

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "core_tables/tables.h"

namespace core {

/// @brief One scalar knob of a key/value table: where it lands and what it
/// may be.
struct ScalarKnob {
  std::string_view key;
  float* value;
  float low;
  float high;
};

/// @brief Turns "a cell is not a number" into "food: ration_auto: a cell is
/// not a number", building the message with appends only.
inline void PrefixError(std::string_view table, std::string_view key, std::string& error) {
  std::string message(table);
  message += ": ";
  if (!key.empty()) {
    message += key;
    message += ": ";
  }
  message += error;
  error = std::move(message);
}

/// @brief Reads one cell as a real number.
/// @param low,high Inclusive bounds; the test is written positively so that
///        NaN fails it (NaN compares false against everything).
/// @return false only when the cell is present and unreadable or out of
///         range; an absent row, absent column or empty cell leaves `value`
///         untouched and succeeds.
inline bool OptionalCell(const ITable& table,
                         std::uint32_t row,
                         std::uint32_t column,
                         float low,
                         float high,
                         float& value,
                         std::string& error) {
  if (row == kNoTableRow || column == kNoTableColumn || table.CellText(row, column).empty()) {
    return true;
  }
  const std::optional<float> cell = table.CellReal(row, column);
  if (!cell) {
    error = "a cell is not a number";
    return false;
  }
  const bool in_range = *cell >= low && *cell <= high;
  if (!in_range) {
    error = "a cell is out of range";
    return false;
  }
  value = *cell;
  return true;
}

/// @brief Reads one key's `value` cell of a key/value table.
inline bool OptionalValue(const ITable& table,
                          std::string_view key,
                          float low,
                          float high,
                          float& value,
                          std::string& error) {
  return OptionalCell(
      table, table.FindRowByKey(key), table.FindColumn("value"), low, high, value, error);
}

/// @brief Reads a whole run of scalar knobs, naming the offender on failure.
inline bool ReadKnobs(const ITable& table,
                      std::string_view table_name,
                      std::span<const ScalarKnob> knobs,
                      std::string& error) {
  for (const ScalarKnob& knob : knobs) {
    if (!OptionalValue(table, knob.key, knob.low, knob.high, *knob.value, error)) {
      PrefixError(table_name, knob.key, error);
      return false;
    }
  }
  return true;
}

}  // namespace core

#endif  // CORE_RESIDENTS_TABLE_READ_H_
