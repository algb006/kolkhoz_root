/// @file
/// @brief Moving resources into and out of stores, pantries and heaps.
/// @threading SINGLE_THREADED
/// Every caller is on the sim thread and outside any phase, and they come in
/// two kinds. The ones that WRITE — AddToStock, DeliverToStores,
/// TakeFromStorage, TakeFromAmounts — are called only from the production
/// decisions sub-step (slot 3), which is sequential: they walk whole tables
/// and would race anywhere else. The pure readers — StoresGoods,
/// StorageCapacityGrams, TotalStock, FreeRoomGrams, StockOf, HeldEverywhere
/// and the three Find* lookups — are also read BETWEEN steps by the
/// subsystem's alarm predicates (task A3, IProductionSystem::CollectAlarms),
/// where nothing is running and nothing is written. Adding a caller means
/// saying which of the two it is; the file holds both kinds side by side.
///
/// NO COUNT OF CALLING FILES HERE, and that is the second correction this
/// paragraph has needed. It used to open with "two callers", which was true
/// when written and is now seven files; a number in prose beside a rule is
/// read as part of the rule and nobody comes back to raise it. The KINDS are
/// two, and that is the part which has to stay true.
///
/// Not a bag of utilities: one subject — a dense ResourceAmounts vector and
/// the arithmetic of adding to it and taking from it — used by the two files
/// of this module that move goods.

#ifndef CORE_PRODUCTION_STOCK_OPS_H_
#define CORE_PRODUCTION_STOCK_OPS_H_

#include <cstdint>
#include <limits>

#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief Grows a stock vector on demand and adds grams (may be negative;
/// clamps at zero).
/// @return WHAT ACTUALLY LANDED, which is not always what was asked for: an
///         unnamed resource has no column to go in and takes nothing, and a
///         negative amount is clamped by what was there. Ignoring this is
///         fine wherever the caller knows the id is good; ACCOUNTING for the
///         load is not one of those places.
///
/// It answered nothing at all until 0.17.79, and the door above it paid for
/// that: DeliverToStores counted its own intention as delivered, the caller
/// subtracted that number from the field, and a harvest whose crop named a
/// resource the roster did not have was destroyed on the way to a store
/// that never received it. The parse now refuses that name at load time —
/// but the CAUSE going away does not take the habit with it, and the first
/// new reason for a refusal would have done the same thing again.
inline Grams AddToStock(ResourceAmounts& stock, ResourceId resource, Grams amount) {
  if (resource.value == kInvalidDefIdValue) {
    return 0;
  }
  if (stock.size() <= resource.value) {
    stock.resize(resource.value + 1U, 0);
  }
  Grams& cell = stock[resource.value];
  const Grams before = cell;
  cell += amount;
  cell = cell < 0 ? 0 : cell;
  return cell - before;
}

/// @brief What a store holds of one resource. A one-line delegate since the
/// stock lights: core_residents needed the same read, so the accessor moved
/// down to core_common beside the type it reads (quantities.h, AmountOf).
/// The name stays because it is what this module's prose calls it.
inline Grams StockOf(const ResourceAmounts& stock, ResourceId resource) {
  return AmountOf(stock, resource);
}

/// @brief Kilograms as grams, multiplying BEFORE the cast. A hen makes far
/// less than a kilogram of manure a day, and casting kilograms first
/// truncated her to zero forever.
///
/// A one-line delegate since the named cast pass, and it stays only for that
/// sentence above it: the ORDER of the multiply and the cast is the thing
/// this name remembers. The guard itself belongs to core_common, and having
/// two conversions meant one of them was unguarded — "safe because of who
/// calls it today" is exactly the reasoning the pass threw out.
inline Grams KilogramsToGrams(float kilograms) {
  return GramsFromKilograms(kilograms);
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
  // The ceiling of the level the unit STANDS at, not of the one being built:
  // a store keeps its old ceiling until the day the level moves (unit rules
  // §11, and ConstructionState says the same about every capacity). The
  // ladder is the ONLY place a capacity comes from — the type's own figure
  // was a copy of level 1 that the export made, so the fallback that read it
  // could never differ from the step it was falling back from.
  return GramsFromKilograms(type.StorageCapacityKgAt(unit.level));
}

/// @brief True when the unit KEEPS GOODS FOR THE SETTLEMENT — by a number
/// or by the outline the player draws, both count — and is BUILT. A level-0
/// row is a construction site: it holds the materials of its own building
/// and stores nothing for anybody (unit_state.h, task A2). One rule, checked
/// wherever a level is read, is what replaces a flag in every table.
///
/// It does NOT answer "has a ceiling to clamp against". That is a second
/// question, it is asked of StorageCapacityGrams, and every caller that
/// needs a ceiling asks it there and says so at its own site. No roster
/// here on purpose: a list of callers spelled out beside a rule is read as
/// a closed one, and the next caller added will not come back to extend it.
inline bool StoresGoods(const UnitRow& unit, const ProductionConfig& config) {
  // ONE READER OF THE CAPACITY, AND IT IS StorageCapacityGrams (2026-09-07).
  // This used to ask the LADDER directly, and the ladder is the wrong source
  // for five of the types: a threshing floor, a manure heap, a silage trench,
  // a firewood yard and a summer camp are bounded by an OUTLINE THE PLAYER
  // DRAWS, so their capacity is an area and an area is not a property of the
  // type. Their ladder cell is blank, and blank is the right answer there —
  // "not from here" rather than a gap.
  //
  // Reading that blank as a zero dropped all five, and did precisely what
  // StorageCapacityGrams forbids in as many words: "callers must treat a
  // negative result as unbounded, NEVER as zero — a manure heap read as a
  // zero-capacity store stops making manure". The cost was recorded before
  // it was understood: ReceivableRoom exists to differ from the alarm's
  // number because using that number once stopped every cart in the village,
  // and with the five dropped it had quietly become the same number again.
  return unit.level > 0 && StorageCapacityGrams(unit, config) != 0;
}

/// @brief First unit able to store goods; kNoRow if none. Phase-1 routing:
/// one shared storage pool. Capacity is no longer ignored — since task A3
/// the door (DeliverToStores) refuses above the ceiling and the remainder is
/// the caller's business, not a line in a log.
inline std::uint32_t FindStorageRow(const WorldState& world, const ProductionConfig& config) {
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    // A NUMBER TO CLAMP AGAINST, and this function has to say so itself now.
    // TakeFromStorage's note has always stated the rule — "a delivery needs a
    // destination with a number to clamp against, so FindStorageRow asks for
    // StoresGoods" — and until StoresGoods was fixed it got that for free
    // from the same defect: outline stores were dropped by accident, and a
    // requirement met by accident is met until the accident is repaired.
    // Without this line the first heap in row order would become the
    // settlement's delivery destination.
    if (StorageCapacityGrams(world.units.rows[row], config) > 0 &&
        StoresGoods(world.units.rows[row], config)) {
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
        config.unit_types[unit.type.value].LivestockCapacityHeadAt(unit.level) > 0.0F) {
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

/// @brief What the settlement holds of one resource, anywhere a taker would
/// find it — the same reach TakeFromStorage has, which is wider than a store:
/// a pantry, a heap and a manger all count.
///
/// ONE HOME, AFTER THREE (2026-09-06). This walk stood twice in the same
/// module — once inside the alarms and once inside the lights — and the two
/// copies differed already: one skipped a marked site with a comment saying
/// why, the other skipped it silently. Two answers to one question part in
/// silence the day somebody edits one of them.
/// @param resource What to count; an unknown id answers 0.
/// @return Grams held everywhere in the settlement.
inline Grams HeldEverywhere(const WorldState& world, ResourceId resource) {
  Grams total = 0;
  for (const UnitRow& unit : world.units.rows) {
    if (unit.level == 0) {
      continue;  // a marked site holds nothing
    }
    total += StockOf(unit.stock, resource);
  }
  return total;
}

/// @brief Total grams a unit is holding, all resources together.
/// A store's ceiling is a tonnage, not a per-resource quota: the design
/// counts what a granary holds, not what it holds of rye.
inline Grams TotalStock(const ResourceAmounts& stock) {
  Grams total = 0;
  for (const Grams amount : stock) {
    if (amount > 0) {
      total += amount;
    }
  }
  return total;
}

/// @brief Free room of one unit in grams: capacity minus what it holds.
/// Negative capacity (an outline the player drew) means unbounded, and is
/// reported as the largest value rather than as zero — a heap is never full.
inline Grams FreeRoomGrams(const UnitRow& unit, const ProductionConfig& config) {
  const Grams capacity = StorageCapacityGrams(unit, config);
  if (capacity < 0) {
    return std::numeric_limits<Grams>::max();
  }
  const Grams held = TotalStock(unit.stock);
  return held >= capacity ? 0 : capacity - held;
}

/// @brief THE DOOR. Puts up to `amount` grams of `resource` into the
/// settlement's stores, in row order, none of them above its capacity;
/// returns what actually went in.
///
/// Every delivery to a store goes through here (task A3,
/// manual/72-storage-and-alarms.md §2). The ceiling is a REFUSAL AT THE
/// DOOR, not a loss: this function puts what fits and says how much that
/// was, and what to do with the remainder is the caller's — the harvest
/// waits on its field, a herd's produce is simply not made, and what can be
/// kept nowhere is booked to the year's `lost_no_room` so that nothing vanishes
/// without a line. A caller that ignores the return value is the bug this
/// signature exists to make visible.
///
/// Taking is wider than delivering and stays that way (TakeFromStorage): a
/// delivery needs a destination with a number to clamp against, taking does
/// not. A level-0 row is a site and stores nothing for anybody.
inline Grams DeliverToStores(WorldState& world,
                             const ProductionConfig& config,
                             ResourceId resource,
                             Grams amount) {
  if (amount <= 0) {
    return 0;
  }
  Grams placed = 0;
  for (std::uint32_t row = 0; row < world.units.rows.size() && placed < amount; ++row) {
    UnitRow& unit = world.units.rows[row];
    // A NUMBERED store, and the test says so itself. It has always had to:
    // an outline reports unbounded room and would swallow the whole load,
    // leaving the second pass below unreachable and the ceiling unenforced.
    // What changed on 2026-09-07 is only the reason it is not redundant —
    // it used to guard against a hypothetical row carrying both a tonnage
    // and the by-plot flag, and it now carries the whole weight, because
    // StoresGoods counts outlines.
    if (!StoresGoods(unit, config) || StorageCapacityGrams(unit, config) < 0) {
      continue;
    }
    const Grams room = FreeRoomGrams(unit, config);
    if (room <= 0) {
      continue;
    }
    const Grams left = amount - placed;
    const Grams take = room < left ? room : left;
    // THE ANSWER, NOT THE INTENTION. `take` is what this door meant to put
    // in; what the vector accepted is what the caller may subtract from the
    // field, and the two part company the moment AddToStock refuses.
    placed += AddToStock(unit.stock, resource, take);
  }
  if (placed >= amount) {
    return placed;
  }
  // SECOND PASS: an outline the player drew that ALREADY HOLDS this very
  // resource — the haystack with the hay in it, the log pile with the logs.
  // Such a place has no ceiling (the player's contour is its size), and the
  // core has no routing table to say which store takes what, so "where this
  // resource already lies" is the only rule available that does not invent
  // one. Without it the ceiling would turn a haystack standing full of hay
  // into a hundred tonnes of hay booked as lost while the stack watched:
  // a numbered store is what a DELIVERY needs (A2, stock_ops.h), and until
  // task A4 gives goods a route, this is the narrowest way to keep the
  // ceiling from inventing losses the design never described. Second pass
  // and not first: the numbered stores are still the settlement's stores.
  for (std::uint32_t row = 0; row < world.units.rows.size() && placed < amount; ++row) {
    UnitRow& unit = world.units.rows[row];
    if (unit.level == 0 || StorageCapacityGrams(unit, config) >= 0) {
      continue;  // not built, or a numbered store the first pass has seen
    }
    if (StockOf(unit.stock, resource) <= 0) {
      continue;  // an outline that is not this resource's home
    }
    placed += AddToStock(unit.stock, resource, amount - placed);
  }
  return placed;
}

/// @brief Takes up to `wanted` grams of `resource` from anywhere the
/// settlement keeps it, in row order; returns what was actually taken.
///
/// TAKING IS WIDER THAN DELIVERING, and deliberately so. A delivery needs a
/// destination with a number to clamp against, so FindStorageRow asks for a
/// capacity it can read. Taking needs nothing of the sort: what lies in a heap or a
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
    if (!StoresGoods(unit, config)) {
      // Neither a numbered store nor an outline the player drew. This used to
      // spell the second half out — `&& StorageCapacityGrams(...) >= 0` —
      // because StoresGoods answered no for an outline; that compensation is
      // what the predicate now does itself, and keeping it would leave a
      // patch standing over a hole that has been filled.
      continue;
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
