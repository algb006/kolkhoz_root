/// @file
/// @brief The run chairman raises a heap of hay still lying after the cut's
/// months (#1015 arm A'; econ meadow-hay-weathering §7b; boss, the #1056
/// thread after [47]): from 1 September, every field-heap task over hay with
/// load not at the level named is set to it (kSetLogisticsLevel) - standing
/// for the player who raises the hay task's level, the door the page names.
/// @threading SINGLE_THREADED
///
/// OFF unless the environment names the level, KOLKHOZ_HAY_HEAP_LEVEL=0..3.
/// Level 1 is every field heap's default (logistics.csv level_field_heap),
/// so only 0 does anything: a rule set to 1 cannot fire.

#ifndef TESTS_RUN_COMMON_HAY_HEAP_POLICY_H_
#define TESTS_RUN_COMMON_HAY_HEAP_POLICY_H_

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string_view>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/logistics_state.h"
#include "core_common/order_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace run {

/// @brief The level KOLKHOZ_HAY_HEAP_LEVEL names, or -1 when unset or not 0..3.
/// MSVC refuses std::getenv as unsafe (C4996 under /WX): _dupenv_s there.
inline int HayHeapLevelFromEnvironment() {
#ifdef _MSC_VER
  char* value = nullptr;
  std::size_t length = 0;
  if (_dupenv_s(&value, &length, "KOLKHOZ_HAY_HEAP_LEVEL") != 0 || value == nullptr) {
    return -1;
  }
  const std::string_view text(value);
  const int level = text.size() == 1 && text[0] >= '0' && text[0] <= '3' ? text[0] - '0' : -1;
  std::free(value);
  return level;
#else
  const char* const value = std::getenv("KOLKHOZ_HAY_HEAP_LEVEL");
  if (value == nullptr) {
    return -1;
  }
  const std::string_view text(value);
  return text.size() == 1 && text[0] >= '0' && text[0] <= '3' ? text[0] - '0' : -1;
#endif
}

/// @brief Raises the hay heaps still lying from 1 September to the level named.
class HayHeapPolicy {
 public:
  explicit HayHeapPolicy(const core::ITableSet& tables) : level_(HayHeapLevelFromEnvironment()) {
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      const std::uint32_t row = resources->FindRowByKey("hay");
      hay_ = row == core::kNoTableRow ? core::kInvalidDefIdValue : static_cast<std::uint16_t>(row);
    }
  }

  /// @brief One day: from the first day of September (month 8, 0-based) to the
  /// year's end, every hay heap's task with load not at the level gets it.
  void RunDay(core::ISimulation& simulation) {
    if (level_ < 0 || hay_ == core::kInvalidDefIdValue) {
      return;
    }
    const core::WorldState& world = simulation.CompletedState();
    if (static_cast<std::uint32_t>(world.calendar.date.month) < 8U) {
      return;
    }
    const auto wanted = static_cast<core::LogisticsLevel>(static_cast<std::uint8_t>(level_));
    std::vector<core::OrderRow> orders;
    for (std::uint32_t row = 0; row < world.logistics_tasks.rows.size(); ++row) {
      const core::LogisticsTaskRow& task = world.logistics_tasks.rows[row];
      if (task.load_kind != core::LogisticsLoadKind::kFieldHeap || task.level == wanted) {
        continue;
      }
      const std::uint32_t field = core::FindRow(world.fields, task.field);
      if (field == core::kNoRow || world.fields.rows[field].reaped_grams <= 0 ||
          world.fields.rows[field].reaped_resource.value != hay_) {
        continue;
      }
      core::OrderRow order;
      order.kind = core::OrderKind::kSetLogisticsLevel;
      order.logistics_task = world.logistics_tasks.row_ids[row];
      order.logistics_level = wanted;
      orders.push_back(order);
      ++raised_;
    }
    if (!orders.empty()) {
      simulation.StageOrders(std::span<const core::OrderRow>(orders.data(), orders.size()), {});
    }
  }

  /// The level named, -1 when off.
  [[nodiscard]] int level() const { return level_; }

  /// Orders staged in the run.
  [[nodiscard]] std::uint32_t raised() const { return raised_; }

 private:
  int level_ = -1;
  std::uint16_t hay_ = core::kInvalidDefIdValue;
  std::uint32_t raised_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_HAY_HEAP_POLICY_H_
