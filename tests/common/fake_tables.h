/// @file
/// @brief A balance table and a table set made of literals, for tests that
/// must not touch the disk.
/// @threading SINGLE_THREADED
/// Test-side code, built and read on the thread that runs the test.
///
/// WHY THIS IS SHARED AND THE ROWS ARE NOT. Boss's line, 2026-09-04:
///
/// > **Подделка таблицы — это водопровод, а не фикстура.** Фикстура — данные
/// > внутри неё, и они обязаны остаться разными: на каждое правило свой
/// > предмет, который только оно и отбраковывает.
///
/// So the pipe lives here and the water stays in each test. A shared set of
/// ROWS would drift back into one fixture that every rule is checked
/// against, and a subject that several rules reject tells you nothing about
/// which one did.
///
/// WHAT IT COST TO WRITE THIS FIVE TIMES. The five copies had diverged, and
/// not cosmetically: core_construction's parsed cells with `std::stod`,
/// which THROWS on text where the shipped reader returns "not a number" and
/// accepts a partial parse where the shipped reader refuses one. A test
/// standing on that fake measured a reader the core does not have. That is
/// the same class as the two `CellOrDefault` functions with one name and
/// different argument orders — the danger is not the duplicate, it is the
/// divergence inside it.
///
/// THE RULE THIS FILE OBEYS: **the fake reads a cell exactly as
/// core_tables/csv_table_set.cpp does**, `std::from_chars` and the refusal
/// of `inf` and `nan` included. A test that needs a reader which lets those
/// through — because it is checking the SECOND lock on that door — writes
/// its own one-off and says so; that is its subject, not plumbing.

#ifndef TESTS_COMMON_FAKE_TABLES_H_
#define TESTS_COMMON_FAKE_TABLES_H_

#include <charconv>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "core_tables/tables.h"

namespace test {

/// @brief One in-memory table: a header row plus data rows.
/// @note The key column is column 0, as it is in every shipped table.
class FakeTable final : public core::ITable {
 public:
  FakeTable(std::vector<std::string> columns, std::vector<std::vector<std::string>> rows)
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

  /// @brief As csv_table_set.cpp does it: a whole decimal integer or nothing.
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

  /// @brief As csv_table_set.cpp does it, INCLUDING the refusal of `inf` and
  /// `nan`: `std::from_chars` accepts both as a complete match, and every
  /// consumer of a real cell eventually casts it to an integer, where a
  /// non-finite value is undefined behaviour.
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
    if (!std::isfinite(value)) {
      return std::nullopt;
    }
    return value;
  }

 private:
  std::vector<std::string> columns_;

  std::vector<std::vector<std::string>> rows_;
};

/// @brief A table set holding whatever tables the test names.
/// @note Non-owning: every table must outlive the set, which is what a test
///       gets for free by declaring both in the same scope.
class FakeTableSet final : public core::ITableSet {
 public:
  /// @brief An empty set — a world with no tables at all, which is what a
  /// subsystem factory must survive on its documented defaults.
  FakeTableSet() = default;

  FakeTableSet(std::vector<std::pair<std::string, const core::ITable*>> tables)
      : tables_(std::move(tables)) {}

  /// @brief The one-table case, which is most of them.
  FakeTableSet(std::string name, const core::ITable& table) : tables_{{std::move(name), &table}} {}

  const core::ITable* FindTable(std::string_view name) const override {
    for (const auto& [table_name, table] : tables_) {
      if (table_name == name) {
        return table;
      }
    }
    return nullptr;
  }

  std::uint32_t TableCount() const override { return static_cast<std::uint32_t>(tables_.size()); }

  std::string_view TableName(std::uint32_t index) const override {
    return index < tables_.size() ? std::string_view(tables_[index].first) : std::string_view{};
  }

 private:
  std::vector<std::pair<std::string, const core::ITable*>> tables_;
};

}  // namespace test

#endif  // TESTS_COMMON_FAKE_TABLES_H_
