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

#include "../../common/fake_tables.h"
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

/// A table whose CellReal LETS `nan` and `inf` THROUGH, unlike the shipped
/// reader and unlike the shared fake in tests/common/fake_tables.h.
///
/// That is this test's SUBJECT, not a shortcut. csv_table_set.cpp refuses
/// non-finite values at the door, and the range test here is the second lock
/// on that door; a fake that closes the first lock cannot tell whether the
/// second one is there at all. So the leak is deliberate, local, and says so.
class LeakyTable final : public core::ITable {
 public:
  explicit LeakyTable(test::FakeTable table) : table_(std::move(table)) {}

  std::uint32_t RowCount() const override { return table_.RowCount(); }

  std::uint32_t ColumnCount() const override { return table_.ColumnCount(); }

  std::uint32_t FindColumn(std::string_view name) const override { return table_.FindColumn(name); }

  std::uint32_t FindRowByKey(std::string_view key) const override {
    return table_.FindRowByKey(key);
  }

  std::string_view CellText(std::uint32_t row, std::uint32_t column) const override {
    return table_.CellText(row, column);
  }

  std::optional<std::int64_t> CellInteger(std::uint32_t row, std::uint32_t column) const override {
    return table_.CellInteger(row, column);
  }

  /// `std::from_chars` and nothing else — no isfinite, on purpose.
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
  test::FakeTable table_;
};

/// life.csv as a key/value table, with one of every kind of trouble in it.
LeakyTable KnobTable() {
  return LeakyTable(test::FakeTable({"key", "value"},
                                    {{"good", "4"},
                                     {"empty", ""},
                                     {"words", "four"},
                                     {"too_big", "1000"},
                                     {"negative", "-1"},
                                     {"not_a_number", "nan"},
                                     {"infinite", "inf"}}));
}

int TestThreeAnswers() {
  int failures = 0;
  const LeakyTable table = KnobTable();
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

  // три вида «ничего» — and the caller's value is LEFT ALONE. This is the answer that
  // did not exist before: an empty cell used to fall into zero, which is
  // how an empty has_wear came to mean "does not wear" (task A5's caveat).
  value = 7.0F;
  failures += Expect(core::ReadCell(table,
                                    table.FindRowByKey("empty"),
                                    value_column,
                                    core::Range{.low = 0.0F, .high = 10.0F},
                                    value,
                                    error) == core::CellState::kEmpty,
                     "an empty cell in a PRESENT column has its own answer — not zero, and not "
                     "the same answer as a column nobody wrote at all");
  failures += Expect(value == 7.0F, "and leaves the caller's value untouched");
  value = 7.0F;
  failures += Expect(core::ReadCell(table,
                                    core::kNoTableRow,
                                    value_column,
                                    core::Range{.low = 0.0F, .high = 10.0F},
                                    value,
                                    error) == core::CellState::kNoRow,
                     "a row that is not there is its own answer too");
  failures += Expect(
      core::ReadCell(
          table, 0, core::kNoTableColumn, core::Range{.low = 0.0F, .high = 10.0F}, value, error) ==
          core::CellState::kNoColumn,
      "and so is a column that is not there");
  failures += Expect(value == 7.0F, "none of which touch the value either");

  // AND THE THREE MUST NOT COLLAPSE BACK. Asserting each against its own
  // enumerator would pass just as well if two of them shared a value, so the
  // separation is asserted directly: this is the whole subject of task A6,
  // and it is the one thing a test of "the states are distinct" has to say.
  const auto state_of = [&](std::uint32_t row, std::uint32_t column) {
    float ignored = 0.0F;
    std::string unused;
    return core::ReadCell(
        table, row, column, core::Range{.low = 0.0F, .high = 10.0F}, ignored, unused);
  };
  const core::CellState empty_cell = state_of(table.FindRowByKey("empty"), value_column);
  const core::CellState no_row = state_of(core::kNoTableRow, value_column);
  const core::CellState no_column = state_of(0, core::kNoTableColumn);
  failures += Expect(empty_cell != no_row && empty_cell != no_column && no_row != no_column,
                     "the three kinds of nothing are three values, not one — which is the whole "
                     "of what task A6 changed");
  failures +=
      Expect(core::IsAbsent(empty_cell) && core::IsAbsent(no_row) && core::IsAbsent(no_column),
             "and a caller that truly does not care can still say so in one word");

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
/// A COLUMN THAT IS THERE MUST ANSWER FOR EVERY ROW — and the refusal names
/// the cell rather than the rule.
///
/// This is the shape that was missing until task A6. The three wrappers that
/// existed all answered "what do I do when there is no value": keep the
/// caller's, write a fallback, hand the question back. None could say THERE
/// MUST BE ONE, so a column meant to be filled for every row had no way to
/// say so — and a hole in an export read as a number, silently, which is the
/// one outcome that costs nothing to notice.
///
/// Each of the four cases is asserted separately BECAUSE THE SUBJECT IS THE
/// DIFFERENCE BETWEEN THEM. A test that only checked the refusal would pass
/// against a rule that refused everything, and a test that only checked the
/// good cell would pass against a rule that refused nothing.
int TestRequiredCell() {
  int failures = 0;
  const test::FakeTable table{{"key", "value"},
                              {{"good", "3"}, {"empty", ""}, {"wrong", "x"}, {"big", "99"}}};
  const std::uint32_t value_column = table.FindColumn("value");
  const core::Range band{.low = 0.0F, .high = 10.0F};
  const auto required =
      [&](std::string_view key, std::uint32_t column, float& out, std::string& error) {
        return core::RequiredCell(
            table, "fake", "value", table.FindRowByKey(key), column, band, out, error);
      };

  float value = 7.0F;
  std::string error;
  failures += Expect(required("good", value_column, value, error) && value == 3.0F,
                     "a filled cell inside its band is read, and the refusal does not fire on it");

  value = 7.0F;
  error.clear();
  failures += Expect(!required("empty", value_column, value, error),
                     "an EMPTY cell in a present column is REFUSED — this is the whole point");
  failures += Expect(value == 7.0F, "and a refused cell leaves the caller's value alone");
  // The message is for somebody looking at a spreadsheet, so it has to name
  // the cell. "A required value is missing" would be true and useless.
  failures +=
      Expect(error.find("fake") != std::string::npos && error.find("value") != std::string::npos &&
                 error.find("row 1") != std::string::npos,
             "and the refusal names the table, the column and the row");

  value = 7.0F;
  error.clear();
  failures += Expect(!required("wrong", value_column, value, error),
                     "a cell that is not a number is still refused, as it always was");
  error.clear();
  failures +=
      Expect(!required("big", value_column, value, error), "and so is one outside its band");

  // A ROW THAT IS NOT THERE IS REFUSED, and the first draft of this wrapper
  // let it pass in silence. Two ways to have no row, and BOTH are asserted:
  // the sentinel a lookup returns, and an index simply past the end — the
  // second used to fall through into kEmpty, because CellText answers an
  // out-of-range index with the same empty view it gives a blank cell, so
  // the refusal would have named a row number that does not exist.
  value = 7.0F;
  error.clear();
  failures +=
      Expect(!core::RequiredCell(
                 table, "fake", "value", core::kNoTableRow, value_column, band, value, error),
             "a row named by the no-such-row sentinel is refused");
  error.clear();
  failures +=
      Expect(!core::RequiredCell(
                 table, "fake", "value", table.RowCount() + 5, value_column, band, value, error),
             "and so is an index past the end — which is not a blank cell, however much "
             "the table's answer looks like one");
  failures += Expect(error.find("no such row") != std::string::npos,
                     "and it is refused AS a missing row, not as an empty cell");
  failures += Expect(value == 7.0F, "neither touches the caller's value");

  // A MISSING COLUMN IS NOT A REFUSAL, and that is deliberate rather than an
  // oversight: a column absent for every row is a table this build was not
  // given, which is stub_tables.h's conversation, not this one. Asserting it
  // here is what keeps the two apart — without this line the wrapper could
  // quietly grow into a second answer to "were we given the tables".
  value = 7.0F;
  error.clear();
  failures += Expect(required("good", core::kNoTableColumn, value, error),
                     "a column that is not there at all is NOT a refusal — that is a different "
                     "question, asked elsewhere");
  failures += Expect(value == 7.0F, "and it leaves the value alone too");
  return failures;
}

int TestNotANumber() {
  int failures = 0;
  const LeakyTable table = KnobTable();
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
  const LeakyTable table = KnobTable();
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
  failures += TestRequiredCell();
  failures += TestNotANumber();
  failures += TestEntryPoints();
  if (failures == 0) {
    std::cout << "unit_core_catalog: all checks passed\n";
  }
  return failures;
}
