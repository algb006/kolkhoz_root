// The shops of epoch I at work (processing_shops.h).

#include "processing_shops.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "core_catalog/processing_catalog.h"
#include "core_common/calendar.h"
#include "core_common/ledger_state.h"
#include "core_common/unit_state.h"
#include "field_haul.h"
#include "stock_ops.h"
#include "unit_production.h"

namespace core {
namespace {

/// Below this a batch count is nothing: a gram of cabbage is no work.
constexpr double kNoBatch = 1e-6;

/// What a recipe can do today, and what it stands for when it cannot.
struct Availability {
  /// Whole or part batches the stores, the room and the barrels allow.
  double batches = 0.0;

  /// The main input lies there (the cooperage: barrels are wanted).
  bool has_work = false;

  /// A barrel or a second input it stands for; invalid when it works, has
  /// no work, or stands for room (kStoreFull says that).
  ResourceId missing;
};

bool InBarrel(const ProcessingCatalog& catalog, ResourceId resource) {
  return resource.value < catalog.in_barrel.size() && catalog.in_barrel[resource.value] != 0;
}

Grams HeldInBarrels(const ProductionConfig& config, const WorldState& world) {
  Grams held = 0;
  for (std::size_t index = 0; index < config.processing.in_barrel.size(); ++index) {
    if (config.processing.in_barrel[index] != 0) {
      held += HeldEverywhere(world, DefIdFromIndex<ResourceIdTag>(index));
    }
  }
  return held;
}

/// What the started building sites still lack of `resource`: the recipe of
/// the level being raised less what lies delivered (a site's stock, or a
/// standing unit's reserved share). Only kDelivering: a marked plot is not
/// started (construction design §6).
Grams OwedToSites(const ProductionConfig& config, const WorldState& world, ResourceId resource) {
  Grams owed = 0;
  for (const UnitRow& unit : world.units.rows) {
    if (unit.construction.phase != ConstructionPhase::kDelivering) {
      continue;
    }
    for (const LevelCost& cost : config.processing.level_costs) {
      if (cost.unit_type.value != unit.type.value || cost.level != unit.construction.target_level ||
          cost.resource.value != resource.value) {
        continue;
      }
      const Grams delivered = unit.level == 0 ? AmountOf(unit.stock, resource)
                                              : AmountOf(unit.construction.reserved, resource);
      owed += cost.grams > delivered ? cost.grams - delivered : 0;
    }
  }
  return owed;
}

const ProcessingRecipe* FindRule(const ProcessingCatalog& catalog, ProcessingRule rule) {
  for (const ProcessingRecipe& recipe : catalog.recipes) {
    if (recipe.rule == rule) {
      return &recipe;
    }
  }
  return nullptr;
}

bool InPicklingSeason(const ProcessingCatalog& catalog, const WorldState& world) {
  const auto month = static_cast<std::uint8_t>(world.calendar.date.month);
  return month >= catalog.pickling_from_month && month <= catalog.pickling_to_month;
}

/// The fresh reserve (econ, boss seq 184): sauerkraut_fresh_share of the
/// vegetables the book says were harvested this year.
Grams PicklingReserve(const ProductionConfig& config,
                      const WorldState& world,
                      ResourceId vegetables) {
  const Grams harvested = AmountOf(world.ledger.current.harvest, vegetables);
  return static_cast<Grams>(
      std::llround(static_cast<double>(harvested) *
                   static_cast<double>(config.processing.sauerkraut_fresh_share)));
}

/// Barrels wanted, in the cooperage's batches: what the vegetables in the
/// stores would fill as sauerkraut, less the barrels free (boss seq 184).
double CooperageWant(const ProductionConfig& config,
                     const WorldState& world,
                     const ProcessingRecipe& cooperage) {
  const ProcessingCatalog& catalog = config.processing;
  const ProcessingRecipe* const pickling = FindRule(catalog, ProcessingRule::kPickling);
  if (pickling == nullptr || catalog.barrel_capacity_grams <= 0 || cooperage.outputs.empty() ||
      catalog.barrel_grams <= 0 || pickling->inputs.front().grams <= 0) {
    return 0.0;
  }
  const double ratio = static_cast<double>(pickling->outputs.front().grams) /
                       static_cast<double>(pickling->inputs.front().grams);
  const double capacity = static_cast<double>(catalog.barrel_capacity_grams);
  const double vegetables =
      static_cast<double>(TakeableGrams(world, config, pickling->inputs.front().resource));
  const double needed = std::ceil(vegetables * ratio / capacity);
  const double free = std::floor(static_cast<double>(BarrelRoomFree(config, world)) / capacity);
  const double barrels_per_batch = static_cast<double>(cooperage.outputs.front().grams) /
                                   static_cast<double>(catalog.barrel_grams);
  if (needed <= free || !(barrels_per_batch > 0.0)) {
    return 0.0;
  }
  return (needed - free) / barrels_per_batch;
}

/// The tightest bound on a recipe's batches so far, and what set it.
struct Limit {
  double batches = std::numeric_limits<double>::infinity();
  ResourceId resource;

  /// Whether the bound is one a shop STANDS for (a barrel, a second input)
  /// rather than has no work for (its main input) or no room for.
  bool stands = false;

  void Tighten(double by, ResourceId what, bool what_stands) {
    if (by < batches) {
      batches = by;
      resource = what;
      stands = what_stands;
    }
  }
};

/// The inputs' bound; sets `has_work` from the main input (the first line;
/// the cooperage's work is its want, set by the caller).
void LimitByInputs(const ProductionConfig& config,
                   const WorldState& world,
                   const ProcessingRecipe& recipe,
                   Limit& limit,
                   bool& has_work) {
  for (std::size_t line = 0; line < recipe.inputs.size(); ++line) {
    const ProcessingAmount& input = recipe.inputs[line];
    Grams available = TakeableGrams(world, config, input.resource);
    if (recipe.rule == ProcessingRule::kPickling && line == 0) {
      available -= PicklingReserve(config, world, input.resource);
    }
    if (recipe.rule == ProcessingRule::kCooperage) {
      available -= OwedToSites(config, world, input.resource);
    }
    const double by = available > 0 && input.grams > 0
                          ? static_cast<double>(available) / static_cast<double>(input.grams)
                          : 0.0;
    const bool main = line == 0 && recipe.rule != ProcessingRule::kCooperage;
    if (main) {
      has_work = by > kNoBatch;
    }
    limit.Tighten(by, input.resource, !main);
  }
}

/// The outputs' bound: the stores' room (not a stand — kStoreFull says it)
/// and, for what lives in barrels, the free barrels (a stand).
void LimitByOutputs(const ProductionConfig& config,
                    const WorldState& world,
                    const ProcessingRecipe& recipe,
                    Limit& limit) {
  const Grams barrel_room = BarrelRoomFree(config, world);
  for (const ProcessingAmount& output : recipe.outputs) {
    if (output.grams <= 0) {
      continue;
    }
    const auto grams = static_cast<double>(output.grams);
    limit.Tighten(static_cast<double>(ReceivableRoom(config, world, output.resource)) / grams,
                  output.resource,
                  false);
    if (InBarrel(config.processing, output.resource)) {
      limit.Tighten(
          static_cast<double>(barrel_room) / grams, config.processing.barrel_resource, true);
    }
  }
}

Availability Available(const ProductionConfig& config,
                       const WorldState& world,
                       const ProcessingRecipe& recipe) {
  Availability out;
  if (recipe.rule == ProcessingRule::kPickling && !InPicklingSeason(config.processing, world)) {
    return out;  // out of season: no work, and no alarm
  }
  Limit limit;
  if (recipe.rule == ProcessingRule::kCooperage) {
    limit.batches = CooperageWant(config, world, recipe);
    out.has_work = limit.batches > kNoBatch;
  }
  LimitByInputs(config, world, recipe, limit, out.has_work);
  LimitByOutputs(config, world, recipe, limit);
  out.batches = std::isfinite(limit.batches) && limit.batches > kNoBatch ? limit.batches : 0.0;
  if (out.has_work && out.batches <= 0.0 && limit.stands) {
    out.missing = limit.resource;
  }
  return out;
}

/// `batches` of the recipe: inputs out of the stores, outputs (× the shop's
/// wear) through the door, both booked. What the door refuses is lost_no_room
/// — nothing vanishes without a line.
void WorkBatches(const ProductionConfig& config,
                 WorldState& current,
                 const ProcessingRecipe& recipe,
                 double batches,
                 float wear_factor) {
  YearLedger& book = current.ledger.current;
  for (const ProcessingAmount& input : recipe.inputs) {
    const auto wanted =
        static_cast<Grams>(std::llround(batches * static_cast<double>(input.grams)));
    const Grams taken = TakeFromStorage(current, config, input.resource, wanted);
    AddLedgerAmount(book.processed, input.resource, taken);
  }
  for (const ProcessingAmount& output : recipe.outputs) {
    // Rounded, not floored: batches come back from man-days ÷ days-per-batch
    // and land a hair under the whole (7.9999999), and a floor lost the gram.
    // The room and the barrels were checked for the whole batches already.
    const auto made = static_cast<Grams>(std::llround(batches * static_cast<double>(output.grams) *
                                                      static_cast<double>(wear_factor)));
    const Grams placed = DeliverToStores(current, config, output.resource, made);
    AddLedgerAmount(book.made, output.resource, made);
    AddLedgerAmount(book.lost_no_room, output.resource, made - placed);
  }
}

bool IsShop(const ProcessingCatalog& catalog, UnitTypeId type) {
  return ProcessingPlaces(catalog, type) > 0;
}

}  // namespace

std::int64_t BarrelsHeld(const ProductionConfig& config, const WorldState& world) {
  const ProcessingCatalog& catalog = config.processing;
  if (catalog.barrel_resource.value == kInvalidDefIdValue || catalog.barrel_grams <= 0) {
    return 0;
  }
  return HeldEverywhere(world, catalog.barrel_resource) / catalog.barrel_grams;
}

Grams BarrelRoomFree(const ProductionConfig& config, const WorldState& world) {
  const Grams room = BarrelsHeld(config, world) * config.processing.barrel_capacity_grams;
  const Grams held = HeldInBarrels(config, world);
  return room > held ? room - held : 0;
}

void SettleProcessing(const ProductionConfig& config, WorldState& current) {
  const ProcessingCatalog& catalog = config.processing;
  if (catalog.recipes.empty()) {
    return;
  }
  for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
    const UnitTypeId type = current.units.rows[row].type;
    if (!IsShop(catalog, type)) {
      continue;
    }
    // By index and re-read, as the sawmill does: the stores taken from and
    // delivered to are rows of this same table.
    const float written = current.units.rows[row].production_days_written;
    const float remaining = current.units.rows[row].production_days_remaining;
    double left = written > remaining ? static_cast<double>(written - remaining) : 0.0;
    const float wear = WearOutputFactor(config, current.units.rows[row]);
    for (const ProcessingRecipe& recipe : catalog.recipes) {
      if (recipe.unit_type.value != type.value || !(left > 0.0)) {
        continue;
      }
      const Availability can = Available(config, current, recipe);
      const double days = std::min(left, can.batches * static_cast<double>(recipe.labor_days));
      if (!(days > 0.0)) {
        continue;
      }
      left -= days;
      WorkBatches(config, current, recipe, days / static_cast<double>(recipe.labor_days), wear);
    }
    double demand = 0.0;
    if (UnitCanWork(current, current.units.rows[row])) {
      for (const ProcessingRecipe& recipe : catalog.recipes) {
        if (recipe.unit_type.value == type.value) {
          demand +=
              Available(config, current, recipe).batches * static_cast<double>(recipe.labor_days);
        }
      }
    }
    current.units.rows[row].production_days_remaining = static_cast<float>(demand);
    current.units.rows[row].production_days_written = static_cast<float>(demand);
  }
}

void CollectProcessingAlarms(const ProductionConfig& config,
                             const WorldState& world,
                             std::vector<Alarm>& alarms) {
  const ProcessingCatalog& catalog = config.processing;
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    const UnitRow& unit = world.units.rows[row];
    if (!IsShop(catalog, unit.type) || !UnitCanWork(world, unit)) {
      continue;
    }
    for (const ProcessingRecipe& recipe : catalog.recipes) {
      if (recipe.unit_type.value != unit.type.value) {
        continue;
      }
      const Availability can = Available(config, world, recipe);
      if (can.missing.value == kInvalidDefIdValue) {
        continue;
      }
      Alarm alarm;
      alarm.kind = AlarmKind::kProcessingStopped;
      alarm.unit = world.units.row_ids[row];
      alarm.resource = can.missing;
      alarms.push_back(alarm);
      break;  // one per shop: the first recipe that stands says why
    }
  }
}

void WearBarrels(const ProductionConfig& config, WorldState& current) {
  const ProcessingCatalog& catalog = config.processing;
  const std::int64_t held = BarrelsHeld(config, current);
  if (held <= 0 || !(catalog.barrel_wear_per_year > 0.0F)) {
    return;
  }
  // Whole barrels, to the nearest (boss seq 184, question 5).
  const std::int64_t lost =
      std::llround(static_cast<double>(held) * static_cast<double>(catalog.barrel_wear_per_year));
  const Grams taken =
      TakeFromStorage(current, config, catalog.barrel_resource, lost * catalog.barrel_grams);
  AddLedgerAmount(current.ledger.current.spoiled, catalog.barrel_resource, taken);
}

}  // namespace core
