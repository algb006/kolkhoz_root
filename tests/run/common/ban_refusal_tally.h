/// @file
/// @brief #1788's print: the run bot's build orders the placement bans refuse -
/// which type, where, how often - so the reference layout's sites that sit on a
/// ban are NAMED, not silently moved (boss, #1788 (a)).
/// @threading SINGLE_THREADED
/// Test-side code, read once a day from the thread that owns the simulation.

#ifndef TESTS_RUN_COMMON_BAN_REFUSAL_TALLY_H_
#define TESTS_RUN_COMMON_BAN_REFUSAL_TALLY_H_

#include <cmath>
#include <cstdint>
#include <map>
#include <ostream>
#include <string>
#include <utility>

#include "core_common/order_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

class BanRefusalTally {
 public:
  explicit BanRefusalTally(const core::ITableSet& tables)
      : types_(tables.FindTable("unit_types")) {}

  /// @brief Counts every kBuildUnit order refused kRuleForbids since the last
  ///        call, by (type, place rounded to 10 m). An order is counted once,
  ///        by its id.
  void CountDay(const core::WorldState& world) {
    for (std::size_t row = 0; row < world.orders.rows.size(); ++row) {
      const core::OrderRow& order = world.orders.rows[row];
      if (order.kind != core::OrderKind::kBuildUnit ||
          order.status != core::OrderStatus::kRefused ||
          order.refusal != core::OrderRefusal::kRuleForbids) {
        continue;
      }
      const std::uint32_t id = world.orders.row_ids[row].value;
      if (!seen_.emplace(id, true).second) {
        continue;
      }
      const std::string type =
          types_ != nullptr ? std::string(types_->CellText(order.unit_type.value, 0)) : "?";
      const auto x = static_cast<int>(std::lround(order.position.x / 10.0F) * 10);
      const auto y = static_cast<int>(std::lround(order.position.y / 10.0F) * 10);
      ++by_site_[{type, {x, y}}];
    }
  }

  /// @brief One line a village: the refused sites, most refused first.
  void Print(std::ostream& out, std::uint64_t seed) const {
    out << "population_curve: BANS1788 seed " << seed << " refused_kRuleForbids_sites "
        << by_site_.size();
    for (const auto& [site, count] : by_site_) {
      out << " " << site.first << "@" << site.second.first << "," << site.second.second << "x"
          << count;
    }
    out << '\n';
  }

 private:
  const core::ITable* types_;
  std::map<std::uint32_t, bool> seen_;
  std::map<std::pair<std::string, std::pair<int, int>>, std::uint32_t> by_site_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_BAN_REFUSAL_TALLY_H_
