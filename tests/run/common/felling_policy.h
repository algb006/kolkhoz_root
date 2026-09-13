/// @file
/// @brief The run's chairman fells the nearest stand when the village has
/// fewer logs than one granary takes.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THE RUN FELLS. On the approved layout the store sites stood on logs
/// and nothing else: the 690 start logs were gone by year 7..14, and 47 of the
/// 55 store-class failed plan years of nine seeds had a granary site waiting
/// for logs (plan_shortfall, THE STORE SITES, 2026-09-13). The core had no
/// channel of logs at all; now it has felling (timber design §8a), and a run
/// has no chairman to mark a stand. So the run plays him, exactly as it plays
/// the building chairman (fixture_policy.h) and the yard (yard_policy.h).
///
/// ONE SIGNAL, ONE ANSWER (boss, parcel 181): fewer logs than one granary →
/// mark the nearest stand that still has timber for one granary's logs. It
/// is the answer a chairman gives to a site short of logs, and nothing
/// smarter: no planning a year ahead, no choosing a stand by its haul.
/// THE FIXTURE DIFFERS FROM THE START CANON, AND IT SAYS SO (Declare).

#ifndef TESTS_RUN_COMMON_FELLING_POLICY_H_
#define TESTS_RUN_COMMON_FELLING_POLICY_H_

#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core_catalog/timber_catalog.h"
#include "core_common/order_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/timber_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace run {

class FellingPolicy {
 public:
  explicit FellingPolicy(const core::ITableSet& tables) {
    std::string error;
    ready_ = core::ParseTimberCatalog(tables, catalog_, error) &&
             catalog_.log_resource.value != core::kInvalidDefIdValue && catalog_.log_grams > 0 &&
             !catalog_.stands.empty();
    granary_logs_ = GranaryLogs(tables);
  }

  /// @brief The fixture difference, in words, for the run to print BEFORE it
  /// measures anything.
  static void Declare(const char* run) {
    std::cout << run
              << ": FIXTURE DIFFERS FROM THE START CANON — the run's chairman FELLS the nearest "
                 "stand when the village has fewer logs than one granary takes (timber design "
                 "§8a; boss, 2026-09-13)\n";
  }

  /// @brief One day of the chairman's attention. Call once a day.
  void RunDay(core::ISimulation& simulation) {
    if (!ready_) {
      return;
    }
    if (cooldown_ > 0) {
      --cooldown_;
      return;
    }
    const core::WorldState& world = simulation.CompletedState();
    for (const core::TimberStandRow& stand : world.stands.rows) {
      if (stand.marked_m3 > 0.0F) {
        return;  // a felling is going: one at a time
      }
    }
    // LOGS IN HAND, and the logs already felled and lying count as in hand: a
    // chairman who has a heap in the grove does not fell another for want of
    // carts.
    core::Grams logs = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      logs += catalog_.log_resource.value < unit.stock.size()
                  ? unit.stock[catalog_.log_resource.value]
                  : 0;
    }
    for (const core::TimberStandRow& stand : world.stands.rows) {
      logs += stand.load_grams;
    }
    const core::Grams wanted = static_cast<core::Grams>(granary_logs_) * catalog_.log_grams;
    if (logs >= wanted) {
      return;
    }
    core::OrderRow order;
    if (!NearestMark(world, order)) {
      return;
    }
    simulation.StageOrders(std::span<const core::OrderRow>(&order, 1), {});
    ++marked_;
    cooldown_ = kCooldownDays;
  }

  /// @brief What the policy did, for the run to print at the end.
  void Report(const char* run, const core::WorldState& world) const {
    float felled_left = 0.0F;
    float started = 0.0F;
    for (const core::TimberStandRow& stand : world.stands.rows) {
      if (stand.kind == core::TimberStandKind::kForestOld ||
          stand.table_row >= catalog_.stands.size()) {
        continue;
      }
      felled_left += stand.stock_m3;
      started += core::StartStockM3(catalog_, catalog_.stands[stand.table_row]);
    }
    std::cout << run << ": the run's chairman marked " << marked_
              << " fellings; groves and belts hold " << std::lround(felled_left) << " of "
              << std::lround(started) << " m3\n";
  }

 private:
  static constexpr std::uint32_t kCooldownDays = 2;

  /// The logs a first-level granary takes (unit_level_cost.csv), read so the
  /// signal moves with the table.
  static std::uint32_t GranaryLogs(const core::ITableSet& tables) {
    constexpr std::uint32_t kFallback = 60;
    const core::ITable* const costs = tables.FindTable("unit_level_cost");
    if (costs == nullptr) {
      return kFallback;
    }
    const std::uint32_t unit_col = costs->FindColumn("unit");
    const std::uint32_t level_col = costs->FindColumn("level");
    const std::uint32_t resource_col = costs->FindColumn("resource");
    const std::uint32_t amount_col = costs->FindColumn("amount");
    for (std::uint32_t row = 0; row < costs->RowCount(); ++row) {
      if (costs->CellText(row, unit_col) == "granary" && costs->CellText(row, level_col) == "1" &&
          costs->CellText(row, resource_col) == "log") {
        const std::optional<float> amount = costs->CellReal(row, amount_col);
        return amount.has_value() && *amount > 0.0F ? static_cast<std::uint32_t>(*amount)
                                                    : kFallback;
      }
    }
    return kFallback;
  }

  /// The village's centre, as the building chairman reckons it.
  static core::Vec2 Centre(const core::WorldState& world) {
    core::Vec2 sum{.x = 0.0F, .y = 0.0F};
    for (const core::UnitRow& unit : world.units.rows) {
      sum.x += unit.position.x;
      sum.y += unit.position.y;
    }
    const auto count = static_cast<float>(world.units.rows.size());
    return count > 0.0F ? core::Vec2{.x = sum.x / count, .y = sum.y / count} : sum;
  }

  bool NearestMark(const core::WorldState& world, core::OrderRow& order) const {
    const core::Vec2 centre = Centre(world);
    std::uint32_t best = core::kNoRow;
    float best_distance = 0.0F;
    float best_volume = 0.0F;
    for (std::uint32_t row = 0; row < world.stands.rows.size(); ++row) {
      const core::TimberStandRow& stand = world.stands.rows[row];
      if (stand.table_row >= catalog_.stands.size()) {
        continue;
      }
      const core::TimberStandDef& def = catalog_.stands[stand.table_row];
      const float share = stand.kind == core::TimberStandKind::kForestOld
                              ? def.log_share * catalog_.old_log_share_factor
                              : def.log_share;
      if (!(share > 0.0F) || !(stand.stock_m3 > 0.0F)) {
        continue;
      }
      // Enough of the stand for one granary's logs, or what it has left.
      const float wanted_m3 = static_cast<float>(granary_logs_) * catalog_.log_m3 / share;
      const float volume = wanted_m3 < stand.stock_m3 ? wanted_m3 : stand.stock_m3;
      // A MARK THAT YIELDS NOT ONE LOG IS NOT A MARK: an old-forest square
      // holds a few cubic metres of trunks, and marking its crumbs would fell
      // nothing and be marked again the next day, for ever.
      if (volume * share < catalog_.log_m3) {
        continue;
      }
      const float dx = stand.position.x - centre.x;
      const float dy = stand.position.y - centre.y;
      const float distance = std::sqrt((dx * dx) + (dy * dy));
      if (best == core::kNoRow || distance < best_distance) {
        best = row;
        best_distance = distance;
        best_volume = volume;
      }
    }
    if (best == core::kNoRow) {
      return false;
    }
    order.kind = core::OrderKind::kMarkFelling;
    order.stand = world.stands.row_ids[best];
    order.volume_m3 = best_volume;
    return true;
  }

  core::TimberCatalog catalog_;

  bool ready_ = false;

  std::uint32_t granary_logs_ = 60;

  std::uint32_t cooldown_ = 0;

  std::uint32_t marked_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_FELLING_POLICY_H_
