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
#include "core_catalog/district_visit_catalog.h"
#include "core_catalog/limit_catalog.h"
#include "core_catalog/road_rules_catalog.h"
#include "core_catalog/table_lookup.h"
#include "core_catalog/table_value.h"
#include "core_catalog/world_conventions.h"
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

/// FOUR ANSWERS TO A NAME, and three of them used to arrive as one empty id.
///
/// The lookup this exercises replaced four hand-written copies (crops in two
/// modules, the feed links, and a second ResourceByKey). Every one of them
/// answered "empty id" to a misspelt key, and an empty id is refused later
/// by AddToStock — silently, and rightly, because an unnamed resource has no
/// column to go in. The load was then counted as delivered anyway.
///
/// The three cases that must stay apart: NOBODY TO ASK (no roster at all —
/// a stub table set, and legal), NOTHING ASKED (an empty cell), and A NAME
/// THAT IS NOT THERE (a typo, and the only one that is an accusation).
int TestLookupKey() {
  int failures = 0;
  // hay first, rye second, so the resolved row is 1 and not 0: a zero would
  // pass the checks below by looking like an untouched default.
  const test::FakeTable roster({"key"}, {{"hay"}, {"rye"}});
  const test::FakeTable crops({"key", "resource"},
                              {{"rye_winter", "rye"},
                               {"clover", "hey"},  // the typo
                               {"fallow", ""}});   // named nothing
  const std::uint32_t column = crops.FindColumn("resource");
  std::string error;

  std::uint32_t row_index = core::kNoTableRow;
  failures +=
      Expect(core::LookupRow(&roster, crops, 0, column, row_index) == core::KeyState::kFound &&
                 row_index == 1,
             "a key the roster carries resolves to its row");
  failures +=
      Expect(core::LookupRow(&roster, crops, 1, column, row_index) == core::KeyState::kNotFound,
             "a key the roster does not carry is NOT FOUND, not absent");
  failures +=
      Expect(core::LookupRow(&roster, crops, 2, column, row_index) == core::KeyState::kEmpty,
             "an empty cell named nothing, which is a different thing");
  failures +=
      Expect(core::LookupRow(nullptr, crops, 1, column, row_index) == core::KeyState::kNoRoster,
             "and with no roster at all there is nobody to ask");
  failures += Expect(row_index == 1,
                     "a failed lookup leaves the caller's value alone, so a default survives");
  failures += Expect(!core::IsAbsent(core::KeyState::kNotFound),
                     "a name that is not there is not 'absent': it is a claim that is false");

  // RequiredRow: the refusal, and what it says.
  row_index = core::kNoTableRow;
  failures +=
      Expect(!core::RequiredRow(&roster, crops, 1, column, "resource", false, row_index, error),
             "RequiredRow refuses a name the roster does not have");
  failures += Expect(error.find("hey") != std::string::npos,
                     "and the message quotes the key AS WRITTEN, which is the part a person fixes");
  failures += Expect(error.find("row 2") != std::string::npos,
                     "and counts the row as the file does, data lines from one");

  failures +=
      Expect(!core::RequiredRow(&roster, crops, 2, column, "resource", false, row_index, error),
             "an empty cell is refused when the column is not optional");
  failures +=
      Expect(core::RequiredRow(&roster, crops, 2, column, "resource", true, row_index, error),
             "and allowed when it is");

  // THE ONE THAT KEEPS STUB TABLE SETS LOADING. A null roster passes every
  // time: refusing here would break the thing stubs exist for, and it is the
  // case a two-state rule would have merged with the typo.
  failures +=
      Expect(core::RequiredRow(nullptr, crops, 1, column, "resource", false, row_index, error),
             "no roster is not a refusal: there was nobody to ask");

  core::ResourceId id{};
  failures +=
      Expect(core::RequiredResource(&roster, crops, 0, column, "resource", false, id, error) &&
                 id.value == 1,
             "and the resource-shaped call hands back the dense id");
  return failures;
}

}  // namespace

/// THE MTS COLUMN'S LOT KIND (boss, parcel 235): `service` is read, and a
/// misspelt kind still refuses the catalogue.
int TestServiceLotKind() {
  int failures = 0;
  const test::FakeTable resources({"key", "kg_per_unit"}, {{"glass", "1"}});
  const test::FakeTable goods({"lot", "resource", "amount"}, {});
  const auto parse = [&](const char* kind, core::LimitCatalog& catalog) {
    const test::FakeTable lots({"key", "points", "era", "kind"},
                               {{"mts_column_spring", "120", "1", kind}});
    const test::FakeTableSet set(
        {{"resources", &resources}, {"limit_catalog", &lots}, {"limit_lot_goods", &goods}});
    std::string error;
    return core::ParseLimitCatalog(set, catalog, error);
  };
  core::LimitCatalog read;
  failures += Expect(parse("service", read) && read.lots.size() == 1 &&
                         read.lots[0].kind == core::LimitLotKind::kService,
                     "a service lot is read as a service");
  core::LimitCatalog misspelt;
  failures +=
      Expect(!parse("servise", misspelt), "and a misspelt kind still refuses the catalogue");
  return failures;
}

/// THE OVERFULFILMENT SCALE (district §1; boss seq 70, 2026-09-18): the six
/// knobs are read out of world_params.csv, and the scale prices tonnes tier
/// by tier with no cap.
int TestTheOverfulfilScale() {
  int failures = 0;
  const test::FakeTable world({"key", "value", "reader"},
                              {{"limit_overfulfil_tier1_t", "4", "core"},
                               {"limit_overfulfil_tier2_t", "10", "core"},
                               {"limit_overfulfil_tier1_points_per_t", "30", "core"},
                               {"limit_overfulfil_tier2_points_per_t", "12", "core"},
                               {"limit_overfulfil_tier3_points_per_t", "2", "core"},
                               {"limit_overfulfil_grain_kcal_per_gram", "3.5", "core"},
                               {"limit_overfulfil_shortfall_factor", "2.5", "core"}});
  const test::FakeTableSet set({{"world_params", &world}});
  core::LimitCatalog read;
  std::string error;
  failures += Expect(core::ParseLimitCatalog(set, read, error), "overfulfilment: the scale reads");
  failures += Expect(
      read.overfulfil_tier1_t == 4.0F && read.overfulfil_tier2_t == 10.0F &&
          read.overfulfil_points_per_t[0] == 30.0F && read.overfulfil_points_per_t[1] == 12.0F &&
          read.overfulfil_points_per_t[2] == 2.0F && read.overfulfil_grain_kcal_per_gram == 3.5F &&
          read.overfulfil_shortfall_factor == 2.5F,
      "overfulfilment: every knob of the scale comes from its own row");
  // 20 t: 4 at 30, 10 at 12, 6 at 2 — 120 + 120 + 12.
  failures += Expect(core::OverfulfilPoints(read, 20.0F) == 252.0F &&
                         core::OverfulfilPoints(read, 0.0F) == 0.0F &&
                         core::OverfulfilPoints(read, 2.0F) == 60.0F,
                     "overfulfilment: tonnes are priced tier by tier, and no cap stops them");
  return failures;
}

/// THE MTS COLUMN IN THE CATALOGUE (boss, parcels 449, 451): its two lots by
/// key, the hectares and the windows out of world_params.csv with the months
/// taken from human 1..12 to Month's 0..11; a column row that is not a service
/// and a window that ends before it begins refuse the catalogue.
int TestTheMtsColumnKnobs() {
  int failures = 0;
  const test::FakeTable resources({"key", "kg_per_unit"}, {{"glass", "1"}});
  const test::FakeTable goods({"lot", "resource", "amount"}, {});
  const auto parse =
      [&](const char* autumn_kind, const char* spring_to, core::LimitCatalog& catalog) {
        const test::FakeTable lots({"key", "points", "era", "kind"},
                                   {{"glass", "25", "1", "goods"},
                                    {"mts_column_spring", "120", "1", "service"},
                                    {"mts_column_autumn", "150", "1", autumn_kind}});
        const test::FakeTable world({"key", "value", "reader"},
                                    {{"mts_column_ha_limit", "60", "core"},
                                     {"mts_column_ha_per_work_day", "12", "core"},
                                     {"mts_column_spring_from_month", "3", "core"},
                                     {"mts_column_spring_to_month", spring_to, "core"},
                                     {"mts_column_autumn_from_month", "8", "core"},
                                     {"mts_column_autumn_to_month", "11", "core"}});
        const test::FakeTableSet set({{"resources", &resources},
                                      {"limit_catalog", &lots},
                                      {"limit_lot_goods", &goods},
                                      {"world_params", &world}});
        std::string error;
        return core::ParseLimitCatalog(set, catalog, error);
      };
  core::LimitCatalog read;
  failures += Expect(parse("service", "5", read) && read.mts_spring_lot.value == 1 &&
                         read.mts_autumn_lot.value == 2,
                     "mts: the spring and autumn lots are found by their keys");
  failures += Expect(read.mts_column_ha_limit == 60.0F && read.mts_column_ha_per_work_day == 12.0F,
                     "mts: the hectares are read from world_params");
  failures += Expect(read.mts_spring_from_month == 2 && read.mts_spring_to_month == 4 &&
                         read.mts_autumn_from_month == 7 && read.mts_autumn_to_month == 10,
                     "mts: the months come in human and are kept 0-based");
  core::LimitCatalog goods_column;
  failures += Expect(!parse("goods", "5", goods_column),
                     "mts: a column row that is not a service refuses the catalogue");
  core::LimitCatalog reversed;
  failures += Expect(!parse("service", "2", reversed),
                     "mts: a spring window ending before it begins refuses the catalogue");
  return failures;
}

/// THE LIVESTOCK WINDOW'S DATA (boss, parcel 5 of the resume thread): what a
/// livestock lot brings, and the one asymmetry in how the cells refuse.
///
/// AN EMPTY head_count MUST NOT REFUSE THE PARSE, and that is the check worth
/// having twice over. The tables ship with two such lots today — the piglet
/// and chick batches, whose size is balance work nobody has done — so a rule
/// that refused them would take the WHOLE district catalogue down at every
/// load: no limit points, no glass, no timber, no MTS column, over an unwritten
/// crate of chicks. Everything else about the row refuses, because everything
/// else that is wrong there is a typo and not an absence.
int TestLivestockLots() {
  int failures = 0;
  const test::FakeTable resources({"key", "kg_per_unit"}, {{"glass", "1"}});
  const test::FakeTable goods({"lot", "resource", "amount"}, {});
  const test::FakeTable kinds({"key"}, {{"cow"}, {"horse"}, {"chicken"}});
  const test::FakeTable lots(
      {"key", "points", "era", "kind"},
      {{"horse_head", "70", "1", "livestock"}, {"chick_lot", "10", "1", "livestock"}});
  const auto parse = [&](const char* lot,
                         const char* kind,
                         const char* head,
                         const char* stage,
                         const char* sex,
                         core::LimitCatalog& catalog) {
    const test::FakeTable stock(
        {"lot", "livestock", "head_count", "arrives_stage", "sex_choice"},
        {{"horse_head", "horse", "1", "adult_start", "1"}, {lot, kind, head, stage, sex}});
    const test::FakeTableSet set({{"resources", &resources},
                                  {"limit_catalog", &lots},
                                  {"limit_lot_goods", &goods},
                                  {"livestock", &kinds},
                                  {"limit_lot_livestock", &stock}});
    std::string error;
    return core::ParseLimitCatalog(set, catalog, error);
  };
  core::LimitCatalog read;
  failures += Expect(parse("chick_lot", "chicken", "", "young", "0", read) &&
                         read.lots[0].livestock.value == 1 && read.lots[0].head_count == 1 &&
                         read.lots[0].arrives_stage == core::LivestockArrivalStage::kAdultStart &&
                         read.lots[0].sex_choice,
                     "a horse lot brings one grown head and the order names its sex");
  failures += Expect(read.lots[1].livestock.value == 2 &&
                         read.lots[1].arrives_stage == core::LivestockArrivalStage::kYoung &&
                         !read.lots[1].sex_choice,
                     "poultry comes young and mixed");
  // THE POINT OF THE WHOLE CHECK: the empty cell is read as "not written yet",
  // the rest of the row IS read, and the count stays nil — which is what makes
  // the lot unorderable later without taking anything else down with it.
  failures += Expect(read.lots[1].head_count == 0,
                     "and an empty head_count leaves the count nil instead of refusing the load");
  core::LimitCatalog no_lot;
  failures += Expect(!parse("chick_bundle", "chicken", "6", "young", "0", no_lot),
                     "a row naming a lot the catalogue has not got refuses");
  core::LimitCatalog no_kind;
  failures += Expect(!parse("chick_lot", "duck", "6", "young", "0", no_kind),
                     "a row naming a livestock kind the tables have not got refuses");
  core::LimitCatalog bad_stage;
  failures += Expect(!parse("chick_lot", "chicken", "6", "adult", "0", bad_stage),
                     "arrives_stage is 'adult_start' or 'young' and nothing else");
  core::LimitCatalog bad_sex;
  failures +=
      Expect(!parse("chick_lot", "chicken", "6", "young", "2", bad_sex), "sex_choice is 0 or 1");
  core::LimitCatalog bad_head;
  failures += Expect(!parse("chick_lot", "chicken", "0", "young", "0", bad_head),
                     "a head_count that is there and is not a head refuses");
  return failures;
}

/// The biology factor's one door (core_catalog/world_conventions.h): read
/// from whichever home carries it, refused when the two disagree or the
/// value is out of range, empty when neither has the row.
int TestLifeSpeedupDoor() {
  int failures = 0;
  const auto find = [](const char* in_world, const char* in_life, std::optional<float>& value) {
    std::vector<std::vector<std::string>> world_rows = {{"leaf_fall_month", "10", "core"}};
    if (in_world != nullptr) {
      world_rows.push_back({"life_speedup", in_world, "core"});
    }
    std::vector<std::vector<std::string>> life_rows = {{"adult_age_years", "16"}};
    if (in_life != nullptr) {
      life_rows.push_back({"life_speedup", in_life});
    }
    const test::FakeTable world({"key", "value", "reader"}, world_rows);
    const test::FakeTable life({"key", "value"}, life_rows);
    const test::FakeTableSet set({{"world_params", &world}, {"life", &life}});
    std::string error;
    return core::FindLifeSpeedup(set, value, error);
  };
  std::optional<float> only_life;
  std::optional<float> only_world;
  std::optional<float> both;
  std::optional<float> neither;
  failures += Expect(find(nullptr, "4", only_life) && only_life == 4.0F,
                     "life_speedup: life.csv alone still answers, until it becomes an export");
  failures += Expect(find("6", nullptr, only_world) && only_world == 6.0F,
                     "life_speedup: world_params alone answers — its new home");
  failures +=
      Expect(find("4", "4", both) && both == 4.0F, "life_speedup: both homes agreeing answer");
  failures += Expect(find(nullptr, nullptr, neither) && !neither.has_value(),
                     "life_speedup: neither home — no answer, and no error; the caller decides");
  std::optional<float> disagree;
  std::optional<float> zero;
  failures += Expect(!find("3", "4", disagree) && !disagree.has_value(),
                     "life_speedup: two homes that disagree are refused, not one of them chosen");
  failures += Expect(!find("0", nullptr, zero) && !zero.has_value(),
                     "life_speedup: a factor of zero is refused — the clock divides by it");
  const test::FakeTable empty({"key", "value", "reader"}, {});
  const test::FakeTableSet bare({{"world_params", &empty}});
  failures += Expect(core::LifeSpeedupOr(bare, 2.5F) == 2.5F,
                     "life_speedup: a reader that cannot report keeps its own fallback");
  const test::FakeTable split_world({"key", "value", "reader"}, {{"life_speedup", "3", "core"}});
  const test::FakeTable split_life({"key", "value"}, {{"life_speedup", "4"}});
  const test::FakeTableSet split({{"world_params", &split_world}, {"life", &split_life}});
  failures += Expect(core::LifeSpeedupOr(split, 2.5F) == 2.5F,
                     "life_speedup: and on a refused set too — neither home's number is taken");
  return failures;
}

/// The roads' numbers (delivery 3): read into their fields, refused out of
/// range, and the falling order of the traffic thresholds held.
int TestRoadRules() {
  int failures = 0;
  const auto parse = [](std::vector<std::vector<std::string>> rows, core::RoadRules& rules) {
    const test::FakeTable world({"key", "value", "reader"}, std::move(rows));
    const test::FakeTableSet set({{"world_params", &world}});
    std::string error;
    return core::ParseRoadRules(set, rules, error);
  };
  core::RoadRules read;
  failures += Expect(parse({{"road_dry_days_dirt", "2", "core"},
                            {"road_wet_factor_gravel", "0.9", "core"},
                            {"road_strip_decay_pct_per_day", "0.5", "core"}},
                           read) &&
                         read.dry_days[0] == 2.0F && read.wet_factor[1] == 0.9F &&
                         read.strip_decay_pct_per_day == 0.5F && read.frozen_factor == 1.15F,
                     "road rules: rows land in their fields; an absent row keeps the default");
  core::RoadRules slow_frost;
  core::RoadRules upside_down;
  failures += Expect(!parse({{"road_frozen_factor", "0.9", "core"}}, slow_frost),
                     "road rules: a frozen bed slower than a dry one is refused (min_ok 1)");
  failures += Expect(!parse({{"road_traffic_rare_from", "5", "core"}}, upside_down),
                     "road rules: a 'rarely' threshold above 'regularly' is refused");
  failures += Expect(core::RoadWorldParamKeys().size() == 29,
                     "road rules: the core knows all 29 road_ keys of the export");
  return failures;
}

/// The district's regular visits (boss, parcel 324): the three knobs read, and
/// a month outside the year, a half month and a notice longer than a month
/// refuse the catalogue.
int TestDistrictVisitKnobs() {
  int failures = 0;
  const auto parse = [](const char* karasev,
                        const char* polushkina,
                        const char* notice,
                        core::DistrictVisitCatalog& catalog) {
    const test::FakeTable world({"key", "value", "reader"},
                                {{"district_visit_karasev_month", karasev, "core"},
                                 {"district_visit_polushkina_month", polushkina, "core"},
                                 {"district_visit_notice_days", notice, "core"}});
    const test::FakeTableSet set({{"world_params", &world}});
    std::string error;
    return core::ParseDistrictVisitCatalog(set, catalog, error);
  };
  core::DistrictVisitCatalog read;
  failures += Expect(parse("7", "11", "3", read) && read.karasev_month == 7 &&
                         read.polushkina_month == 11 && read.notice_days == 3,
                     "district visits: the months and the notice come off world_params");
  core::DistrictVisitCatalog thirteenth;
  core::DistrictVisitCatalog half;
  core::DistrictVisitCatalog long_notice;
  failures += Expect(!parse("13", "12", "2", thirteenth) && !parse("6", "6.5", "2", half) &&
                         !parse("6", "12", "5", long_notice),
                     "district visits: a thirteenth month, half a month and a notice past a "
                     "month are refused");
  return failures;
}

int main() {
  int failures = 0;
  failures += TestLifeSpeedupDoor();
  failures += TestRoadRules();
  failures += TestDistrictVisitKnobs();
  failures += TestServiceLotKind();
  failures += TestTheMtsColumnKnobs();
  failures += TestTheOverfulfilScale();
  failures += TestLivestockLots();
  failures += TestThreeAnswers();
  failures += TestRequiredCell();
  failures += TestNotANumber();
  failures += TestEntryPoints();
  failures += TestLookupKey();
  if (failures == 0) {
    std::cout << "unit_core_catalog: all checks passed\n";
  }
  return failures;
}
