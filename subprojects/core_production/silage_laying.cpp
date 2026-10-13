// The autumn's silage laying (silage_laying.h).
#include "silage_laying.h"

#include <cmath>
#include <cstdint>
#include <limits>

#include "core_common/calendar.h"
#include "core_common/emit_event.h"
#include "core_common/herd_state.h"
#include "core_common/land_state.h"
#include "core_common/ledger_state.h"
#include "core_common/state_table_ops.h"
#include "field_work.h"
#include "stock_ops.h"

namespace core {

namespace {

bool IsMeadow(const FieldRow& field) {
  return field.kind == LandKind::kMeadow || field.kind == LandKind::kFloodplainMeadow;
}

/// Whether the laying cannot go on tomorrow: the window's last evening or
/// past it, or the haymaking itself is over (field_work.h).
bool LayingEndsTonight(const ProductionConfig& config, const WorldState& world) {
  const auto month = static_cast<std::uint8_t>(world.calendar.date.month);
  if (month > config.farming.silage_to_month) {
    return true;
  }
  if (month == config.farming.silage_to_month &&
      world.calendar.date.day_in_month + 1U >= kDaysPerMonth) {
    return true;
  }
  return MeadowCutEndsTonight(config, world);
}

/// Whether any meadow's cut is still turned for `trench`.
bool AnyCutTurnedFor(const WorldState& world, UnitId trench) {
  for (const FieldRow& field : world.fields.rows) {
    if (field.silage_share > 0.0F && field.silage_trench.value == trench.value) {
      return true;
    }
  }
  return false;
}

/// Green mass, grams, still standing on the cuts turned for `trench`.
Grams StandingFor(const ProductionConfig& config, const WorldState& world, UnitId trench) {
  Grams standing = 0;
  for (const FieldRow& field : world.fields.rows) {
    if (field.silage_share > 0.0F && field.silage_trench.value == trench.value) {
      standing += MeadowGreenStanding(config, field);
    }
  }
  return standing;
}

/// Closes every cut turned for `trench`: the day's share is laid, the rest
/// stands uncut.
void CloseCutsTurnedFor(const ProductionConfig& config, WorldState& current, UnitId trench) {
  for (FieldRow& field : current.fields.rows) {
    if (field.silage_share > 0.0F && field.silage_trench.value == trench.value) {
      CloseMeadowCutNow(config, current, field);
    }
  }
}

/// Whether a turning would take this meadow today: being mown for hay, grass
/// still standing, and no load put aside by an earlier turning. Hay on its
/// heap does not hold it back - the turning puts it aside (field_work.h).
bool MayBeTurned(const ProductionConfig& config, const FieldRow& field) {
  return IsMeadow(field) && field.phase == FieldPhase::kHarvest && !(field.silage_share > 0.0F) &&
         field.set_aside_grams <= 0 && MeadowGreenStanding(config, field) > 0;
}

/// The green mass a cut turned for silage lays for a game man-day of its
/// work, grams: its share's whole green mass over its whole work.
float GreenPerWorkDay(const ProductionConfig& config, const FieldRow& field) {
  if (!(field.harvest_work_days > 0.0F)) {
    return 0.0F;
  }
  const float rate = field.kind == LandKind::kFloodplainMeadow
                         ? config.farming.meadow_floodplain_yield_kg_per_ha
                         : config.farming.meadow_yield_kg_per_ha;
  const float green = static_cast<float>(KilogramsToGrams(rate * field.area_ga)) *
                      field.silage_share * config.farming.green_mass_per_hay;
  return green / field.harvest_work_days;
}

/// «КОСЯТ СТОЛЬКО, СКОЛЬКО УВОЗЯТ» - the day's mowing for one trench, grams
/// of green mass: a day's carting (FarmingConfig::silage_cut_ahead_t) less
/// the backlog - what lies on the heaps above one such day - and never more
/// than the order still lacks. With the heaps cleared each day it is one
/// day's carting; with the carts behind, it shrinks to nought.
Grams MowingOpenedToday(const ProductionConfig& config, Grams on_heaps, Grams lacks) {
  const Grams day = GramsFromKilograms(config.farming.silage_cut_ahead_t * 1000.0F);
  const Grams backlog = on_heaps > day ? on_heaps - day : 0;
  const Grams open = day > backlog ? day - backlog : 0;
  return open < lacks ? open : lacks;
}

/// The row of the meadow nearest `from` a turning would take; kNoRow if none.
/// Ties go to the lower row - the same on every machine.
std::uint32_t NearestToTurn(const ProductionConfig& config, const WorldState& world, Vec2 from) {
  std::uint32_t best = kNoRow;
  float best_m2 = std::numeric_limits<float>::max();
  for (std::uint32_t row = 0; row < world.fields.rows.size(); ++row) {
    const FieldRow& field = world.fields.rows[row];
    if (!MayBeTurned(config, field)) {
      continue;
    }
    const float dx = field.center.x - from.x;
    const float dy = field.center.y - from.y;
    const float m2 = (dx * dx) + (dy * dy);
    if (m2 < best_m2) {
      best_m2 = m2;
      best = row;
    }
  }
  return best;
}

/// A sealed trench whose day has come: all its green mass becomes silage.
void Ripen(const ProductionConfig& config, WorldState& current, UnitRow& unit) {
  unit.silage_sealed_day = kTrenchNotSealed;
  const Grams green = AmountOf(unit.stock, config.green_mass_resource);
  if (green <= 0 || config.silage_resource.value == kInvalidDefIdValue) {
    return;
  }
  const Grams silage = GramsFromFloat(static_cast<float>(green) * config.farming.silage_per_green);
  unit.stock[config.green_mass_resource.value] = 0;
  if (unit.stock.size() <= config.silage_resource.value) {
    unit.stock.resize(static_cast<std::size_t>(config.silage_resource.value) + 1U, 0);
  }
  unit.stock[config.silage_resource.value] += silage;
  // The book: green mass is a harvested resource, and without these lines
  // every tonne laid would leave the balance with no line (as the shops').
  AddLedgerAmount(current.ledger.current.processed, config.green_mass_resource, green);
  AddLedgerAmount(current.ledger.current.made, config.silage_resource, silage);
}

}  // namespace

bool IsSilageTrench(const ProductionConfig& config, const UnitRow& unit) {
  return unit.dead == 0 && unit.level > 0 &&
         config.green_mass_resource.value != kInvalidDefIdValue &&
         IsHomeOf(unit, config, config.green_mass_resource);
}

bool InSilageWindow(const ProductionConfig& config, const WorldState& world) {
  const auto month = static_cast<std::uint8_t>(world.calendar.date.month);
  return month >= config.farming.silage_from_month && month <= config.farming.silage_to_month;
}

Grams GreenOnTheWay(const ProductionConfig& config, const WorldState& world, UnitId trench) {
  Grams heaps = 0;
  for (const FieldRow& field : world.fields.rows) {
    if (field.silage_trench.value == trench.value && field.reaped_grams > 0 &&
        field.reaped_resource.value == config.green_mass_resource.value) {
      heaps += field.reaped_grams;
    }
  }
  return heaps;
}

Grams SilageOrderLacks(const ProductionConfig& config, const WorldState& world, std::uint32_t row) {
  const UnitRow& unit = world.units.rows[row];
  if (unit.silage_target <= 0) {
    return 0;
  }
  const Grams covered = AmountOf(unit.stock, config.green_mass_resource) +
                        GreenOnTheWay(config, world, world.units.row_ids[row]);
  return covered < unit.silage_target ? unit.silage_target - covered : 0;
}

namespace {

/// The window's last day of the year `today` is in, absolute.
SimDay WindowLastDay(const ProductionConfig& config, SimDay today) {
  const SimDay year_start = today - (today % kDaysPerYear);
  return year_start + ((static_cast<SimDay>(config.farming.silage_to_month) + 1U) * kDaysPerMonth) -
         1U;
}

Grams SilageOf(const ProductionConfig& config, Grams green) {
  return GramsFromFloat(static_cast<float>(green) * config.farming.silage_per_green);
}

}  // namespace

void SilageLandingsAhead(const ProductionConfig& config,
                         const WorldState& world,
                         std::vector<SilageLanding>& landings) {
  const SimDay today = world.calendar.day;
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    const UnitRow& unit = world.units.rows[row];
    if (!IsSilageTrench(config, unit)) {
      continue;
    }
    const UnitId id = world.units.row_ids[row];
    const Grams green_in = AmountOf(unit.stock, config.green_mass_resource);
    if (unit.silage_sealed_day != kTrenchNotSealed) {
      if (green_in > 0) {
        landings.push_back({.day = unit.silage_sealed_day + config.farming.silage_ripen_days,
                            .silage = SilageOf(config, green_in)});
      }
      continue;
    }
    Grams coming = green_in + GreenOnTheWay(config, world, id);
    if (unit.silage_target > 0) {
      const Grams lacks = SilageOrderLacks(config, world, row);
      const Grams standing = StandingFor(config, world, id);
      coming += standing < lacks ? standing : lacks;
    }
    if (coming <= 0) {
      continue;
    }
    const SimDay last = WindowLastDay(config, today);
    landings.push_back({.day = (last > today ? last : today) + config.farming.silage_ripen_days,
                        .silage = SilageOf(config, coming)});
  }
}

SilageAdvice SilageLayingToAdvise(const ProductionConfig& config, const WorldState& world) {
  SilageAdvice advice;
  const auto month = static_cast<std::uint8_t>(world.calendar.date.month);
  if (month + 1U < config.farming.silage_from_month || month > config.farming.silage_to_month ||
      MeadowCutEndsTonight(config, world)) {
    return advice;
  }
  UnitId trench;
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    const UnitRow& unit = world.units.rows[row];
    if (IsSilageTrench(config, unit) && unit.silage_sealed_day == kTrenchNotSealed &&
        unit.silage_target <= 0 && AmountOf(unit.stock, config.green_mass_resource) <= 0) {
      trench = world.units.row_ids[row];
      break;
    }
  }
  if (trench.value == kInvalidEntityIdValue) {
    return advice;
  }
  // THE TONNES ARE THE LEAST OF THREE (boss and econ, 13 October 2026; the
  // first pair's lamp named 330-722 t, the whole uncut grass - a number no
  // cart could move):
  // (1) the grass standing on meadows a turning would take;
  Grams grass = 0;
  for (const FieldRow& field : world.fields.rows) {
    if (MayBeTurned(config, field)) {
      grass += MeadowGreenStanding(config, field);
    }
  }
  // (2) what the window's days can cart - a day's green carting each
  // (FarmingConfig::silage_cut_ahead_t), from today or the window's first day;
  const SimDay today = world.calendar.day;
  const SimDay last = WindowLastDay(config, today);
  const SimDay year_start = today - (today % kDaysPerYear);
  const SimDay first = year_start + (static_cast<SimDay>(config.farming.silage_from_month) * kDaysPerMonth);
  const SimDay from = today > first ? today : first;
  const Grams carted = last >= from ? GramsFromKilograms(config.farming.silage_cut_ahead_t * 1000.0F *
                                                         static_cast<float>(last - from + 1U))
                                    : 0;
  // (3) what the herds will eat of silage before the next laying - their
  // ration's share of it for a year - less the silage lying in the stores.
  const Grams silage = SilageOf(config, grass < carted ? grass : carted);
  Grams eaten_kg_year = 0;
  if (config.silage_resource.value < config.feed_values.size() &&
      config.feed_values[config.silage_resource.value] > 0.0F) {
    float kg_a_day = 0.0F;
    for (const auto& link : config.feed_links) {
      if (link.resource.value != config.silage_resource.value ||
          link.kind.value >= config.livestock.size()) {
        continue;
      }
      std::uint32_t heads = 0;
      for (const HerdRow& herd : world.herds.rows) {
        heads += herd.household_owned == 0 && herd.kind.value == link.kind.value ? herd.adult_count : 0U;
      }
      kg_a_day += static_cast<float>(heads) * config.livestock[link.kind.value].feed_units_per_game_day *
                  link.max_share / config.feed_values[config.silage_resource.value];
    }
    eaten_kg_year = GramsFromKilograms(kg_a_day * static_cast<float>(kDaysPerYear));
  }
  Grams held = 0;
  for (const UnitRow& unit : world.units.rows) {
    held += unit.dead == 0 && unit.level > 0 ? AmountOf(unit.stock, config.silage_resource) : 0;
  }
  const Grams wanted = eaten_kg_year > held ? eaten_kg_year - held : 0;
  const Grams named = silage < wanted ? silage : wanted;
  if (named <= 0) {
    return advice;
  }
  advice.trench = trench;
  advice.silage = named;
  advice.ripe_day = (last > today ? last : today) + config.farming.silage_ripen_days;
  return advice;
}

OrderRefusal OrderLaySilage(const ProductionConfig& config,
                            WorldState& current,
                            const OrderRow& order) {
  const std::uint32_t row = FindRow(current.units, order.unit);
  if (row == kNoRow) {
    return OrderRefusal::kNoSuchSubject;
  }
  UnitRow& unit = current.units.rows[row];
  if (!IsSilageTrench(config, unit)) {
    return OrderRefusal::kRuleForbids;
  }
  if (order.amount <= 0) {
    // WITHDRAWN: no more grass is turned or mown for it. What already lies
    // in the trench and on the heaps is the laying, sealed by the evening
    // (SettleSilageLaying reads a trench with green mass and no order).
    unit.silage_target = 0;
    CloseCutsTurnedFor(config, current, order.unit);
    return OrderRefusal::kNone;
  }
  if (unit.silage_sealed_day != kTrenchNotSealed) {
    return OrderRefusal::kTrenchSealed;
  }
  const auto month = static_cast<std::uint8_t>(current.calendar.date.month);
  if (month > config.farming.silage_to_month || MeadowCutEndsTonight(config, current)) {
    return OrderRefusal::kLayingWindowClosed;
  }
  // INSIDE THE WINDOW the answer is known now: grass standing somewhere - on
  // a cut already turned for this trench, or on a meadow a turning would
  // take, its heap of hay carted or not (it will be by a morning). Before
  // the window nobody can say what the haymaking will leave.
  if (InSilageWindow(config, current) && !AnyCutTurnedFor(current, order.unit)) {
    bool grass = false;
    for (const FieldRow& field : current.fields.rows) {
      grass = grass || (IsMeadow(field) && field.phase == FieldPhase::kHarvest &&
                        MeadowGreenStanding(config, field) > 0);
    }
    if (!grass) {
      return OrderRefusal::kNoUncutMeadow;
    }
  }
  // Kept in GREEN MASS, the unit the meadows and the heaps are counted in.
  const float per_green = config.farming.silage_per_green;
  unit.silage_target = per_green > 0.0F
                           ? GramsFromFloat(static_cast<float>(order.amount) / per_green)
                           : order.amount;
  return OrderRefusal::kNone;
}

void RunSilageLaying(const ProductionConfig& config, WorldState& current) {
  const bool in_window = InSilageWindow(config, current);
  for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
    UnitRow& unit = current.units.rows[row];
    const UnitId id = current.units.row_ids[row];
    if (unit.silage_sealed_day != kTrenchNotSealed &&
        current.calendar.day >= unit.silage_sealed_day + config.farming.silage_ripen_days) {
      Ripen(config, current, unit);
    }
    if (unit.silage_target <= 0) {
      continue;
    }
    if (!IsSilageTrench(config, unit)) {
      // The trench is gone from under its order: nothing is mown for it.
      unit.silage_target = 0;
      CloseCutsTurnedFor(config, current, id);
      continue;
    }
    const Grams lacks = SilageOrderLacks(config, current, row);
    if (lacks <= 0) {
      // COVERED by what is in and on the way: the scythes stop. If a heap
      // then rots short of it, tomorrow's morning turns grass again.
      CloseCutsTurnedFor(config, current, id);
      continue;
    }
    // A turned cut mown through - its last share laid this morning - is over.
    for (FieldRow& field : current.fields.rows) {
      if (field.silage_share > 0.0F && field.silage_trench.value == id.value &&
          !(field.work_days_remaining + field.silage_work_held > 0.0F)) {
        CloseMeadowCutNow(config, current, field);
      }
    }
    if (!in_window) {
      continue;
    }
    // THE DAY'S MOWING, paced by the heaps (MowingOpenedToday), and grass
    // turned ONLY FOR IT: a meadow is taken from the hay's scythe the morning
    // its grass is wanted, nearest first, not a week's worth at once.
    Grams open = MowingOpenedToday(config, GreenOnTheWay(config, current, id), lacks);
    // (A kilogram's slack: the standing grass comes through two floats, and
    // a gram short of the day must not take a second meadow from the hay.)
    constexpr Grams kSlack = 1000;
    Grams standing = StandingFor(config, current, id);
    while (standing + kSlack < open) {
      const std::uint32_t meadow = NearestToTurn(config, current, unit.position);
      if (meadow == kNoRow) {
        break;
      }
      FieldRow& field = current.fields.rows[meadow];
      TurnMeadowToSilage(config, current, field, id);
      if (!(field.silage_share > 0.0F)) {
        break;
      }
      standing += MeadowGreenStanding(config, field);
    }
    // And of the turned cuts' work, only the day's is opened to the scythes;
    // the rest is held (FieldRow::silage_work_held). Row order: the same on
    // every machine.
    for (FieldRow& field : current.fields.rows) {
      if (!(field.silage_share > 0.0F) || field.silage_trench.value != id.value) {
        continue;
      }
      const float left = field.work_days_remaining + field.silage_work_held;
      const float per_day = GreenPerWorkDay(config, field);
      float days = per_day > 0.0F ? static_cast<float>(open) / per_day : 0.0F;
      // The last sliver of a cut goes with the day that reaches it: a held
      // remainder under a thousandth of a man-day is rounding, not grass.
      constexpr float kSliverDays = 0.001F;
      days = days + kSliverDays < left ? days : left;
      field.work_days_remaining = days;
      field.silage_work_held = left - days;
      const Grams taken = GramsFromFloat(days * per_day);
      open = open > taken ? open - taken : 0;
    }
  }
}

void SettleSilageLaying(const ProductionConfig& config, WorldState& current) {
  // A meadow whose green heap is gone - carted or rotted - and whose cut is
  // no longer turned keeps no trench's name, and the hay its turning put
  // aside goes back on the heap with its carting priced.
  for (FieldRow& field : current.fields.rows) {
    if (field.silage_share > 0.0F || field.reaped_grams > 0) {
      continue;
    }
    field.silage_trench = UnitId{};
    ReturnSetAsideLoad(config, current, field);
  }
  const bool over = LayingEndsTonight(config, current);
  for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
    UnitRow& unit = current.units.rows[row];
    const UnitId id = current.units.row_ids[row];
    const Grams green = AmountOf(unit.stock, config.green_mass_resource);
    // A laying is open on a trench with an order, and on one that holds
    // green mass unsealed (its order withdrawn, or met and cleared below).
    const bool open = unit.silage_sealed_day == kTrenchNotSealed &&
                      (unit.silage_target > 0 || (green > 0 && IsSilageTrench(config, unit)));
    if (!open) {
      continue;
    }
    if (over) {
      CloseCutsTurnedFor(config, current, id);
    }
    const bool covered = unit.silage_target <= 0 || SilageOrderLacks(config, current, row) <= 0;
    if (!(over || covered) || AnyCutTurnedFor(current, id) ||
        GreenOnTheWay(config, current, id) > 0) {
      continue;  // grass still to mow for it, or a heap still to cart
    }
    // BEFORE THE WINDOW an order with nothing laid is not a laying that has
    // ended: it stands for the autumn.
    if (!over && green <= 0) {
      continue;
    }
    unit.silage_target = 0;
    if (green > 0) {
      unit.silage_sealed_day = current.calendar.day;
    }
    SimEvent& sealed = EmitEvent(current, EventKind::kSilageLaid, EventSeverity::kNotable);
    sealed.unit = id;
    sealed.resource = config.green_mass_resource;
    sealed.amount = green;
  }
}

}  // namespace core
