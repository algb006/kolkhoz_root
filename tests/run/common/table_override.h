#ifndef TESTS_RUN_COMMON_TABLE_OVERRIDE_H_
#define TESTS_RUN_COMMON_TABLE_OVERRIDE_H_

/// @file table_override.h
/// @brief A table set that is another with some `key,value` rows of ONE table read otherwise (7 Nov
/// 2026,
///        #1573's session-day cost: the same world collected under two configs, so the world and
///        the code are told apart). Only rows the base table has are overridden; their `value` cell
///        is the given text.
/// @threading SINGLE_THREADED - a run's own helper.

#include <charconv>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "core_tables/tables.h"

namespace run {

class OverriddenTable final : public core::ITable {
 public:
  OverriddenTable(const core::ITable& base, const std::map<std::string, std::string>& values)
      : base_(base), value_col_(base.FindColumn("value")) {
    for (const auto& [key, value] : values) {
      const std::uint32_t row = base.FindRowByKey(key);
      if (row != core::kNoTableRow) {
        by_row_[row] = value;
      }
    }
  }

  std::uint32_t RowCount() const override { return base_.RowCount(); }

  std::uint32_t ColumnCount() const override { return base_.ColumnCount(); }

  std::uint32_t FindColumn(std::string_view name) const override { return base_.FindColumn(name); }

  std::uint32_t FindRowByKey(std::string_view key) const override {
    return base_.FindRowByKey(key);
  }

  std::string_view CellText(std::uint32_t row, std::uint32_t column) const override {
    const auto it = column == value_col_ ? by_row_.find(row) : by_row_.end();
    return it != by_row_.end() ? std::string_view(it->second) : base_.CellText(row, column);
  }

  std::optional<std::int64_t> CellInteger(std::uint32_t row, std::uint32_t column) const override {
    const auto it = column == value_col_ ? by_row_.find(row) : by_row_.end();
    if (it == by_row_.end()) {
      return base_.CellInteger(row, column);
    }
    std::int64_t value = 0;
    const auto [end, error] =
        std::from_chars(it->second.data(), it->second.data() + it->second.size(), value);
    return error == std::errc{} && end == it->second.data() + it->second.size()
               ? std::optional<std::int64_t>(value)
               : std::nullopt;
  }

  std::optional<float> CellReal(std::uint32_t row, std::uint32_t column) const override {
    const auto it = column == value_col_ ? by_row_.find(row) : by_row_.end();
    if (it == by_row_.end()) {
      return base_.CellReal(row, column);
    }
    try {
      return std::stof(it->second);
    } catch (...) {
      return std::nullopt;
    }
  }

  /// @brief How many rows the base table had of those asked.
  std::size_t Overridden() const { return by_row_.size(); }

 private:
  const core::ITable& base_;
  std::uint32_t value_col_;
  std::map<std::uint32_t, std::string> by_row_;
};

class OverriddenTableSet final : public core::ITableSet {
 public:
  OverriddenTableSet(const core::ITableSet& base,
                     std::string_view table,
                     const std::map<std::string, std::string>& values)
      : base_(base), name_(table) {
    if (const core::ITable* const found = base.FindTable(table)) {
      table_.emplace(*found, values);
    }
  }

  const core::ITable* FindTable(std::string_view name) const override {
    return name == name_ && table_.has_value() ? &*table_ : base_.FindTable(name);
  }

  std::uint32_t TableCount() const override { return base_.TableCount(); }

  std::string_view TableName(std::uint32_t index) const override { return base_.TableName(index); }

  std::size_t Overridden() const { return table_.has_value() ? table_->Overridden() : 0; }

 private:
  const core::ITableSet& base_;
  std::string name_;
  std::optional<OverriddenTable> table_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_TABLE_OVERRIDE_H_
