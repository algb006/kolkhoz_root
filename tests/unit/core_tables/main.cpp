// Unit test of core_tables: the reading contract and the O3 CSV loader.
// Builds a temporary tables directory, loads it, and checks the dialect of
// manual/61-balance-tables.md: comments, quoting, empty cells, the key
// column, unknown columns, whole-or-nothing failures.

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include "core_tables/tables.h"

static_assert(std::is_abstract_v<core::ITable>, "ITable is a contract");
static_assert(std::is_abstract_v<core::ITableSet>, "ITableSet is a contract");
static_assert(std::has_virtual_destructor_v<core::ITableSet>,
              "table sets are destroyed through the interface");
static_assert(core::kNoTableRow == 0xFFFFFFFFu, "the no-row sentinel is pinned");
static_assert(core::kNoTableColumn == 0xFFFFFFFFu, "the no-column sentinel is pinned");

namespace {

int Expect(bool condition, const char* label) {
  if (condition) {
    return 0;
  }
  std::cout << "FAIL: " << label << '\n';
  return 1;
}

void WriteFile(const std::filesystem::path& path, std::string_view content) {
  std::ofstream file(path, std::ios::binary);
  file << content;
}

}  // namespace

int main() {
  namespace fs = std::filesystem;
  int failures = 0;
  const fs::path root = fs::temp_directory_path() / "unit_core_tables";
  fs::remove_all(root);
  fs::create_directories(root / "good");

  WriteFile(root / "good" / "crops.csv",
            "key,title,yield_kg_per_ha,ripen_days,fertility_delta,note\n"
            "# cereals\n"
            "rye_winter,\"Rye, winter\",1100,44,-1,\n"
            "wheat_spring,Wheat,900,36,-3,balancer scratch\n"
            "# legumes restore\n"
            "pea,Pea,800.5,32,2,\"has \"\"quotes\"\"\"\n");
  WriteFile(root / "good" / "constants.csv", "\xEF\xBB\xBFname,value\r\nspeed_factor,12\r\n");

  // THE COMMENT LINE IS PROSE, and prose has quotes and commas in it. Both
  // used to reach the tokenizer: the quote failed the whole file with
  // "stray quote in an unquoted cell", the comma quietly split the comment
  // into cells and was saved only by the row being thrown away afterwards.
  // A table that will not load empties the consumer's world, and it was the
  // graphics layer that found it, not us (boss, 2026-09-04).
  std::string prose_error;
  fs::create_directories(root / "prose");
  WriteFile(root / "prose" / "notes.csv",
            "key,value\n"
            "# a comment with \"quotes\" in it, and a comma too\n"
            "  # indented, and still a comment\n"
            "alpha,1\n"
            "#\n"
            "beta,2\n");
  const auto prose = core::LoadTableSet((root / "prose").string(), &prose_error);
  failures += Expect(prose != nullptr, "a comment with quotes and commas does not fail the table");
  if (prose != nullptr) {
    const core::ITable* notes = prose->FindTable("notes");
    failures += Expect(notes != nullptr && notes->RowCount() == 2,
                       "and the comments are gone while both data rows stay");
  }

  // AND THE TRAP IN THE OBVIOUS FIX. Sweeping the text for lines that begin
  // with '#' would pass the two checks above and silently eat this one: a
  // quoted cell may span lines, and a continuation line beginning with '#'
  // is DATA, not a comment. Deciding at the start of a line WITH the quote
  // state is what tells them apart.
  fs::create_directories(root / "spanning");
  WriteFile(root / "spanning" / "notes.csv",
            "key,note\n"
            "# a real comment\n"
            "alpha,\"first line\n"
            "# not a comment: this is inside the cell\n"
            "last line\"\n"
            "beta,plain\n");
  const auto spanning = core::LoadTableSet((root / "spanning").string(), &prose_error);
  failures += Expect(spanning != nullptr, "a table with a multi-line quoted cell loads");
  if (spanning != nullptr) {
    const core::ITable* notes = spanning->FindTable("notes");
    failures += Expect(notes != nullptr && notes->RowCount() == 2, "and it is two rows, not three");
    if (notes != nullptr && notes->RowCount() == 2) {
      const std::string_view cell = notes->CellText(0, notes->FindColumn("note"));
      failures += Expect(cell.find("# not a comment") != std::string_view::npos,
                         "the hash line INSIDE the quoted cell survived as data");
    }
  }

  // A good directory loads whole.
  std::string error;
  const auto set = core::LoadTableSet((root / "good").string(), &error);
  failures += Expect(set != nullptr, "a good directory loads");
  if (set != nullptr) {
    failures += Expect(set->TableCount() == 2, "both files became tables");
    failures += Expect(set->TableName(0) == "constants", "load order is file-name order");
    failures += Expect(set->FindTable("no_such") == nullptr, "a missing table is nullptr");

    const core::ITable* crops = set->FindTable("crops");
    failures += Expect(crops != nullptr, "crops.csv is found by stem");
    if (crops != nullptr) {
      failures += Expect(crops->RowCount() == 3, "comment lines are dropped from rows");
      failures += Expect(crops->ColumnCount() == 6, "all header columns counted");
      const std::uint32_t yield_column = crops->FindColumn("yield_kg_per_ha");
      failures += Expect(yield_column == 2, "columns are found by header name");
      failures += Expect(crops->FindColumn("no_such") == core::kNoTableColumn,
                         "a missing column is kNoTableColumn");

      // DefId = row index: comments dropped, so pea is row 2.
      const std::uint32_t pea_row = crops->FindRowByKey("pea");
      failures += Expect(pea_row == 2, "keys resolve to dense row indices");
      failures +=
          Expect(crops->FindRowByKey("oats") == core::kNoTableRow, "a missing key is kNoTableRow");

      failures += Expect(crops->CellText(0, 1) == "Rye, winter", "a quoted cell keeps its comma");
      failures +=
          Expect(crops->CellText(pea_row, 5) == "has \"quotes\"", "doubled quotes unescape");
      failures += Expect(crops->CellInteger(0, yield_column) == 1100, "integers parse");
      failures += Expect(!crops->CellInteger(pea_row, yield_column).has_value(),
                         "a real number is not a plain integer");
      const std::optional<float> pea_yield = crops->CellReal(pea_row, yield_column);
      failures += Expect(pea_yield.has_value() && *pea_yield > 800.4F && *pea_yield < 800.6F,
                         "reals parse with the dot separator");
      failures += Expect(!crops->CellInteger(0, 5).has_value() && crops->CellText(0, 5).empty(),
                         "an empty cell is no value");
      failures += Expect(crops->CellText(99, 0).empty() && crops->CellText(0, 99).empty(),
                         "out-of-range cells read as empty");
    }

    // BOM and CRLF from a Sheets export are tolerated.
    const core::ITable* constants = set->FindTable("constants");
    failures += Expect(constants != nullptr && constants->FindColumn("name") == 0,
                       "a BOM does not corrupt the first header");
    failures +=
        Expect(constants != nullptr && constants->CellInteger(0, 1) == 12, "CRLF rows parse");
  }

  // Non-finite cells: std::from_chars accepts "inf" and "nan" as a complete
  // match, and every consumer of a real cell eventually casts it to an
  // integer, where that is undefined behaviour. CellReal refuses them at the
  // door (tables.h; phase-2 task A6, the table-value debt). The table itself
  // still LOADS — the dialect has nothing against the text — and the refusal
  // is per cell, which is what lets a consumer fall back to its default.
  fs::create_directories(root / "wild");
  WriteFile(root / "wild" / "knobs.csv",
            "key,v\n"
            "plus_inf,inf\n"
            "minus_inf,-inf\n"
            "not_a_number,nan\n"
            "capital,INF\n"
            "huge_but_finite,1e30\n"
            "ordinary,2.5\n");
  const auto wild = core::LoadTableSet((root / "wild").string(), &error);
  failures += Expect(wild != nullptr, "a table with non-finite text still loads");
  if (wild != nullptr) {
    const core::ITable* knobs = wild->FindTable("knobs");
    failures += Expect(knobs != nullptr, "knobs.csv is found");
    if (knobs != nullptr) {
      const std::uint32_t value_column = knobs->FindColumn("v");
      const auto cell = [&](const char* key) {
        return knobs->CellReal(knobs->FindRowByKey(key), value_column);
      };
      failures += Expect(!cell("plus_inf").has_value(), "\"inf\" is not a value");
      failures += Expect(!cell("minus_inf").has_value(), "\"-inf\" is not a value");
      failures += Expect(!cell("not_a_number").has_value(), "\"nan\" is not a value");
      failures += Expect(!cell("capital").has_value(), "\"INF\" is not a value either");
      // Finite is not the same as sensible: the range stays the consumer's,
      // and CellReal hands this one over as it is.
      const std::optional<float> huge = cell("huge_but_finite");
      failures += Expect(huge.has_value() && *huge > 1e29F,
                         "a huge but finite cell is still a value — the range is the reader's");
      const std::optional<float> plain = cell("ordinary");
      failures += Expect(plain.has_value() && *plain > 2.4F && *plain < 2.6F,
                         "and an ordinary cell is untouched by all this");
      failures +=
          Expect(!knobs->CellInteger(knobs->FindRowByKey("plus_inf"), value_column).has_value(),
                 "\"inf\" was never an integer to begin with");
    }
  }

  // Whole-or-nothing: each defect fails the entire load with an error text.
  fs::create_directories(root / "dup");
  WriteFile(root / "dup" / "bad.csv", "key,v\na,1\na,2\n");
  error.clear();
  failures +=
      Expect(core::LoadTableSet((root / "dup").string(), &error) == nullptr && !error.empty(),
             "a duplicate key fails the load");

  fs::create_directories(root / "quote");
  WriteFile(root / "quote" / "bad.csv", "key,v\na,\"unclosed\n");
  failures += Expect(core::LoadTableSet((root / "quote").string(), nullptr) == nullptr,
                     "an unclosed quote fails the load");

  fs::create_directories(root / "wide");
  WriteFile(root / "wide" / "bad.csv", "key,v\na,1,2\n");
  failures += Expect(core::LoadTableSet((root / "wide").string(), nullptr) == nullptr,
                     "a row wider than the header fails the load");

  failures += Expect(core::LoadTableSet((root / "missing").string(), &error) == nullptr,
                     "a missing directory fails the load");

  // THE ROW CAP, and why it is worth a test of its own. A DefId is 16 bits
  // and 0xFFFF means "no such definition" (core_common/ids.h), so every
  // parser in the core bounds an id with `value >= vector.size()` and every
  // one of them is correct ONLY because a definition table can never hold
  // 65536 rows. That guard lives here, in one place, and nothing measured
  // it until the named cast pass went looking (task A7a).
  //
  // The boundary is the interesting number, not the middle: 65535 rows is
  // the largest table there can be, and it yields indices 0..65534 — so the
  // sentinel is not a row, which is exactly what the parsers rely on.
  fs::create_directories(root / "huge");
  {
    std::string wide_table = "key,v\n";
    wide_table.reserve(1U << 20U);
    for (std::uint32_t row = 0; row < 65536U; ++row) {
      wide_table += "k" + std::to_string(row) + ",1\n";
    }
    WriteFile(root / "huge" / "many.csv", wide_table);
  }
  failures += Expect(core::LoadTableSet((root / "huge").string(), &error) == nullptr,
                     "65536 rows fail the load: a DefId could not name the last one");

  fs::create_directories(root / "brim");
  {
    std::string brim_table = "key,v\n";
    brim_table.reserve(1U << 20U);
    for (std::uint32_t row = 0; row < 65535U; ++row) {
      brim_table += "k" + std::to_string(row) + ",1\n";
    }
    WriteFile(root / "brim" / "many.csv", brim_table);
  }
  const auto brim = core::LoadTableSet((root / "brim").string(), &error);
  const core::ITable* brim_table = brim == nullptr ? nullptr : brim->FindTable("many");
  failures += Expect(brim_table != nullptr && brim_table->RowCount() == 65535U,
                     "and 65535 rows load: the last row is 65534, the sentinel is not a row");

  fs::remove_all(root);
  if (failures == 0) {
    std::cout << "unit_core_tables: all checks passed\n";
  }
  return failures;
}
