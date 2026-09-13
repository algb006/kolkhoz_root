/// @file
/// @brief The runs' declared difference from the game: STORES STAND ALONE —
/// store modularity is off in the prosthesis (boss, 2026-09-13, parcel 198,
/// option A).
/// @threading SINGLE_THREADED
/// Test-side code. The table set it wraps is read-only once built, like any
/// table set, and the wrapper adds no state of its own after construction.
///
/// WHAT IT DOES. The core refuses to mark a module without a sound parent
/// (unit rules §11), and by unit_types.csv every store is a module of a yard.
/// The runs' chairman built stores on their own before that rule existed, and
/// every store measurement of the day stands on that. Boss decided that store
/// modularity comes as a separate move with a before/after measurement, so
/// until then the runs read unit_types.csv with the `parent` cell BLANKED for
/// every module that keeps goods — a type whose level 1 has a storage
/// capacity (unit_levels.csv `storage_capacity_t`). Today that is granary,
/// food_store, icehouse, goods_store and fuel_point; the rule is the data's,
/// so a sixth store would join without an edit here.
///
/// Every other module — the sawmill first — keeps its parent. The shipped
/// tables are not touched: the difference lives in the run, is printed once
/// per process, and goes the day store modularity is switched on.

#ifndef TESTS_RUN_COMMON_STORE_PROSTHESIS_H_
#define TESTS_RUN_COMMON_STORE_PROSTHESIS_H_

#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core_tables/tables.h"

namespace run {

/// @brief unit_types with the `parent` cell blank on the rows named.
class StoresStandAloneTable final : public core::ITable {
 public:
  StoresStandAloneTable(const core::ITable& inner, std::vector<std::uint8_t> blanked)
      : inner_(inner), blanked_(std::move(blanked)), parent_column_(inner.FindColumn("parent")) {}

  std::uint32_t RowCount() const override { return inner_.RowCount(); }

  std::uint32_t ColumnCount() const override { return inner_.ColumnCount(); }

  std::uint32_t FindColumn(std::string_view name) const override { return inner_.FindColumn(name); }

  std::uint32_t FindRowByKey(std::string_view key) const override {
    return inner_.FindRowByKey(key);
  }

  std::string_view CellText(std::uint32_t row, std::uint32_t column) const override {
    return Blanked(row, column) ? std::string_view{} : inner_.CellText(row, column);
  }

  std::optional<std::int64_t> CellInteger(std::uint32_t row, std::uint32_t column) const override {
    return Blanked(row, column) ? std::nullopt : inner_.CellInteger(row, column);
  }

  std::optional<float> CellReal(std::uint32_t row, std::uint32_t column) const override {
    return Blanked(row, column) ? std::nullopt : inner_.CellReal(row, column);
  }

 private:
  bool Blanked(std::uint32_t row, std::uint32_t column) const {
    return column == parent_column_ && column != core::kNoTableColumn && row < blanked_.size() &&
           blanked_[row] != 0;
  }

  const core::ITable& inner_;
  std::vector<std::uint8_t> blanked_;
  std::uint32_t parent_column_;
};

/// @brief A table set that is the loaded one but for unit_types.
class StoresStandAloneTables final : public core::ITableSet {
 public:
  explicit StoresStandAloneTables(std::unique_ptr<core::ITableSet> inner)
      : inner_(std::move(inner)) {
    const core::ITable* const types = inner_->FindTable("unit_types");
    const core::ITable* const levels = inner_->FindTable("unit_levels");
    if (types == nullptr || levels == nullptr) {
      return;
    }
    std::vector<std::uint8_t> blanked(types->RowCount(), 0);
    const std::uint32_t parent_column = types->FindColumn("parent");
    const std::uint32_t unit_column = levels->FindColumn("unit");
    const std::uint32_t level_column = levels->FindColumn("level");
    const std::uint32_t storage_column = levels->FindColumn("storage_capacity_t");
    for (std::uint32_t row = 0; row < levels->RowCount(); ++row) {
      const std::optional<std::int64_t> level = levels->CellInteger(row, level_column);
      const std::optional<float> storage = levels->CellReal(row, storage_column);
      if (!level.has_value() || *level != 1 || !storage.has_value() || !(*storage > 0.0F)) {
        continue;
      }
      const std::uint32_t type_row = types->FindRowByKey(levels->CellText(row, unit_column));
      if (type_row == core::kNoTableRow || types->CellText(type_row, parent_column).empty()) {
        continue;
      }
      blanked[type_row] = 1;
      names_ += names_.empty() ? "" : ", ";
      names_ += std::string(types->CellText(type_row, types->FindColumn("key")));
    }
    unit_types_ = std::make_unique<StoresStandAloneTable>(*types, std::move(blanked));
  }

  const core::ITable* FindTable(std::string_view name) const override {
    if (name == "unit_types" && unit_types_ != nullptr) {
      return unit_types_.get();
    }
    return inner_->FindTable(name);
  }

  std::uint32_t TableCount() const override { return inner_->TableCount(); }

  std::string_view TableName(std::uint32_t index) const override {
    return inner_->TableName(index);
  }

  /// @brief The stores whose parent was blanked, comma-separated.
  const std::string& StandAloneStores() const { return names_; }

 private:
  std::unique_ptr<core::ITableSet> inner_;
  std::unique_ptr<StoresStandAloneTable> unit_types_;
  std::string names_;
};

/// @brief Wraps a loaded set and says the difference out loud, once a process.
inline std::unique_ptr<core::ITableSet> StoresStandAlone(std::unique_ptr<core::ITableSet> loaded) {
  auto wrapped = std::make_unique<StoresStandAloneTables>(std::move(loaded));
  static bool said = false;
  if (!said && !wrapped->StandAloneStores().empty()) {
    said = true;
    std::cout << "run: DECLARED DIFFERENCE — stores stand alone, store modularity is off in the "
                 "prosthesis ("
              << wrapped->StandAloneStores() << ")\n";
  }
  return wrapped;
}

}  // namespace run

#endif  // TESTS_RUN_COMMON_STORE_PROSTHESIS_H_
