// Carrying a load off a field, and the rot of everything that lies in a
// store (field_haul.h). Split out of production_system.cpp when that file
// crossed the thousand-line limit: the day of a load and the day of a crop
// are different days, and only one of them is about growing.

#include "field_haul.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

#include "core_common/herd_state.h"
#include "core_common/ids.h"
#include "core_common/ledger_state.h"
#include "core_common/spoilage.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "stock_ops.h"

namespace core {
namespace {

/// @brief Whether the settlement has an adult kolkhoz horse at all. The cart
/// is not counted apart from the animal: the canon of the start hands out
/// horses and horse tackle to the same yards together, so a free draught
/// horse IS a cart of 750 kg (boss, 2026-09-03).
///
/// THIS BLOCK AND SettleHauling's HAD SWAPPED PLACES, and neither was where
/// it belonged: this one stood over SettleHauling, and SettleHauling's own
/// words floated above the closing brace of the namespace, attached to
/// nothing at all. The sixteenth case of the same class, and the first found
/// by reading a file the delta mechanism had never once looked at
/// (2026-09-07).
bool DraughtHorsesFree(const ProductionConfig& config, const WorldState& world) {
  if (config.horse_kind.value == kInvalidDefIdValue) {
    return false;
  }
  for (const HerdRow& herd : world.herds.rows) {
    if (herd.kind.value == config.horse_kind.value && herd.household_owned == 0 &&
        herd.adult_count > 0) {
      return true;
    }
  }
  return false;
}

}  // namespace

/// @brief Room a load of this resource could actually be delivered INTO,
/// unlike FreeRoomOfStores in production_alarms.cpp, which deliberately
/// ignores stores bounded by an outline because the alarm it serves asks
/// "will this fit in a building". A heap under the open sky takes
/// everything, and a haul must be sent for a load a heap can receive. Told
/// apart because the first version of the hauling cap used the alarm's
/// number and stopped every cart in the village: the granaries were full,
/// the heaps were not, and nobody was sent (task A4).
/// @return The sum over numbered stores, or Grams max when an outline
///         holding this resource stands.
///
/// THE SECOND HALF COULD NOT HAPPEN UNTIL 2026-09-07, which made this
/// function numerically identical to the number it was written to differ
/// from — found by the first reading this file had ever had, and cured
/// where the fault was rather than here.
///
/// StoresGoods asked the LADDER for a capacity — `StorageCapacityKgAt() > 0`
/// — and every outline store leaves that cell blank, so all five of them
/// (threshing floor, manure heap, firewood yard, silage trench, summer camp)
/// were dropped at the `continue` below, and the unbounded branch under it
/// was unreachable code that looked like a working rule. It now asks
/// StorageCapacityGrams, whose contract says in as many words what the
/// ladder reading was breaking: "callers must treat a negative result as
/// unbounded, NEVER as zero — a manure heap read as a zero-capacity store
/// stops making manure".
///
/// The cost is worth keeping in view, because it was PAID BEFORE IT WAS
/// UNDERSTOOD: this function exists at all because the hauling cap once used
/// the alarm's number and stopped every cart in the village. The defect then
/// quietly restored that very number here, and the separation survived as
/// prose about a difference that had stopped existing.
Grams ReceivableRoom(const ProductionConfig& config, const WorldState& world, ResourceId resource) {
  Grams room = 0;
  for (const UnitRow& unit : world.units.rows) {
    if (!StoresGoods(unit, config)) {
      continue;
    }
    const Grams free_here = FreeRoomGrams(unit, config);
    if (free_here == std::numeric_limits<Grams>::max()) {
      // AN OUTLINE THE PLAYER DREW, and it is not a destination for
      // anything: THE DOOR takes a load into one only when the outline is
      // that resource's home (IsHomeOf; DeliverToStores, second pass). Until
      // 2026-09-14 "home" meant "some of it already lies there", for want of
      // a table — and an emptied log pile stopped being the logs' home, so
      // 24 t of felled logs lay in the grove with no demand to cart them
      // (boss, parcels 267-268; resource_stores.csv).
      //
      // The demand has to ask the door's own question, and until 2026-09-07
      // it could not be caught asking a different one: StoresGoods dropped
      // every outline a line earlier, so this branch was unreachable and its
      // claim — that ANY heap can receive ANY load — was never tested. It
      // came alive the day StoresGoods was repaired and cost the run its
      // harvest in one build: the village hauled its rye to a log pile, the
      // door refused it, and by year six the farm had stopped ploughing to
      // do nothing but carry grain into a closed door. That is the exact
      // failure SettleHauling's own comment says it exists to prevent.
      if (IsHomeOf(unit, config, resource)) {
        return free_here;  // the haystack, the hay's home, empty or not
      }
      continue;
    }
    // A numbered store counts only for what it takes in — the door's own
    // question (NumberedStoreTakes, DeliverToStores).
    if (!NumberedStoreTakes(unit, config, resource)) {
      continue;
    }
    room += free_here;
  }
  return room;
}

namespace {

HaulRate RateToward(const ProductionConfig& config,
                    const WorldState& world,
                    Vec2 from,
                    Vec2 destination) {
  const bool harnessed = DraughtHorsesFree(config, world);
  // РАСПУТИЦА slows the cart and the carrier alike (WeatherState::mud); on
  // every road, because the haul is measured in a straight line and the core
  // knows no gravel on it (production_config.h, mud_speed_factor).
  const float mud = world.weather.mud ? config.farming.mud_speed_factor : 1.0F;
  const float speed_kmh = (harnessed ? config.harness_speed_kmh : config.walk_speed_kmh) * mud;
  const float hours_per_km = speed_kmh > 0.0F ? static_cast<float>(kClockScale) / speed_kmh : 0.0F;
  const Grams load = GramsFromKilograms(harnessed ? config.cart_load_kg : config.carry_kg_adult);
  return RateBetween(from, destination, hours_per_km, load);
}

/// @brief One load's day: what the carriers took in, and tomorrow's demand.
/// The body SettleHauling had for a field, over any holder of a load — a
/// field or a timber stand (2026-09-13, "the carting is a mechanism, not a
/// row"). Every rule of it is written at its first home, SettleHauling below.
void SettleLoad(const ProductionConfig& config,
                WorldState& current,
                const HaulRate& rate,
                ResourceId resource,
                Grams& load,
                float& haul_days_remaining,
                float& haul_days_written) {
  const Grams receivable = ReceivableRoom(config, current, resource);
  const Grams haulable = receivable < load ? receivable : load;
  const float done =
      haul_days_written > haul_days_remaining ? haul_days_written - haul_days_remaining : 0.0F;
  const float against = haul_days_written;
  if (done > 0.0F && against > 0.0F) {
    const float share = done / against;
    const Grams carried =
        GramsFromFloat(static_cast<float>(haulable) * (share > 1.0F ? 1.0F : share));
    const Grams offered = carried < load ? carried : load;
    const Grams moved = DeliverToStores(current, config, resource, offered);
    load -= moved;
  }
  const Grams left = ReceivableRoom(config, current, resource);
  haul_days_remaining =
      load > 0 ? HaulDaysFor(left < load ? left : load, rate, config.standard_day_hours) : 0.0F;
  haul_days_written = haul_days_remaining;
}

}  // namespace

HaulRate FieldHaulRate(const ProductionConfig& config,
                       const WorldState& world,
                       const FieldRow& field) {
  // To a store that takes the load in (NumberedStoreTakes), first in row order
  // as the door fills them; none such: the shared store, as before.
  std::uint32_t store_row = kNoRow;
  for (std::uint32_t row = 0; row < world.units.rows.size() && store_row == kNoRow; ++row) {
    const UnitRow& unit = world.units.rows[row];
    if (StoresGoods(unit, config) && StorageCapacityGrams(unit, config) > 0 &&
        field.reaped_resource.value != kInvalidDefIdValue &&
        NumberedStoreTakes(unit, config, field.reaped_resource)) {
      store_row = row;
    }
  }
  if (store_row == kNoRow) {
    store_row = FindStorageRow(world, config);
  }
  const Vec2 destination =
      store_row == kNoRow ? field.center : world.units.rows[store_row].position;
  return RateToward(config, world, field.center, destination);
}

HaulRate StandHaulRate(const ProductionConfig& config,
                       const WorldState& world,
                       const TimberStandRow& stand) {
  // THE LOGS GO TO THEIR HOME — the log pile of the start, "куча брёвен,
  // бесплатная площадка" (timber design §2), named so by resource_stores.csv
  // — because that is the door's own rule for a heap under the open sky
  // (DeliverToStores, second pass), and a haul measured to the granary would
  // price a trip the logs never make. No log pile: the shared store.
  std::uint32_t destination_row = kNoRow;
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    const UnitRow& unit = world.units.rows[row];
    if (StoresGoods(unit, config) && StorageCapacityGrams(unit, config) < 0 &&
        IsHomeOf(unit, config, config.timber.log_resource)) {
      destination_row = row;
      break;
    }
  }
  if (destination_row == kNoRow) {
    destination_row = FindStorageRow(world, config);
  }
  const Vec2 destination =
      destination_row == kNoRow ? stand.position : world.units.rows[destination_row].position;
  return RateToward(config, world, stand.position, destination);
}

void SettleStandHauling(const ProductionConfig& config, WorldState& current) {
  for (TimberStandRow& stand : current.stands.rows) {
    if (stand.load_grams <= 0) {
      stand.haul_days_remaining = 0.0F;
      stand.haul_days_written = 0.0F;
      continue;
    }
    const HaulRate rate = StandHaulRate(config, current, stand);
    SettleLoad(config,
               current,
               rate,
               config.timber.log_resource,
               stand.load_grams,
               stand.haul_days_remaining,
               stand.haul_days_written);
  }
}

HaulRate SiteHaulRate(const ProductionConfig& config,
                      const WorldState& world,
                      const ExtractionSiteRow& site) {
  // The clay to the clay pile, the stone to the stone pile: the resource's
  // home by resource_stores.csv, the door's own rule for a heap under the open
  // sky, as for the logs above. No such heap: the shared store.
  std::uint32_t destination_row = kNoRow;
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    const UnitRow& unit = world.units.rows[row];
    if (StoresGoods(unit, config) && StorageCapacityGrams(unit, config) < 0 &&
        IsHomeOf(unit, config, site.resource)) {
      destination_row = row;
      break;
    }
  }
  if (destination_row == kNoRow) {
    destination_row = FindStorageRow(world, config);
  }
  const Vec2 destination =
      destination_row == kNoRow ? site.position : world.units.rows[destination_row].position;
  return RateToward(config, world, site.position, destination);
}

void SettleSiteHauling(const ProductionConfig& config, WorldState& current) {
  for (ExtractionSiteRow& site : current.extraction_sites.rows) {
    if (site.load_grams <= 0) {
      site.haul_days_remaining = 0.0F;
      site.haul_days_written = 0.0F;
      continue;
    }
    const HaulRate rate = SiteHaulRate(config, current, site);
    SettleLoad(config,
               current,
               rate,
               site.resource,
               site.load_grams,
               site.haul_days_remaining,
               site.haul_days_written);
  }
}

/// @brief Turns the hauling labor delivered today into grain that actually
/// moved, and re-sizes tomorrow's demand for what is still lying out.
///
/// Before task A4 this was a daily retry that moved everything the stores
/// had room for, the instant they had it. The load now waits for HANDS,
/// and the door still decides where it fits: capacity is production's
/// rule and transport is labor's muscle, which is why one of them writes
/// the seam and the other drains it.
void SettleHauling(const ProductionConfig& config, WorldState& current) {
  for (FieldRow& field : current.fields.rows) {
    if (field.reaped_grams <= 0) {
      continue;
    }
    const HaulRate rate = FieldHaulRate(config, current, field);
    // NOBODY IS SENT FOR A LOAD THAT HAS NOWHERE TO GO. The ceiling is a
    // refusal at the door (task A3), and a cart turned away at the door
    // has still cost the village a man for a day. The first version of
    // this asked for carriers on the whole load: by year four the village
    // had stopped ploughing and reaping and did nothing but haul grain
    // into a granary that could not take it — 2700 man-days a year poured
    // into a closed door. The alarm for that state already exists and says
    // the true reason (kHarvestWaitingOnField); what was missing was that
    // the demand must be the SMALLER of what lies out and what can be
    // received.
    // THE DEMAND IS THE SMALLER OF WHAT LIES OUT AND WHAT CAN BE TAKEN
    // IN, and both halves of that are load-bearing. Drop the load and the
    // village hauls for ever into a granary that never opens. Drop the
    // room and it does the same thing more slowly: a gap of one kilogram
    // asks for two hundred tonnes to be carried, the whole village goes,
    // and one kilogram arrives — measured, twice, in the thirty-year run.
    //
    // What was drained since the demand was written. Never negative: room
    // shrinks overnight as well as grows, and a demand that came out
    // smaller than what is left of it means nobody hauled, not that
    // somebody un-hauled.
    // WHAT PEOPLE CARRIED, and nothing else. Measured against what the
    // settlement itself wrote last night — not against today's demand, which
    // is capped by a room that grows every day as the village eats. Comparing
    // the two demands booked that growth as somebody's day of work, and the
    // run carried its whole harvest for nothing.
    //
    // SETTLED AS A SHARE OF THE LOAD, not as an absolute weight. The seam
    // was set to `wanted` last night and drained by real people since, so
    // the share carried is what is missing from it. Converting the missing
    // man-days straight back into grams looked simpler and was subtly
    // wrong: the rate is recomputed here, and a horse that appeared
    // overnight made the two ends of the subtraction measure different
    // things. A share cannot move more of a load than the load has.
    //
    // Tomorrow's demand is what is still out there and can still be taken,
    // and it is remembered, because tomorrow it is the only honest baseline.
    //
    // THE ARITHMETIC LIVES IN SettleLoad since 2026-09-13, shared with the
    // logs lying on a timber stand; the reasons stay here, at their first
    // home.
    SettleLoad(config,
               current,
               rate,
               field.reaped_resource,
               field.reaped_grams,
               field.haul_days_remaining,
               field.haul_days_written);
    if (field.reaped_grams == 0) {
      field.reaped_resource = ResourceId{};
    }
  }
}

/// @brief A day in the life of everything lying in a unit's store.
/// The larders are core_residents' rows and rot there, by the same rule
/// out of the same column — one rule, two owners, no drift.
namespace {

/// What leaves an emptied store first (start §5, «сначала то что портится и
/// то что воруют»): the shortest shelf life — nought, «keeps for ever», is
/// the longest — then what is stolen more readily, then row order.
std::vector<std::uint32_t> EmptyingOrder(const ProductionConfig& config, const UnitRow& unit) {
  std::vector<std::uint32_t> order;
  for (std::uint32_t index = 0; index < unit.stock.size(); ++index) {
    if (unit.stock[index] > 0) {
      order.push_back(index);
    }
  }
  const auto shelf = [&config](std::uint32_t index) {
    const float days = index < config.spoil_days.size() ? config.spoil_days[index] : 0.0F;
    return days > 0.0F ? days : std::numeric_limits<float>::infinity();
  };
  const auto theft = [&config](std::uint32_t index) {
    return index < config.theft_rank.size() ? config.theft_rank[index] : std::uint8_t{0};
  };
  std::ranges::sort(order, [&shelf, &theft](std::uint32_t left, std::uint32_t right) {
    if (shelf(left) != shelf(right)) {
      return shelf(left) < shelf(right);
    }
    if (theft(left) != theft(right)) {
      return theft(left) > theft(right);
    }
    return left < right;
  });
  return order;
}

}  // namespace

void SettleStoreEmptying(const ProductionConfig& config, WorldState& current) {
  for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
    UnitRow& unit = current.units.rows[row];
    if (unit.emptying == 0) {
      continue;
    }
    const std::vector<std::uint32_t> order = EmptyingOrder(config, unit);
    // What has somewhere to go today: each resource up to the room that
    // takes it (the emptied unit is nobody's home, stock_ops.h).
    const auto movable = [&config, &current, &unit, &order]() {
      Grams total = 0;
      for (const std::uint32_t index : order) {
        const ResourceId resource = DefIdFromIndex<ResourceIdTag>(index);
        const Grams room = ReceivableRoom(config, current, resource);
        total += unit.stock[index] < room ? unit.stock[index] : room;
      }
      return total;
    };
    const float done = unit.haul_days_written > unit.haul_days_remaining
                           ? unit.haul_days_written - unit.haul_days_remaining
                           : 0.0F;
    const bool carrying = unit.emptying == 1;  // 2: the carrying is paused
    if (carrying && done > 0.0F && unit.haul_days_written > 0.0F) {
      const float share = done / unit.haul_days_written;
      Grams budget = GramsFromFloat(static_cast<float>(movable()) * (share > 1.0F ? 1.0F : share));
      for (const std::uint32_t index : order) {
        if (budget <= 0) {
          break;
        }
        const ResourceId resource = DefIdFromIndex<ResourceIdTag>(index);
        const Grams wanted = unit.stock[index] < budget ? unit.stock[index] : budget;
        const Grams moved = DeliverToStores(current, config, resource, wanted);
        AddToStock(unit.stock, resource, -moved);
        budget -= moved;
      }
    }
    // Tomorrow's demand: what is left that has somewhere to go, toward the
    // first store that takes the first of it.
    const Grams left = carrying ? movable() : 0;
    Vec2 destination = unit.position;
    for (const UnitRow& other : current.units.rows) {
      if (!order.empty() && StoresGoods(other, config) &&
          NumberedStoreTakes(other, config, DefIdFromIndex<ResourceIdTag>(order.front()))) {
        destination = other.position;
        break;
      }
    }
    const HaulRate rate = RateToward(config, current, unit.position, destination);
    unit.haul_days_remaining = left > 0 ? HaulDaysFor(left, rate, config.standard_day_hours) : 0.0F;
    unit.haul_days_written = unit.haul_days_remaining;
  }
}

void SpoilFieldHeaps(const ProductionConfig& config, WorldState& current) {
  const float keeping = config.keeping_factor * config.farming.field_heap_keeping_factor;
  for (FieldRow& field : current.fields.rows) {
    const ResourceId resource = field.reaped_resource;
    if (field.reaped_grams <= 0 || resource.value >= config.spoil_days.size()) {
      continue;
    }
    const Grams gone = SpoiledToday(field.reaped_grams, config.spoil_days[resource.value], keeping);
    if (gone <= 0) {
      continue;
    }
    field.reaped_grams -= gone;
    AddLedgerAmount(current.ledger.current.spoiled, resource, gone);
    if (field.reaped_grams <= 0) {
      field.reaped_grams = 0;
      field.reaped_resource = ResourceId{};  // the invariant: empty means unnamed
    }
  }
}

void SpoilStores(const ProductionConfig& config, WorldState& current) {
  if (config.spoil_days.empty()) {
    return;  // a table-less world keeps everything for ever
  }
  for (UnitRow& unit : current.units.rows) {
    if (unit.stock.empty()) {
      continue;
    }
    SpoilAmounts(
        unit.stock, config.spoil_days, config.keeping_factor, current.ledger.current.spoiled);
  }
}

}  // namespace core
