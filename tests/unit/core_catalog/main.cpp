// Unit test of core_catalog: the one policy for reading a value out of a
// balance table, and the three answers a cell can give.
//
// WHY THIS TEST IS THE POINT OF THE MODULE. The policy used to exist four
// times over, and none of the four was tested directly — each was exercised
// only through whatever config parser happened to own it, which is how two
// functions called CellOrDefault came to take their arguments in different
// orders without anything noticing. A rule with one home can have one test,
// and this is it.

#include <charconv>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core_catalog/table_value.h"
#include "core_tables/tables.h"

namespace {

int Expect(bool condition, const char* label) {
  if (condition) {
    return 0;
  }
  std::cout << "FAIL: " << label << '\n';
  return 1;
}

/// One in-memory table: a header row plus data rows.
///
/// Its CellReal DELIBERATELY lets `nan` and `inf` through, exactly as
/// `from_chars` does and as the shipped reader did until UB-004 was closed.
/// The range test is the second lock on that door, and a test whose fake
/// closes the first lock cannot tell whether the second one is there.
class FakeTable final : public core::ITable {
 public:
  FakeTable(std::vector<std::string_view> columns, std::vector<std::vector<std::string_view>> rows)
      : columns_(std::move(columns)), rows_(std::move(rows)) {}

  std::uint32_t RowCount() const override { return static_cast<std::uint32_t>(rows_.size()); }

  std::uint32_t ColumnCount() const override { return static_cast<std::uint32_t>(columns_.size()); }

  std::uint32_t FindColumn(std::string_view name) const override {
    for (std::uint32_t index = 0; index < columns_.size(); ++index) {
      if (columns_[index] == name) {
        return index;
      }
    }
    return core::kNoTableColumn;
  }

  std::uint32_t FindRowByKey(std::string_view key) const override {
    for (std::uint32_t row = 0; row < rows_.size(); ++row) {
      if (!rows_[row].empty() && rows_[row][0] == key) {
        return row;
      }
    }
    return core::kNoTableRow;
  }

  std::string_view CellText(std::uint32_t row, std::uint32_t column) const override {
    if (row >= rows_.size() || column >= rows_[row].size()) {
      return {};
    }
    return rows_[row][column];
  }

  std::optional<std::int64_t> CellInteger(std::uint32_t row, std::uint32_t column) const override {
    const std::optional<float> value = CellReal(row, column);
    return value ? std::optional<std::int64_t>(static_cast<std::int64_t>(*value)) : std::nullopt;
  }

  std::optional<float> CellReal(std::uint32_t row, std::uint32_t column) const override {
    const std::string_view text = CellText(row, column);
    if (text.empty()) {
      return std::nullopt;
    }
    float value = 0.0F;
    const char* const begin = text.data();
    const auto result = std::from_chars(begin, begin + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != begin + text.size()) {
      return std::nullopt;
    }
    return value;
  }

 private:
  std::vector<std::string_view> columns_;

  std::vector<std::vector<std::string_view>> rows_;
};

/// life.csv as a key/value table, with one of every kind of trouble in it.
FakeTable KnobTable() {
  return FakeTable({"key", "value"},
                   {{"good", "4"},
                    {"empty", ""},
                    {"words", "four"},
                    {"too_big", "1000"},
                    {"negative", "-1"},
                    {"not_a_number", "nan"},
                    {"infinite", "inf"}});
}

int TestThreeAnswers() {
  int failures = 0;
  const FakeTable table = KnobTable();
  const std::uint32_t value_column = table.FindColumn("value");
  std::string error;

  // kRead — present, a number, inside its range.
  float value = 0.0F;
  failures += Expect(core::ReadCell(table,
                                    table.FindRowByKey("good"),
                                    value_column,
                                    core::Range{.low = 0.0F, .high = 10.0F},
                                    value,
                                    error) == core::CellState::kRead,
                     "a good cell reads");
  failures += Expect(value == 4.0F, "and gives its value");

  // kAbsent — and the caller's value is LEFT ALONE. This is the answer that
  // did not exist before: an empty cell used to fall into zero, which is
  // how an empty has_wear came to mean "does not wear" (task A5's caveat).
  value = 7.0F;
  failures += Expect(core::ReadCell(table,
                                    table.FindRowByKey("empty"),
                                    value_column,
                                    core::Range{.low = 0.0F, .high = 10.0F},
                                    value,
                                    error) == core::CellState::kAbsent,
                     "an empty cell in a present column is ABSENT, not zero");
  failures += Expect(value == 7.0F, "and leaves the caller's value untouched");
  value = 7.0F;
  failures += Expect(core::ReadCell(table,
                                    core::kNoTableRow,
                                    value_column,
                                    core::Range{.low = 0.0F, .high = 10.0F},
                                    value,
                                    error) == core::CellState::kAbsent,
                     "so is a row that is not there");
  failures += Expect(
      core::ReadCell(
          table, 0, core::kNoTableColumn, core::Range{.low = 0.0F, .high = 10.0F}, value, error) ==
          core::CellState::kAbsent,
      "and a column that is not there");
  failures += Expect(value == 7.0F, "none of which touch the value either");

  // kBad — present and wrong, in each of the four ways a cell can be wrong.
  value = 7.0F;
  const core::Range narrow{.low = 0.0F, .high = 10.0F};
  failures += Expect(
      core::ReadCell(table, table.FindRowByKey("words"), value_column, narrow, value, error) ==
          core::CellState::kBad,
      "text where a number belongs is a refusal");
  failures += Expect(
      core::ReadCell(table, table.FindRowByKey("too_big"), value_column, narrow, value, error) ==
          core::CellState::kBad,
      "so is a number above the range");
  failures += Expect(
      core::ReadCell(table, table.FindRowByKey("negative"), value_column, narrow, value, error) ==
          core::CellState::kBad,
      "and one below it");
  failures += Expect(value == 7.0F, "and a refused cell never writes the value");
  return failures;
}

/// THE DEFECT THE MODULE WAS BUILT FOR (UB-001/002, reopened by four
/// delivery cycles): `nan` and `inf` reach a float-to-int cast. They are
/// refused by the RANGE, and the range test is written positively for
/// exactly this reason — NaN compares false against everything, so a
/// negated test would have let it through.
int TestNotANumber() {
  int failures = 0;
  const FakeTable table = KnobTable();
  const std::uint32_t value_column = table.FindColumn("value");
  std::string error;
  float value = 7.0F;

  failures += Expect(table.CellReal(table.FindRowByKey("not_a_number"), value_column).has_value(),
                     "the fake reader hands NaN through, as from_chars does");
  for (const char* key : {"not_a_number", "infinite"}) {
    failures += Expect(
        core::ReadCell(
            table, table.FindRowByKey(key), value_column, core::Range::Any(), value, error) ==
            core::CellState::kBad,
        "and the range refuses it, even a range that bounds nothing");
  }
  failures += Expect(value == 7.0F, "and nothing reaches the caller's float");

  // Range::Any() is not "no check": it still has ends, and they are the
  // sentence in the code that says a knob has no bounds worth naming.
  failures += Expect(core::Range::Any().high == core::Range::kUnbounded &&
                         core::Range::Any().low == -core::Range::kUnbounded,
                     "Any() is a stated range and not an absent one");
  return failures;
}

/// The four entry points differ in ONE thing each, and the difference is
/// the reason all four exist.
int TestEntryPoints() {
  int failures = 0;
  const FakeTable table = KnobTable();
  const core::Range narrow{.low = 0.0F, .high = 10.0F};
  std::string error;

  // CellOrDefault writes a named fallback when the cell is absent...
  float value = 7.0F;
  failures += Expect(core::CellOrDefault(table,
                                         table.FindRowByKey("empty"),
                                         table.FindColumn("value"),
                                         narrow,
                                         3.0F,
                                         value,
                                         error),
                     "an absent cell is not an error for CellOrDefault");
  failures += Expect(value == 3.0F, "and the fallback lands");

  // ...while OptionalCell keeps whatever the caller already had.
  value = 7.0F;
  failures += Expect(
      core::OptionalCell(
          table, table.FindRowByKey("empty"), table.FindColumn("value"), narrow, value, error),
      "an absent cell is not an error for OptionalCell either");
  failures += Expect(value == 7.0F, "but it keeps the caller's value, and that is the difference");

  // RequiredValue refuses an absent key, and says which table and key.
  value = 7.0F;
  failures += Expect(!core::RequiredValue(table, "life", "no_such_key", narrow, value, error),
                     "a required key that is not there refuses");
  failures += Expect(
      error.find("life") != std::string::npos && error.find("no_such_key") != std::string::npos,
      "and the message names the table and the key");
  failures += Expect(core::RequiredValue(table, "life", "good", narrow, value, error),
                     "a required key that is there reads");

  // OptionalValue does not mind a missing key at all.
  failures += Expect(core::OptionalValue(table, "no_such_key", narrow, value, error),
                     "an optional key that is not there is silence, not a refusal");

  // ReadKnobs names the offender rather than the run.
  float first = 0.0F;
  float second = 0.0F;
  const core::ScalarKnob knobs[] = {
      {.key = "good", .value = &first, .range = narrow},
      {.key = "too_big", .value = &second, .range = narrow},
  };
  failures += Expect(!core::ReadKnobs(table, "life", knobs, error), "a bad knob refuses the run");
  failures += Expect(error.find("too_big") != std::string::npos,
                     "and the message names the knob that was wrong, not the first one");
  failures += Expect(first == 4.0F, "the knobs before it were already read");
  return failures;
}

}  // namespace

int main() {
  int failures = 0;
  failures += TestThreeAnswers();
  failures += TestNotANumber();
  failures += TestEntryPoints();
  if (failures == 0) {
    std::cout << "unit_core_catalog: all checks passed\n";
  }
  return failures;
}
