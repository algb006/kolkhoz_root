// The shops of epoch I at work (processing_shops.h).

#include "processing_shops.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "core_catalog/processing_catalog.h"
#include "core_common/away_in_district.h"
#include "core_common/calendar.h"
#include "core_common/geometry.h"
#include "core_common/ledger_state.h"
#include "core_common/resident_state.h"
#include "core_common/road_route.h"
#include "core_common/unit_state.h"
#include "core_common/work_seam.h"
#include "field_haul.h"
#include "stock_ops.h"
#include "unit_production.h"

namespace core {
namespace {

/// Below this a batch count is nothing: a gram of cabbage is no work.
constexpr double kNoBatch = 1e-6;

/// Halvings of the room's search: 2^-40 of the input bound, far below a gram.
constexpr int kRoomSearchSteps = 40;

/// What a recipe can do today, and what it stands for when it cannot.
struct Availability {
  /// Whole or part batches the stores, the room and the barrels allow.
  double batches = 0.0;

  /// The main input lies there (the cooperage: barrels are wanted).
  bool has_work = false;

  /// A barrel, a second input, or the output with no room left for it (boss
  /// seq 11: a legitimate stand says why); invalid when it works or has no
  /// work.
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

/// Batches of `pickling` its inputs in the stores allow: the main input
/// above the fresh reserve when `main_too`, and every other input (the
/// grocery's salt) always. Never below zero.
double PicklingBatchesStocked(const ProductionConfig& config,
                              const WorldState& world,
                              const ProcessingRecipe& pickling,
                              bool main_too) {
  double batches = std::numeric_limits<double>::infinity();
  for (std::size_t line = main_too ? 0 : 1; line < pickling.inputs.size(); ++line) {
    const ProcessingAmount& input = pickling.inputs[line];
    Grams available = TakeableGrams(world, config, input.resource);
    if (line == 0) {
      available -= PicklingReserve(config, world, input.resource);
    }
    const double by = available > 0 && input.grams > 0
                          ? static_cast<double>(available) / static_cast<double>(input.grams)
                          : 0.0;
    batches = std::min(batches, by);
  }
  return batches;
}

/// Grams of sauerkraut the barrels are to be ready for (production units
/// §8а, «Когда»; boss seq 7 and 11 on econ seq 6 and 8). Before this year's
/// vegetables are in the book, last season's sauerkraut — the closed book's
/// `made`, the season lying whole inside the calendar year — «готовим
/// столько, сколько заполнили в прошлом году»; the first year's closed book
/// is empty and the 150 start barrels do. After, what the vegetables above
/// the fresh reserve would make, and no more than the grocery in the stores
/// salts (PicklingBatchesStocked takes every input).
///
/// THE SALT BINDS AFTER THE HARVEST ONLY (boss seq 13): last season's figure
/// was already bounded by last season's salt, and the grocery lot comes in
/// August — a cap before the harvest ate the whole of July. 0.34.8 capped
/// both; that was my reading, and it was wrong.
Grams SauerkrautToBarrel(const ProductionConfig& config,
                         const WorldState& world,
                         const ProcessingRecipe& pickling) {
  const ProcessingAmount& vegetables = pickling.inputs.front();
  const ProcessingAmount& sauerkraut = pickling.outputs.front();
  if (AmountOf(world.ledger.current.harvest, vegetables.resource) <= 0) {
    return AmountOf(world.ledger.closed.made, sauerkraut.resource);
  }
  const double batches = PicklingBatchesStocked(config, world, pickling, true);
  return static_cast<Grams>(std::llround(batches * static_cast<double>(sauerkraut.grams)));
}

/// Barrels wanted, in the cooperage's batches (production units §8а,
/// «Когда»): from July to December, while the free barrels are fewer than
/// the sauerkraut to come (SauerkrautToBarrel) ÷ the barrel's capacity.
///
/// Until 0.34.8 the need was every vegetable in the stores, with neither
/// the reserve nor the season: host's first acceptance saw the cooper make
/// 51-67 barrels from May to August out of boards Epoch I has few of. 0.34.8
/// added the smoked goods held to the need, as §8а then read — and they are
/// already in barrels, so they took the free ones as well and counted twice
/// (boss seq 13 rewrote the line).
double CooperageWant(const ProductionConfig& config,
                     const WorldState& world,
                     const ProcessingRecipe& cooperage) {
  const ProcessingCatalog& catalog = config.processing;
  const ProcessingRecipe* const pickling = FindRule(catalog, ProcessingRule::kPickling);
  if (pickling == nullptr || catalog.barrel_capacity_grams <= 0 || cooperage.outputs.empty() ||
      catalog.barrel_grams <= 0 || pickling->inputs.empty() || pickling->outputs.empty() ||
      !MonthInRange(static_cast<std::uint8_t>(world.calendar.date.month),
                    catalog.cooperage_from_month,
                    catalog.cooperage_to_month)) {
    return 0.0;
  }
  const double capacity = static_cast<double>(catalog.barrel_capacity_grams);
  const double needed =
      std::ceil(static_cast<double>(SauerkrautToBarrel(config, world, *pickling)) / capacity);
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

/// Grams of `output` the stores would take once `batches` of the recipe's
/// inputs are out of them: the door's room (ReceivableRoom) over the stock
/// the take (TakeFromStorage, row order) would leave.
///
/// THE INPUTS MAKE ROOM, and until 2026-09-19 the bound did not know it. The
/// room was read off the stores as they stood, full of the very cabbage the
/// shop was to take: forty tonnes in the food store and a sauerkraut shop
/// standing idle beside it, its demand zero, though every tonne pickled
/// frees a tonne and asks back three hundred and seventy-five kilograms
/// (shop_pace, seed 9; host's seed 9 days 38-40 and 86-90, thread
/// host-econ-shops seq 9).
Grams RoomAfterTaking(const ProductionConfig& config,
                      const WorldState& world,
                      const ProcessingRecipe& recipe,
                      double batches,
                      ResourceId output) {
  std::vector<Grams> left(recipe.inputs.size(), 0);
  for (std::size_t line = 0; line < recipe.inputs.size(); ++line) {
    left[line] =
        static_cast<Grams>(std::llround(batches * static_cast<double>(recipe.inputs[line].grams)));
  }
  Grams room = 0;
  for (const UnitRow& unit : world.units.rows) {
    if (!StoresGoods(unit, config)) {
      continue;
    }
    Grams freed = 0;
    if (IsTakenFrom(unit, config)) {
      for (std::size_t line = 0; line < recipe.inputs.size(); ++line) {
        const ResourceId input = recipe.inputs[line].resource;
        const Grams take = std::min(UnreservedOf(unit, input), left[line]);
        left[line] -= take;
        freed += RoomTaken(config.processing, input, take);
      }
    }
    const Grams capacity = StorageCapacityGrams(unit, config);
    if (capacity < 0) {
      if (IsHomeOf(unit, config, output)) {
        return std::numeric_limits<Grams>::max();  // an outline: no ceiling
      }
      continue;
    }
    if (!NumberedStoreTakes(unit, config, output)) {
      continue;
    }
    const Grams used = RoomUsed(unit.stock, config) - freed;
    room += used < capacity ? GramsFitting(config.processing, output, capacity - used) : 0;
  }
  return room;
}

/// The most batches, up to `upper`, whose `output` the stores take after the
/// inputs are out (RoomAfterTaking). The room grows with the batches only
/// where the take empties a store that also takes the output, so the answer
/// is searched rather than solved: `upper` itself first — the usual answer —
/// then halving toward the last batch count that fits.
double BatchesTheRoomTakes(const ProductionConfig& config,
                           const WorldState& world,
                           const ProcessingRecipe& recipe,
                           const ProcessingAmount& output,
                           double upper) {
  const auto fits = [&](double batches) {
    const double made = batches * static_cast<double>(output.grams);
    return made <=
           static_cast<double>(RoomAfterTaking(config, world, recipe, batches, output.resource));
  };
  if (!std::isfinite(upper)) {
    // No input bounds it: nothing leaves the stores, the room is as it is.
    return static_cast<double>(ReceivableRoom(config, world, output.resource)) /
           static_cast<double>(output.grams);
  }
  if (fits(upper)) {
    return upper;
  }
  double low = 0.0;
  double high = upper;
  for (int step = 0; step < kRoomSearchSteps; ++step) {
    const double middle = (low + high) / 2.0;
    (fits(middle) ? low : high) = middle;
  }
  return low;
}

/// The outputs' bound: the stores' room after the inputs are out (a stand
/// for room, boss seq 11: a shop that has work and nowhere to put it says
/// so) and, for what lives in barrels, the free barrels (a stand).
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
    limit.Tighten(
        BatchesTheRoomTakes(config, world, recipe, output, limit.batches), output.resource, true);
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

void OpenSameDayShops(const ProductionConfig& config, WorldState& current) {
  const ProcessingCatalog& catalog = config.processing;
  for (UnitRow& unit : current.units.rows) {
    if (!IsShop(catalog, unit.type)) {
      continue;
    }
    const bool same_day = std::ranges::any_of(catalog.recipes, [&](const ProcessingRecipe& r) {
      return r.unit_type.value == unit.type.value && WorksTheSameDay(catalog, r);
    });
    if (!same_day || !UnitCanWork(current, unit)) {
      continue;
    }
    double demand = 0.0;
    for (const ProcessingRecipe& recipe : catalog.recipes) {
      if (recipe.unit_type.value == unit.type.value) {
        demand +=
            Available(config, current, recipe).batches * static_cast<double>(recipe.labor_days);
      }
    }
    unit.production_days_remaining = static_cast<float>(demand);
    unit.production_days_written = static_cast<float>(demand);
  }
}

namespace {

/// The road one way, game hours, from the nearest post holder of `parent`
/// to `place`; negative when the parent has no holder with a home. Walking,
/// at the labour model's own chronometer (timber_felling.cpp does the same
/// for the brigade's ride): real km/h over the clock's scale.
float NearestHolderRoadHours(const ProductionConfig& config,
                             const WorldState& world,
                             UnitId parent,
                             Vec2 place) {
  if (!(config.walk_speed_kmh > 0.0F)) {
    return -1.0F;
  }
  const float hours_per_km = static_cast<float>(kClockScale) / config.walk_speed_kmh;
  float best = -1.0F;
  for (const ResidentRow& person : world.residents.rows) {
    Vec2 home;
    // A master in the district's hospital is no master in reach
    // (away_in_district.h): with him counted, a shop whose only master is
    // away would stand in silence under "a master in reach works".
    if (person.post.profession.value == kInvalidDefIdValue ||
        person.post.unit.value != parent.value || !HomePositionOf(world, person.family, home) ||
        OffWork(person, world.calendar.tick)) {
      continue;
    }
    // On foot, by the way there is (road_route.h; 0.36.2).
    const float hours = RoadKm(world, TravelMode::kWalk, home, place) * hours_per_km;
    best = best < 0.0F || hours < best ? hours : best;
  }
  return best;
}

/// Why the shop at `row` stands today, as an alarm; false when it works or
/// has nothing to work. The recipes speak first, in production.csv order;
/// then the road — a shop that could work and whose every master lives
/// beyond the accountant's rule (labor_system.cpp, ReachesForADay) stands
/// with nobody to work it.
bool ShopStands(const ProductionConfig& config,
                const WorldState& world,
                std::uint32_t row,
                Alarm& alarm) {
  const UnitRow& unit = world.units.rows[row];
  alarm.kind = AlarmKind::kProcessingStopped;
  alarm.unit = world.units.row_ids[row];
  bool can_work = false;
  for (const ProcessingRecipe& recipe : config.processing.recipes) {
    if (recipe.unit_type.value != unit.type.value) {
      continue;
    }
    const Availability can = Available(config, world, recipe);
    can_work = can_work || can.batches > kNoBatch;
    if (can.missing.value == kInvalidDefIdValue) {
      continue;
    }
    const bool is_output = std::ranges::any_of(recipe.outputs, [&can](const ProcessingAmount& out) {
      return out.resource.value == can.missing.value;
    });
    alarm.resource = can.missing;
    alarm.stop_reason = is_output ? ProcessingStopReason::kNoRoom : ProcessingStopReason::kShortOf;
    return true;  // one per shop: the first recipe that stands says why
  }
  if (!can_work) {
    return false;
  }
  const float road = NearestHolderRoadHours(config, world, unit.parent, unit.position);
  if (road < 0.0F ||
      RoadLeavesAWorkingDay(
          road, world.weather.daylight_hours, config.travel_limit_hours, config.min_usable_hours)) {
    return false;  // no master at all is not this alarm; a master in reach works
  }
  alarm.stop_reason = ProcessingStopReason::kTooFar;
  alarm.amount = static_cast<std::int64_t>(std::ceil(road));
  return true;
}

}  // namespace

void CollectProcessingAlarms(const ProductionConfig& config,
                             const WorldState& world,
                             std::vector<Alarm>& alarms) {
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    const UnitRow& unit = world.units.rows[row];
    if (!IsShop(config.processing, unit.type) || !UnitCanWork(world, unit)) {
      continue;
    }
    Alarm alarm;
    if (ShopStands(config, world, row, alarm)) {
      alarms.push_back(alarm);
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
