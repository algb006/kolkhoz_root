// The order of the works' supply (#595): the rule's one body, see the header.
#include "core_common/site_supply_queue.h"

#include <algorithm>

#include "core_common/unit_state.h"
#include "core_common/world_state.h"

namespace core {

std::vector<std::uint32_t> SiteSupplyOrder(const WorldState& world) {
  // Every site in the queue, then its order: raised first, the older start
  // first, the id last - a total order, so two readers get one list.
  std::vector<std::uint32_t> queue;
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    const UnitRow& unit = world.units.rows[row];
    if (unit.emptying == 0 && unit.paused == 0 && AnythingAwaits(unit.construction)) {
      queue.push_back(row);
    }
  }
  std::ranges::sort(queue, [&world](std::uint32_t left, std::uint32_t right) {
    const ConstructionState& a = world.units.rows[left].construction;
    const ConstructionState& b = world.units.rows[right].construction;
    if (a.raised != b.raised) {
      return a.raised > b.raised;
    }
    if (a.supply_since_day != b.supply_since_day) {
      return a.supply_since_day < b.supply_since_day;
    }
    return world.units.row_ids[left].value < world.units.row_ids[right].value;
  });
  return queue;
}

std::uint32_t SiteSupplyRank(const WorldState& world, std::uint32_t unit_row) {
  const std::vector<std::uint32_t> order = SiteSupplyOrder(world);
  const auto at = std::ranges::find(order, unit_row);
  return at == order.end() ? kNoSupplyRank : static_cast<std::uint32_t>(at - order.begin());
}

}  // namespace core
