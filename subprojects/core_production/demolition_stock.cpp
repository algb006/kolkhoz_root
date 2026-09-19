// What a unit being taken down held, on its way to the stores
// (demolition_stock.h).

#include "demolition_stock.h"

#include <cstddef>
#include <cstdint>

#include "core_common/unit_state.h"
#include "stock_ops.h"

namespace core {

void SettleDemolitionStock(const ProductionConfig& config, WorldState& current) {
  for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
    if (current.units.rows[row].construction.phase != ConstructionPhase::kDemolishing) {
      continue;
    }
    // By index and re-read: the stores delivered to are rows of this same
    // table, and the door may not deliver into the site itself — a level-0
    // row stores nothing (NumberedStoreAccepts asks StoresGoods).
    for (std::size_t index = 0; index < current.units.rows[row].stock.size(); ++index) {
      const Grams held = current.units.rows[row].stock[index];
      if (held <= 0) {
        continue;
      }
      const Grams placed =
          DeliverToStores(current, config, DefIdFromIndex<ResourceIdTag>(index), held);
      current.units.rows[row].stock[index] -= placed;
    }
  }
}

void CollectDemolitionAlarms(const WorldState& world, std::vector<Alarm>& alarms) {
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    const UnitRow& site = world.units.rows[row];
    if (site.construction.phase != ConstructionPhase::kDemolishing) {
      continue;
    }
    Grams waiting = 0;
    Grams largest = 0;
    ResourceId most;
    for (std::size_t index = 0; index < site.stock.size(); ++index) {
      const Grams held = site.stock[index];
      if (held <= 0) {
        continue;
      }
      waiting += held;
      if (held > largest) {
        largest = held;
        most = DefIdFromIndex<ResourceIdTag>(index);
      }
    }
    if (waiting <= 0) {
      continue;
    }
    Alarm alarm;
    alarm.kind = AlarmKind::kDemolitionStockWaiting;
    alarm.unit = world.units.row_ids[row];
    alarm.resource = most;
    alarm.amount = waiting;
    alarms.push_back(alarm);
  }
}

}  // namespace core
