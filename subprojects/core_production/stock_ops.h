/// @file
/// @brief Moving resources into and out of stores, pantries and heaps.
/// @threading SINGLE_THREADED
/// Called only from the production decisions sub-step (slot 3), which is
/// sequential: these functions walk whole tables and would race anywhere
/// else.
///
/// Not a bag of utilities: one subject — a dense ResourceAmounts vector and
/// the arithmetic of adding to it and taking from it — used by the two files
/// of this module that move goods.

#ifndef CORE_PRODUCTION_STOCK_OPS_H_
#define CORE_PRODUCTION_STOCK_OPS_H_

#include <cstdint>

#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief Grows a stock vector on demand and adds grams (may be negative;
/// clamps at zero).
inline void AddToStock(ResourceAmounts& stock, ResourceId resource, Grams amount) {
  if (resource.value == kInvalidDefIdValue) {
    return;
  }
  if (stock.size() <= resource.value) {
    stock.resize(resource.value + 1U, 0);
  }
  Grams& cell = stock[resource.value];
  cell += amount;
  cell = cell < 0 ? 0 : cell;
}

inline Grams StockOf(const ResourceAmounts& stock, ResourceId resource) {
  if (resource.value == kInvalidDefIdValue || stock.size() <= resource.value) {
    return 0;
  }
  return stock[resource.value];
}

/// @brief Kilograms as grams, multiplying BEFORE the cast. A hen makes far
/// less than a kilogram of manure a day, and casting kilograms first
/// truncated her to zero forever.
inline Grams KilogramsToGrams(float kilograms) {
  return static_cast<Grams>(kilograms * static_cast<float>(kGramsPerKilogram));
}

/// @brief True when the unit type stores goods by a number rather than by
/// the outline the player draws — and the unit is BUILT. A level-0 row is a
/// construction site: it holds the materials of its own building and stores
/// nothing for anybody (unit_state.h, task A2). One rule, checked wherever
/// a level is read, is what replaces a flag in every table.
inline bool StoresGoods(const UnitRow& unit, const ProductionConfig& config) {
  return unit.level > 0 && unit.type.value < config.unit_types.size() &&
         config.unit_types[unit.type.value].storage_capacity_kg > 0.0F;
}

/// @brief First unit able to store goods; kNoRow if none. Phase-1 routing:
/// one shared storage pool, capacity overflow is a logged STUB until real
/// logistics.
inline std::uint32_t FindStorageRow(const WorldState& world, const ProductionConfig& config) {
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    if (StoresGoods(world.units.rows[row], config)) {
      return row;
    }
  }
  return kNoRow;
}

/// @brief First unit that houses livestock; kNoRow if none.
///
/// This is the MANGER of the settlement, and it is one lookup on purpose:
/// the harvest delivers hay here, and a kolkhoz herd eats here. When the two
/// were written separately they drifted apart, and the sixteen billeted
/// horses — a herd with no unit of its own — could not reach the hay at all.
/// They lived five years on the village's bread grain and then starved
/// beside two thousand tonnes of it (manual/balance/69-reconciliation.md
/// §3 D1). A stock yard is not a "storing" unit: its table capacity is in
/// HEADS, which is exactly why FindStorageRow does not find it.
inline std::uint32_t FindStockYardRow(const WorldState& world, const ProductionConfig& config) {
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    const UnitRow& unit = world.units.rows[row];
    if (unit.level > 0 && unit.type.value < config.unit_types.size() &&
        config.unit_types[unit.type.value].livestock_capacity_head > 0.0F) {
      return row;
    }
  }
  return kNoRow;
}

/// @brief First unit of the given type; kNoRow if none. An invalid type
/// matches nothing: otherwise it would match every unit whose type is also
/// unset (a table-less world's houses) and index the config out of bounds.
inline std::uint32_t FindUnitRowOfType(const WorldState& world, UnitTypeId type) {
  if (type.value == kInvalidDefIdValue) {
    return kNoRow;
  }
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    if (world.units.rows[row].level > 0 && world.units.rows[row].type.value == type.value) {
      return row;
    }
  }
  return kNoRow;
}

/// @brief Storage capacity of a unit in grams, or a negative value meaning
/// "no number to read": the capacity is the outline the PLAYER draws, and a
/// heap bounded by its own area has no table figure to clamp against.
/// Callers must treat a negative result as unbounded, never as zero — a
/// manure heap read as a zero-capacity store stops making manure.
inline Grams StorageCapacityGrams(const UnitRow& unit, const ProductionConfig& config) {
  if (unit.type.value >= config.unit_types.size()) {
    return 0;
  }
  const UnitTypeDef& type = config.unit_types[unit.type.value];
  if (type.capacity_by_plot != 0) {
    return -1;
  }
  return static_cast<Grams>(type.storage_capacity_kg) * kGramsPerKilogram;
}

/// @brief Takes up to `wanted` grams of `resource` from anywhere the
/// settlement keeps it, in row order; returns what was actually taken.
///
/// TAKING IS WIDER THAN DELIVERING, and deliberately so. A delivery needs a
/// destination with a number to clamp against, so FindStorageRow asks for
/// StoresGoods. Taking needs nothing of the sort: what lies in a heap or a
/// haystack is there whether or not the table gives that heap a tonnage.
/// The narrow rule made the start's hay INVISIBLE the day the start stock
/// moved out of the church and into the haystack the canon actually
/// describes (start_stock.csv, task A2) — a hundred and sixty-five tonnes of
/// fodder beside a herd that starved. A level-0 unit is skipped all the
/// same: an unbuilt site holds materials for its own building and stores
/// nothing for anybody (unit_state.h).
inline Grams TakeFromStorage(WorldState& world,
                             const ProductionConfig& config,
                             ResourceId resource,
                             Grams wanted) {
  Grams taken = 0;
  for (std::uint32_t row = 0; row < world.units.rows.size() && taken < wanted; ++row) {
    UnitRow& unit = world.units.rows[row];
    if (unit.level == 0) {
      continue;  // a site: what is on it belongs to its own building
    }
    if (!StoresGoods(unit, config) && StorageCapacityGrams(unit, config) >= 0) {
      continue;  // neither a numbered store nor an outline the player drew
    }
    const Grams here = StockOf(unit.stock, resource);
    const Grams take = here < wanted - taken ? here : wanted - taken;
    AddToStock(unit.stock, resource, -take);
    taken += take;
  }
  return taken;
}

/// @brief Takes up to `wanted` grams out of one dense vector — a family's
/// pantry, a unit's own stock — and returns what was actually taken.
inline Grams TakeFromAmounts(ResourceAmounts& stock, ResourceId resource, Grams wanted) {
  const Grams here = StockOf(stock, resource);
  const Grams take = here < wanted ? here : wanted;
  AddToStock(stock, resource, -take);
  return take;
}

}  // namespace core

#endif  // CORE_PRODUCTION_STOCK_OPS_H_
