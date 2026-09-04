// Carrying a load off a field, and the rot of everything that lies in a
// store (field_haul.h). Split out of production_system.cpp when that file
// crossed the thousand-line limit: the day of a load and the day of a crop
// are different days, and only one of them is about growing.

#include "field_haul.h"

#include <cstdint>
#include <limits>

#include "core_common/herd_state.h"
#include "core_common/ids.h"
#include "core_common/ledger_state.h"
#include "core_common/spoilage.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "stock_ops.h"

namespace core {
namespace {

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

/// @brief Turns the hauling labor delivered today into grain that actually
/// moved, and re-sizes tomorrow's demand for what is still lying out.
///
/// Before task A4 this was a daily retry that moved everything the stores
/// had room for, the instant they had it. The load now waits for HANDS,
/// and the door still decides where it fits: capacity is production's
/// rule and transport is labor's muscle, which is why one of them writes
/// the seam and the other drains it.

}  // namespace

/// @brief Room a load could actually be delivered INTO, unlike
/// FreeRoomOfStores below, which deliberately ignores stores bounded by an
/// outline because the alarm it serves asks "will this fit in a building".
/// A heap under the open sky takes everything, and a haul must be sent for
/// a load a heap can receive. Told apart because the first version of the
/// hauling cap used the alarm's number and stopped every cart in the
/// village: the granaries were full, the heaps were not, and nobody was
/// sent (task A4).
/// @return The sum, or Grams max when any store has no ceiling at all.
Grams ReceivableRoom(const ProductionConfig& config, const WorldState& world) {
  Grams room = 0;
  for (const UnitRow& unit : world.units.rows) {
    if (!StoresGoods(unit, config)) {
      continue;
    }
    const Grams free_here = FreeRoomGrams(unit, config);
    if (free_here == std::numeric_limits<Grams>::max()) {
      return free_here;  // an outline store: the load has somewhere to go
    }
    room += free_here;
  }
  return room;
}

HaulRate FieldHaulRate(const ProductionConfig& config,
                       const WorldState& world,
                       const FieldRow& field) {
  const std::uint32_t store_row = FindStorageRow(world, config);
  const Vec2 destination =
      store_row == kNoRow ? field.center : world.units.rows[store_row].position;
  const bool harnessed = DraughtHorsesFree(config, world);
  const float speed_kmh = harnessed ? config.harness_speed_kmh : config.walk_speed_kmh;
  const float hours_per_km = speed_kmh > 0.0F ? static_cast<float>(kClockScale) / speed_kmh : 0.0F;
  const Grams load = GramsFromKilograms(harnessed ? config.cart_load_kg : config.carry_kg_adult);
  return RateBetween(field.center, destination, hours_per_km, load);
}

/// @brief Whether the settlement has an adult kolkhoz horse at all. The
/// cart is not counted apart from the animal: the canon of the start hands
/// out horses and horse tackle to the same yards together, so a free
/// draught horse IS a cart of 750 kg (boss, 2026-09-03).
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
    const Grams receivable = ReceivableRoom(config, current);
    const Grams haulable = receivable < field.reaped_grams ? receivable : field.reaped_grams;
    // What was drained since the demand was written. Never negative: room
    // shrinks overnight as well as grows, and a demand that came out
    // smaller than what is left of it means nobody hauled, not that
    // somebody un-hauled.
    // WHAT PEOPLE CARRIED, and nothing else. Measured against what the
    // settlement itself wrote last night — not against today's demand, which
    // is capped by a room that grows every day as the village eats. Comparing
    // the two demands booked that growth as somebody's day of work, and the
    // run carried its whole harvest for nothing.
    const float done = field.haul_days_written > field.haul_days_remaining
                           ? field.haul_days_written - field.haul_days_remaining
                           : 0.0F;
    const float against = field.haul_days_written;
    // SETTLED AS A SHARE OF THE LOAD, not as an absolute weight. The seam
    // was set to `wanted` last night and drained by real people since, so
    // the share carried is what is missing from it. Converting the missing
    // man-days straight back into grams looked simpler and was subtly
    // wrong: the rate is recomputed here, and a horse that appeared
    // overnight made the two ends of the subtraction measure different
    // things. A share cannot move more of a load than the load has.
    if (done > 0.0F && against > 0.0F) {
      const float share = done / against;
      const Grams carried =
          GramsFromFloat(static_cast<float>(haulable) * (share > 1.0F ? 1.0F : share));
      const Grams offered = carried < field.reaped_grams ? carried : field.reaped_grams;
      const Grams moved = DeliverToStores(current, config, field.reaped_resource, offered);
      field.reaped_grams -= moved;
      if (field.reaped_grams == 0) {
        field.reaped_resource = ResourceId{};
      }
    }
    // Tomorrow's demand: what is still out there and can still be taken.
    const Grams left = ReceivableRoom(config, current);
    field.haul_days_remaining = HaulDaysFor(
        left < field.reaped_grams ? left : field.reaped_grams, rate, config.standard_day_hours);
    // And remember it, because tomorrow this is the only honest baseline.
    field.haul_days_written = field.haul_days_remaining;
  }
}

/// @brief A day in the life of everything lying in a unit's store.
/// The larders are core_residents' rows and rot there, by the same rule
/// out of the same column — one rule, two owners, no drift.
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
