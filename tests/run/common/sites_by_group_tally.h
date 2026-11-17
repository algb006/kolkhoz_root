/// @file
/// @brief #1726 (boss [22], econ's hypothesis «storage built for the tripled grain takes the timber
///        and the cap»): per village and year, the construction sites STARTED and the logs and
///        boards they reserved at the start, by group - houses (class housing), stores (granary,
///        food_store, goods_store, hay_stack, barn, church_store, clamp, vegetable_store) and
///        the rest; and, at each year's end, the logs and boards lying in the stores.
/// @threading SINGLE_THREADED
/// Test-side code, read once a day at its end.
///
/// THE LINE: «SITES1726 seed S house_started a:b:… house_logs … house_boards … store_started …
/// store_logs … store_boards … other_started … other_logs … other_boards … stock_logs …
/// stock_boards …», sixteen years each, logs in pieces and boards in the table's unit, as reserved.

#ifndef TESTS_RUN_COMMON_SITES_BY_GROUP_TALLY_H_
#define TESTS_RUN_COMMON_SITES_BY_GROUP_TALLY_H_

#include <array>
#include <cstdint>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

class SitesByGroupTally {
 public:
  static constexpr std::uint32_t kYears = 16;

  explicit SitesByGroupTally(const core::ITableSet& tables) {
    static constexpr std::array<std::string_view, 8> kStores = {"granary",
                                                                "food_store",
                                                                "goods_store",
                                                                "hay_stack",
                                                                "barn",
                                                                "church_store",
                                                                "clamp",
                                                                "vegetable_store"};
    if (const core::ITable* const types = tables.FindTable("unit_types")) {
      const std::uint32_t klass = types->FindColumn("class");
      for (std::uint32_t row = 0; row < types->RowCount(); ++row) {
        const std::string_view key = types->CellText(row, 0);
        std::uint8_t group = kOther;
        if (klass != core::kNoTableColumn && types->CellText(row, klass) == "housing") {
          group = kHouse;
        }
        for (const std::string_view store : kStores) {
          group = key == store ? kStore : group;
        }
        group_.push_back(group);
      }
    }
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      log_ = resources->FindRowByKey("log");
      board_ = resources->FindRowByKey("board");
    }
  }

  void CountDayEnd(const core::WorldState& world) {
    const std::uint32_t year = world.calendar.day / core::kDaysPerYear;
    if (year >= kYears) {
      return;
    }
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      const auto phase = unit.construction.phase;
      const bool started = phase == core::ConstructionPhase::kDelivering ||
                           phase == core::ConstructionPhase::kBuilding;
      const std::uint32_t id = world.units.row_ids[row].value;
      if (!started || seen_.contains(id)) {
        continue;
      }
      seen_.insert(id);
      const std::uint8_t group = unit.type.value < group_.size() ? group_[unit.type.value] : kOther;
      ++started_[group][year];
      logs_[group][year] += Amount(unit.construction.reserved, log_);
      boards_[group][year] += Amount(unit.construction.reserved, board_);
    }
    if (world.calendar.day % core::kDaysPerYear == core::kDaysPerYear - 1U) {
      for (const core::UnitRow& unit : world.units.rows) {
        stock_logs_[year] += Amount(unit.stock, log_);
        stock_boards_[year] += Amount(unit.stock, board_);
      }
    }
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    static constexpr std::array<std::string_view, 3> kNames = {"house", "store", "other"};
    out << "population_curve: SITES1726 seed " << seed;
    for (std::size_t group = 0; group < 3; ++group) {
      Row(out, std::string(kNames[group]) + "_started", started_[group]);
      Row(out, std::string(kNames[group]) + "_logs", logs_[group]);
      Row(out, std::string(kNames[group]) + "_boards", boards_[group]);
    }
    Row(out, "stock_logs", stock_logs_);
    Row(out, "stock_boards", stock_boards_);
    out << '\n';
  }

 private:
  static constexpr std::uint8_t kHouse = 0;
  static constexpr std::uint8_t kStore = 1;
  static constexpr std::uint8_t kOther = 2;

  static std::int64_t Amount(const core::ResourceAmounts& amounts, std::uint32_t row) {
    return row < amounts.size() ? amounts[row] : 0;
  }

  template <typename T>
  static void Row(std::ostream& out, const std::string& name, const std::array<T, kYears>& v) {
    out << ' ' << name;
    for (std::uint32_t index = 0; index < kYears; ++index) {
      out << (index == 0 ? " " : ":") << v[index];
    }
  }

  std::vector<std::uint8_t> group_;
  std::uint32_t log_ = core::kNoTableRow;
  std::uint32_t board_ = core::kNoTableRow;
  std::set<std::uint32_t> seen_;
  std::array<std::array<std::uint32_t, kYears>, 3> started_{};
  std::array<std::array<std::int64_t, kYears>, 3> logs_{};
  std::array<std::array<std::int64_t, kYears>, 3> boards_{};
  std::array<std::int64_t, kYears> stock_logs_{};
  std::array<std::int64_t, kYears> stock_boards_{};
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_SITES_BY_GROUP_TALLY_H_
