// Implementation of core_tables (include/core_tables/tables.h, task O3):
// LoadTableSet reads every tables/*.csv into an immutable set.
//
// Dialect (manual/61-balance-tables.md): RFC 4180 — comma separator, double
// quotes with "" escapes, multi-line quoted cells; UTF-8; '.' decimal point.
// Liberal on input where Sheets exports drift from the canon: a UTF-8 BOM is
// stripped, CRLF is accepted (every '\r' outside quotes is dropped, so a
// CR-only file collapses into one row and fails loudly on shape, and a bare
// CR cannot smuggle a line break into unquoted cells), a data row shorter
// than the header reads as empty cells. A row LONGER than the header is malformed (cells that no
// column names) and fails the load. Comment lines — first cell starting
// with '#' — are dropped before rows are counted, so data row 0 is the
// first row after the header and its index is exactly the DefId value.

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core_log/log.h"
#include "core_tables/tables.h"

namespace core {
namespace {

/// Rows-of-cells of one parsed file, header excluded.
using CsvRows = std::vector<std::vector<std::string>>;

/// The 65535 cap of tables.h: DefId is uint16 with 0xFFFF reserved.
constexpr std::uint32_t kMaxDataRows = 65535;

/// @brief Splits raw CSV text into rows of cells per RFC 4180.
/// Returns false (with `error` filled) on a structural defect: an unclosed
/// quoted cell, or a stray quote inside an unquoted cell.
bool ParseCsv(std::string_view text, CsvRows& rows, std::string& error) {
  // Sheets exports often lead with a BOM; the canon has none — strip it.
  if (text.starts_with("\xEF\xBB\xBF")) {
    text.remove_prefix(3);
  }
  std::vector<std::string> row;
  std::string cell;
  bool in_quotes = false;
  bool cell_was_quoted = false;
  std::size_t position = 0;
  const std::size_t size = text.size();
  auto end_cell = [&row, &cell, &cell_was_quoted]() {
    row.push_back(std::move(cell));
    cell.clear();
    cell_was_quoted = false;
  };
  auto end_row = [&rows, &row, end_cell]() {
    end_cell();
    // A completely empty line is not a data row — Sheets pads file ends.
    if (row.size() != 1 || !row.front().empty()) {
      rows.push_back(std::move(row));
    }
    row.clear();
  };
  while (position < size) {
    const char character = text[position];
    if (in_quotes) {
      if (character == '"') {
        if (position + 1 < size && text[position + 1] == '"') {
          cell.push_back('"');
          ++position;
        } else {
          in_quotes = false;
        }
      } else {
        cell.push_back(character);
      }
      ++position;
      continue;
    }
    switch (character) {
      case '"':
        if (cell.empty() && !cell_was_quoted) {
          in_quotes = true;
          cell_was_quoted = true;
        } else {
          error = "stray quote in an unquoted cell";
          return false;
        }
        break;
      case ',':
        end_cell();
        break;
      case '\r':
        // CRLF tolerated: the '\n' that follows ends the row.
        break;
      case '\n':
        end_row();
        break;
      default:
        cell.push_back(character);
        break;
    }
    ++position;
  }
  if (in_quotes) {
    error = "unclosed quoted cell";
    return false;
  }
  // A final row without a trailing newline still counts.
  if (!cell.empty() || cell_was_quoted || !row.empty()) {
    end_row();
  }
  return true;
}

/// @brief One loaded table: header names plus dense rows of cell text.
class CsvTable final : public ITable {
 public:
  CsvTable(std::vector<std::string> header, CsvRows data_rows, std::uint32_t key_column)
      : header_(std::move(header)), rows_(std::move(data_rows)), key_column_(key_column) {
    if (key_column_ == kNoTableColumn) {
      return;
    }
    for (std::uint32_t row = 0; row < rows_.size(); ++row) {
      if (key_column_ < rows_[row].size()) {
        row_by_key_.emplace(rows_[row][key_column_], row);
      }
    }
  }

  std::uint32_t RowCount() const override { return static_cast<std::uint32_t>(rows_.size()); }

  std::uint32_t ColumnCount() const override { return static_cast<std::uint32_t>(header_.size()); }

  std::uint32_t FindColumn(std::string_view name) const override {
    for (std::uint32_t column = 0; column < header_.size(); ++column) {
      if (header_[column] == name) {
        return column;
      }
    }
    return kNoTableColumn;
  }

  std::uint32_t FindRowByKey(std::string_view key) const override {
    if (key_column_ == kNoTableColumn) {
      return kNoTableRow;
    }
    const auto found = row_by_key_.find(std::string(key));
    return found == row_by_key_.end() ? kNoTableRow : found->second;
  }

  std::string_view CellText(std::uint32_t row, std::uint32_t column) const override {
    if (row >= rows_.size() || column >= header_.size() || column >= rows_[row].size()) {
      return {};
    }
    return rows_[row][column];
  }

  std::optional<std::int64_t> CellInteger(std::uint32_t row, std::uint32_t column) const override {
    const std::string_view text = CellText(row, column);
    if (text.empty()) {
      return std::nullopt;
    }
    std::int64_t value = 0;
    const auto [parse_end, parse_error] =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (parse_error != std::errc() || parse_end != text.data() + text.size()) {
      return std::nullopt;
    }
    return value;
  }

  std::optional<float> CellReal(std::uint32_t row, std::uint32_t column) const override {
    const std::string_view text = CellText(row, column);
    if (text.empty()) {
      return std::nullopt;
    }
    float value = 0.0F;
    const auto [parse_end, parse_error] =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (parse_error != std::errc() || parse_end != text.data() + text.size()) {
      return std::nullopt;
    }
    // std::from_chars accepts "inf" and "nan" as a complete match, and every
    // consumer of a real cell eventually casts it to an integer, where a
    // non-finite value is undefined behaviour ([conv.fpint]/1). The tables
    // are exported and then hand-edited, so the refusal belongs HERE, at the
    // one door every reader comes through, and not in each reader. Written
    // positively on purpose: `!(v > lo && v < hi)` style tests are what let
    // nan through everywhere else. Phase-2 task A6, the table-value debt.
    if (!std::isfinite(value)) {
      return std::nullopt;
    }
    return value;
  }

  /// @brief Duplicate value of the `key` column, if any — checked at load.
  const std::string* FindDuplicateKey() const {
    if (key_column_ == kNoTableColumn) {
      return nullptr;
    }
    for (std::uint32_t row = 0; row < rows_.size(); ++row) {
      if (key_column_ < rows_[row].size()) {
        const auto mapped = row_by_key_.find(rows_[row][key_column_]);
        if (mapped != row_by_key_.end() && mapped->second != row) {
          return &rows_[row][key_column_];
        }
      }
    }
    return nullptr;
  }

 private:
  std::vector<std::string> header_;

  CsvRows rows_;

  std::uint32_t key_column_;

  std::unordered_map<std::string, std::uint32_t> row_by_key_;
};

class CsvTableSet final : public ITableSet {
 public:
  const ITable* FindTable(std::string_view name) const override {
    for (std::uint32_t index = 0; index < names_.size(); ++index) {
      if (names_[index] == name) {
        return tables_[index].get();
      }
    }
    return nullptr;
  }

  std::uint32_t TableCount() const override { return static_cast<std::uint32_t>(names_.size()); }

  std::string_view TableName(std::uint32_t index) const override {
    if (index >= names_.size()) {
      return {};
    }
    return names_[index];
  }

  void Add(std::string name, std::unique_ptr<CsvTable> table) {
    names_.push_back(std::move(name));
    tables_.push_back(std::move(table));
  }

 private:
  std::vector<std::string> names_;

  std::vector<std::unique_ptr<CsvTable>> tables_;
};

/// @brief Parses one file into a CsvTable; nullptr + error on any defect.
std::unique_ptr<CsvTable> LoadOneTable(const std::filesystem::path& path, std::string& error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = "cannot open the file";
    return nullptr;
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  const std::string text = buffer.str();

  CsvRows parsed;
  if (!ParseCsv(text, parsed, error)) {
    return nullptr;
  }
  // Drop comment rows: first cell starting with '#'.
  std::erase_if(parsed, [](const std::vector<std::string>& row) {
    return !row.empty() && row.front().starts_with('#');
  });
  if (parsed.empty()) {
    error = "no header row";
    return nullptr;
  }
  std::vector<std::string> header = std::move(parsed.front());
  parsed.erase(parsed.begin());
  if (parsed.size() > kMaxDataRows) {
    error = "more than 65535 data rows — DefId is 16-bit";
    return nullptr;
  }
  for (const std::vector<std::string>& row : parsed) {
    if (row.size() > header.size()) {
      error = "a data row has more cells than the header has columns";
      return nullptr;
    }
  }
  std::uint32_t key_column = kNoTableColumn;
  for (std::uint32_t column = 0; column < header.size(); ++column) {
    if (header[column] == "key") {
      key_column = column;
      break;
    }
  }
  auto table = std::make_unique<CsvTable>(std::move(header), std::move(parsed), key_column);
  if (const std::string* duplicate = table->FindDuplicateKey()) {
    error = "duplicate key '" + *duplicate + "'";
    return nullptr;
  }
  return table;
}

}  // namespace

std::unique_ptr<ITableSet> LoadTableSet(std::string_view directory, std::string* error) {
  namespace fs = std::filesystem;
  auto fail = [error](const std::string& message) -> std::unique_ptr<ITableSet> {
    LogError("tables: " + message);
    if (error != nullptr) {
      *error = message;
    }
    return nullptr;
  };

  const fs::path root{std::string(directory)};
  std::error_code fs_error;
  if (!fs::is_directory(root, fs_error)) {
    return fail("'" + root.string() + "' is not a directory");
  }

  // File-name order — the stable load order promised by TableName().
  //
  // Every filesystem call here takes an error_code. The throwing forms are
  // the trap: a range-for over directory_iterator increments with the
  // THROWING operator++, and directory_entry::is_regular_file() without an
  // error_code throws too — either would leave this function by exception,
  // past the "nullptr plus a reason in `error`" contract this whole loader
  // is written to (tables.h). A directory that vanishes mid-scan, or an
  // entry whose status cannot be read, is a load error like any other.
  std::vector<fs::path> files;
  fs::directory_iterator entry(root, fs_error);
  if (fs_error) {
    return fail("cannot list '" + root.string() + "': " + fs_error.message());
  }
  const fs::directory_iterator end;
  while (entry != end) {
    std::error_code entry_error;
    const bool regular = entry->is_regular_file(entry_error);
    if (entry_error) {
      // Whole-or-nothing (tables.h): an entry we cannot even ask about might
      // be the table this run needs, and skipping it would hand back a
      // silently partial balance — the one outcome this loader refuses.
      return fail("cannot read '" + entry->path().string() + "': " + entry_error.message());
    }
    if (regular && entry->path().extension() == ".csv") {
      files.push_back(entry->path());
    }
    entry.increment(fs_error);
    if (fs_error) {
      return fail("cannot list '" + root.string() + "': " + fs_error.message());
    }
  }
  std::sort(files.begin(), files.end());

  auto set = std::make_unique<CsvTableSet>();
  for (const fs::path& path : files) {
    std::string table_error;
    std::unique_ptr<CsvTable> table = LoadOneTable(path, table_error);
    if (table == nullptr) {
      // Whole-or-nothing: one bad file fails the load (tables.h).
      return fail(path.filename().string() + ": " + table_error);
    }
    set->Add(path.stem().string(), std::move(table));
  }
  return set;
}

}  // namespace core
