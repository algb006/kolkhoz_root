// Manure carted heap -> field on sledges (manure_cart.h; #1056 C2).

#include "manure_cart.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "cart_trips.h"
#include "core_common/cart_gear.h"
#include "core_common/haul.h"
#include "core_common/hauled_book.h"
#include "core_common/ids.h"
#include "core_common/land_state.h"
#include "core_common/ledger_state.h"
#include "core_common/quantities.h"
#include "core_common/road_route.h"
#include "core_common/state_table_ops.h"
#include "core_common/work_seam.h"
#include "haul_ways.h"
#include "herd_system.h"
#include "stock_ops.h"
#include "team_oat_plan.h"

namespace core {
namespace {

/// The norm dose of `field`, grams (farming §9's dose, unchanged by C2).
Grams NormDoseOf(const ProductionConfig& config, const FieldRow& field) {
  return GramsFromKilograms(config.farming.manure_norm_kg_per_ha * field.area_ga);
}

/// The heap's manure no standing booking holds: its stock less every seam's
/// `due` (none of it has left the heap yet).
Grams UnbookedOf(const ProductionConfig& config, const WorldState& world, std::uint32_t heap) {
  Grams free = StockOf(world.units.rows[heap].stock, config.manure_resource);
  for (const FieldRow& field : world.fields.rows) {
    if (field.manure_dose.from == world.units.row_ids[heap]) {
      free -= field.manure_dose.due;
    }
  }
  return std::max<Grams>(free, 0);
}

/// FieldRow::manure_applied by what has arrived of the norm dose: the share,
/// rounded, at least 1 once anything arrived (PlanManure's own rule).
void WriteAppliedShare(const ProductionConfig& config, FieldRow& field) {
  const Grams dose = NormDoseOf(config, field);
  const Grams arrived = field.manure_dose.booked - field.manure_dose.due;
  if (dose <= 0 || arrived <= 0) {
    return;
  }
  const float share = std::min(static_cast<float>(arrived) / static_cast<float>(dose), 1.0F);
  field.manure_applied = static_cast<std::uint8_t>(std::max(share * 100.0F + 0.5F, 1.0F));
  field.manure_booked = 0;  // a new dose: its furrow books it (0.37.10)
}

struct Candidate {
  std::uint32_t row = 0;
  float road_km = 0.0F;
};

/// The work oats of one horse-day at full work, grams: the horse's need at
/// the full work ration's share, at the work feed's value - the plough's
/// hold's own sizing (herd_system.cpp, PloughFeedHoldOf).
Grams OatsOfAHorseDay(const ProductionConfig& config) {
  if (config.horse_kind.value >= config.livestock.size()) {
    return 0;
  }
  return TeamWorkFeedGrams(config,
                           config.livestock[config.horse_kind.value].feed_units_per_game_day *
                               config.farming.traction_full_ration_share);
}

/// Days from tomorrow to the first ploughing day: the first day of the
/// earliest spring crop's sowing window, this year's or next year's.
std::uint32_t DaysToTheFirstPloughing(const ProductionConfig& config, const WorldState& world) {
  std::uint32_t first = kDaysPerYear;
  for (const CropDef& crop : config.crops) {
    if (!crop.is_winter) {
      first = std::min(first, static_cast<std::uint32_t>(crop.sow_from_month) * kDaysPerMonth);
    }
  }
  const std::uint32_t today = world.calendar.day % kDaysPerYear;
  return today < first ? first - today - 1U : kDaysPerYear - today - 1U + first;
}

}  // namespace

Grams WinterHoldGrams(const ProductionConfig& config, const WorldState& world) {
  // The winter's other carting a day: the closed book's snow-day horse-days
  // less its manure cart-days, by its snow days; in year 1, with no book,
  // every adult horse at work - the error to the horses' side (econ §C2.2;
  // FirstYearTeamNeed's own choice). EVERY DAY to the plough is counted at
  // that rate, snow or not: tomorrow's snow is not known.
  float horses_a_day = 0.0F;
  const YearLedger& book = world.ledger.closed;
  if (world.calendar.day >= static_cast<SimDay>(kDaysPerYear) && book.snow_days > 0.0F) {
    horses_a_day =
        std::max(book.snow_horse_backed_days - book.manure_cart_days, 0.0F) / book.snow_days;
  } else {
    horses_a_day = static_cast<float>(DraughtHorsesOf(world, config.horse_kind));
  }
  const float horse_days =
      horses_a_day * static_cast<float>(DaysToTheFirstPloughing(config, world));
  return GramsFromFloat(horse_days * static_cast<float>(OatsOfAHorseDay(config)));
}

namespace {}  // namespace

void BookManureDoses(const ProductionConfig& config, WorldState& current) {
  // A STANDING BOOKING STANDS until its field's ploughing closes it
  // (CancelLateManure): from the first snow after the harvest to the plough
  // (boss, the #1056 thread [140]; farming design's manure row, «с первого
  // снега после уборки (ноябрь–декабрь тоже) до пахоты»). Until then the
  // turn cleared every booking and November-December carried nothing. Each
  // call books only the fields with no booking, from the heap's manure no
  // booking holds - so what the herds lay over the winter is booked too, and
  // what a winter did not carry is the next one's (the carry-over).
  const std::uint32_t heap = FindUnitRowOfType(current, config.compost_heap_type);
  if (heap == kNoRow) {
    return;
  }
  Grams held = UnbookedOf(config, current, heap);
  const Vec2 heap_at = current.units.rows[heap].position;
  std::vector<Candidate> below;
  std::vector<Candidate> above;
  for (std::uint32_t row = 0; row < current.fields.rows.size(); ++row) {
    const FieldRow& field = current.fields.rows[row];
    // PlanManure's candidates, unchanged (it stood in production_system.cpp
    // until C2): ploughed this year - idle arable, a crop in the slot or
    // fallow; a winter crop already standing was manured when IT was
    // ploughed, last autumn. THE QUESTION IS «WILL THIS GROUND BE WORKED»,
    // not «does it have a chain» (boss, 2026-09-13): a field the chairman
    // withdrew a chain from fell out of the plan, grew poorer and was dropped
    // again - a circle the gate made itself. Land never cropped stays out by
    // its own sign: it has no sowing day, and manure spread on ground nobody
    // ploughs is simply gone (the burdock, 2026-09-12).
    const bool will_be_worked = HasRotation(field) || field.sown_day != kNeverSownDay;
    // In November «this year» is the next: the harvested field stands idle,
    // its dose of this cycle settled at the harvest (manure_applied 0), and
    // its next plough is the spring's. A FIELD BOOKED ALREADY is asked again
    // only while its booking is short of the norm, to top it up: booked daily,
    // the last field of each day would otherwise keep that day's crumbs for
    // good (its manure_applied is what arrived of THIS booking, not a past
    // cycle's).
    const bool standing = field.manure_dose.from.value != kInvalidEntityIdValue;
    if (field.kind != LandKind::kArable || field.phase != FieldPhase::kIdle || !will_be_worked ||
        (standing ? field.manure_dose.booked >= NormDoseOf(config, field) ||
                        field.manure_dose.from != current.units.row_ids[heap]
                  : field.manure_applied != 0)) {
      continue;
    }
    const Candidate candidate{row, RoadKm(current, TravelMode::kCart, heap_at, field.center)};
    (field.fertility < config.farming.manure_fertility_mark ? below : above).push_back(candidate);
  }
  // NEAREST BY ROAD FIRST; equal road -> poorest first; then row order, so the
  // plan is the same on every machine (boss, the #1056 thread [134]).
  const auto nearest = [&current](const Candidate& left, const Candidate& right) {
    if (left.road_km != right.road_km) {
      return left.road_km < right.road_km;
    }
    const float left_fertility = current.fields.rows[left.row].fertility;
    const float right_fertility = current.fields.rows[right.row].fertility;
    return left_fertility != right_fertility ? left_fertility < right_fertility
                                             : left.row < right.row;
  };
  std::ranges::sort(below, nearest);
  std::ranges::sort(above, nearest);
  // While any field below the mark is short of its dose, a field at or above
  // it gets none: the second list is reached only when the first is dosed
  // whole.
  for (const std::vector<Candidate>* list : {&below, &above}) {
    for (const Candidate& candidate : *list) {
      if (held <= 0) {
        return;
      }
      FieldRow& field = current.fields.rows[candidate.row];
      const Grams dose = NormDoseOf(config, field);
      if (dose <= 0) {
        continue;
      }
      // A PARTIAL DOSE IS A DOSE (PlanManure's rule): the heap's last grams
      // go to the next field in the order. Whole doses or nothing meant the
      // biggest field could never be manured at all: 420 t for twenty-one
      // hectares, and a herd of twenty cows makes 250 a year. A standing
      // booking is topped up to the norm.
      const Grams added = std::min(held, dose - field.manure_dose.booked);
      if (added <= 0) {
        continue;
      }
      held -= added;
      field.manure_dose.from = current.units.row_ids[heap];
      field.manure_dose.booked += added;
      field.manure_dose.due += added;
    }
  }
}

void PriceManureDoses(const ProductionConfig& config, WorldState& current) {
  for (FieldRow& field : current.fields.rows) {
    field.manure_dose.priced = 0;
    field.manure_dose.days_remaining = 0.0F;
    field.manure_dose.days_written = 0.0F;
  }
  current.traction_watch.manure_waits_for_oats = 0;
  if (!SledgeDay(current)) {
    return;  // the dose goes by sledge only (manure_state.h)
  }
  // THE TOP RUNG OF THE TEAM'S OATS (C2.2, «Сани по пайку»): the oats the
  // herds may eat over the three old rungs (HerdFeedAllowance), less the
  // winter's hold to the first ploughing day, buy tomorrow's manure
  // cart-days at a horse-day's work oats each. Nothing is moved or sealed:
  // the hold is a reading.
  const ResourceId oats = TeamWorkFeed(config);
  const ResourceAmounts allowance = HerdFeedAllowance(config, current);
  const Grams over_ladder =
      oats.value < allowance.size() ? std::max<Grams>(allowance[oats.value], 0) : 0;
  const Grams hold = WinterHoldGrams(config, current);
  current.traction_watch.oats_over_ladder = over_ladder;
  current.traction_watch.winter_hold = hold;
  const Grams per_day = OatsOfAHorseDay(config);
  float days_bought = per_day > 0 ? static_cast<float>(std::max<Grams>(over_ladder - hold, 0)) /
                                        static_cast<float>(per_day)
                                  : 0.0F;
  // The standing doses, nearest by road from their heap first - the
  // booking's own order, so the oats go where the booking sent the manure.
  std::vector<Candidate> standing;
  for (std::uint32_t row = 0; row < current.fields.rows.size(); ++row) {
    FieldRow& field = current.fields.rows[row];
    if (!ManureDoseStands(field)) {
      continue;
    }
    const std::uint32_t heap = FindRow(current.units, field.manure_dose.from);
    if (heap == kNoRow) {
      field.manure_dose = ManureDoseSeam{};
      continue;
    }
    standing.push_back(
        {row, RoadKm(current, TravelMode::kCart, current.units.rows[heap].position, field.center)});
  }
  std::ranges::sort(standing, [&current](const Candidate& left, const Candidate& right) {
    if (left.road_km != right.road_km) {
      return left.road_km < right.road_km;
    }
    const float left_fertility = current.fields.rows[left.row].fertility;
    const float right_fertility = current.fields.rows[right.row].fertility;
    return left_fertility != right_fertility ? left_fertility < right_fertility
                                             : left.row < right.row;
  });
  for (const Candidate& candidate : standing) {
    FieldRow& field = current.fields.rows[candidate.row];
    ManureDoseSeam& seam = field.manure_dose;
    const UnitRow& heap = current.units.rows[FindRow(current.units, seam.from)];
    const HaulRate rate = RateToward(
        config, current, heap.position, field.center, TravelMode::kCart, config.manure_resource);
    const float whole = CartDaysFor(config, seam.due, rate);
    if (!(days_bought > 0.0F) || !(whole > 0.0F)) {
      // A dose the oats cannot buy tomorrow waits in the heap: the windows'
      // line (TractionWatch::manure_waits_for_oats).
      current.traction_watch.manure_waits_for_oats = 1;
      continue;
    }
    // The part the oats buy: the dose's grams by the share of its cart-days
    // they cover (a whole-trip model may round a part's own trips; the error
    // is a part of one trip).
    const float share = std::min(days_bought / whole, 1.0F);
    seam.priced = share < 1.0F ? GramsFromFloat(static_cast<float>(seam.due) * share) : seam.due;
    seam.days_remaining = whole * share;
    seam.days_written = seam.days_remaining;
    days_bought -= seam.days_remaining;
    if (share < 1.0F) {
      current.traction_watch.manure_waits_for_oats = 1;
    }
  }
}

void SettleManureDoses(const ProductionConfig& config, WorldState& current) {
  for (std::uint32_t row = 0; row < current.fields.rows.size(); ++row) {
    FieldRow& field = current.fields.rows[row];
    ManureDoseSeam& seam = field.manure_dose;
    if (!ManureDoseStands(field)) {
      continue;
    }
    const std::uint32_t heap_row = FindRow(current.units, seam.from);
    if (heap_row == kNoRow) {
      seam = ManureDoseSeam{};
      continue;
    }
    UnitRow& heap = current.units.rows[heap_row];
    const float done =
        seam.days_written > seam.days_remaining ? seam.days_written - seam.days_remaining : 0.0F;
    float converted = 1.0F;
    if (done > 0.0F && seam.days_written > 0.0F) {
      const float share = std::min(done / seam.days_written, 1.0F);
      // Of TONIGHT'S PRICE (C2.2): the part the oats bought, not the whole due.
      const Grams budget = GramsFromFloat(static_cast<float>(seam.priced) * share);
      const Grams offered = std::min(budget, StockOf(heap.stock, config.manure_resource));
      // Out of the heap and onto the field: the dose lies on the snow until
      // the furrow turns it in (OpenPlowing), so it enters no stock.
      const Grams moved = -AddToStock(heap.stock, config.manure_resource, -offered);
      seam.due = std::max<Grams>(seam.due - moved, 0);
      seam.priced = std::max<Grams>(std::min(seam.priced - moved, seam.due), 0);
      // The winter's hold's book: the manure's own cart-days, as drained.
      current.ledger.current.manure_cart_days +=
          budget > 0 ? done * std::min(static_cast<float>(moved) / static_cast<float>(budget), 1.0F)
                     : 0.0F;
      current.ledger.current.manure_carted += moved;
      WriteAppliedShare(config, field);
      const HaulRate rate = RateToward(
          config, current, heap.position, field.center, TravelMode::kCart, config.manure_resource);
      // ITS TRIPS RIDE THE FIELD'S SOURCE (CartLoadSource::kField): a source
      // of its own would be a save change for a print; manure_carted holds
      // its tonnes apart.
      BookCartRun(config, current, CartLoadSource::kField, rate, moved);
      WorkAssignment hauled;
      hauled.kind = WorkKind::kHauling;
      hauled.field = current.fields.row_ids[row];
      BookHauled(current.ledger.current,
                 config.manure_resource,
                 moved,
                 FootShareOf(current, hauled, SettlementHasCarts(current, config.horse_kind)));
      if (budget > 0) {
        converted = std::min(static_cast<float>(moved) / static_cast<float>(budget), 1.0F);
      }
      SpendHaulNotes(current, hauled, converted);
    }
    // What was drained and moved is settled; what the heap could not give
    // stays drained for the next hour (the rick's supply's rule).
    seam.days_written = seam.days_remaining + (done * (1.0F - converted));
    if (seam.due == 0) {
      seam.days_remaining = 0.0F;
      seam.days_written = 0.0F;
    }
  }
}

void CancelLateManure(WorldState& current, FieldRow& field) {
  if (field.manure_dose.from.value == kInvalidEntityIdValue) {
    return;
  }
  // What is still due never left the heap: it stays there, and is counted.
  current.ledger.current.manure_late += field.manure_dose.due;
  field.manure_dose = ManureDoseSeam{};
}

}  // namespace core
