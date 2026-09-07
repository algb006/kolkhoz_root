// The field half of the core_production boundary: everything that moves ONE
// field. The household half — the plan, the order book, the manure the
// settlement spreads, the walk over every field — stayed in
// production_system.cpp, and calls into this file without being called back.
// The seam and the reason for it are in field_work.h.

#include "field_work.h"

#include <cassert>
#include <cstddef>
#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/emit_event.h"
#include "core_common/haul.h"
#include "core_common/ledger_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "field_haul.h"
#include "production_config.h"
#include "stock_ops.h"

namespace core {
namespace {

/// @brief Is this one of the six bread grains the plan counts?
bool IsPlanGrain(const ProductionConfig& config, ResourceId resource) {
  for (const ResourceId grain : config.plan_grain_resources) {
    if (grain.value == resource.value) {
      return true;
    }
  }
  return false;
}

/// @brief Puts a harvested load where it belongs: hay at the manger, the
/// rest through the store door — none of them above its ceiling (task A3,
/// manual/72-storage-and-alarms.md §2).
/// @return What did NOT fit, in grams. The caller decides what that means:
///         a field keeps it (FieldRow::reaped_grams), a meadow's hay has
///         nowhere else and is booked to the year's lost_no_room.
Grams DeliverHarvest(const ProductionConfig& config,
                     WorldState& current,
                     ResourceId resource,
                     Grams amount) {
  Grams placed = 0;
  if (resource.value == config.hay_resource.value) {
    // The manger first, and it is not a numbered store: the stock yard's
    // table capacity is in HEADS, so the door does not find it and its
    // fodder buffer has no tonnage to be full against.
    const std::uint32_t manger = FindStockYardRow(current, config);
    if (manger != kNoRow) {
      AddToStock(current.units.rows[manger].stock, resource, amount);
      return 0;
    }
  }
  placed = DeliverToStores(current, config, resource, amount);
  return amount - placed;
}

/// The season's cut. The yield is the land's own rate for the WHOLE
/// season, which is why one cut a year is not a simplification: the
/// second cut is inside the number (farming.csv, meadow_yield_kg_per_ha).
void MowMeadow(const ProductionConfig& config, WorldState& current, FieldRow& field) {
  const float rate = field.kind == LandKind::kFloodplainMeadow
                         ? config.farming.meadow_floodplain_yield_kg_per_ha
                         : config.farming.meadow_yield_kg_per_ha;
  const Grams hay = KilogramsToGrams(rate * field.area_ga);
  // A meadow has no reaped buffer of its own: the cut either reaches the
  // manger and the stores or it is lost, and either way it is booked.
  const Grams hay_lost = DeliverHarvest(config, current, config.hay_resource, hay);
  AddLedgerAmount(current.ledger.current.harvest, config.hay_resource, hay);
  AddLedgerAmount(current.ledger.current.lost_no_room, config.hay_resource, hay_lost);
  current.ledger.current.area_harvested_ha += field.area_ga;
  field.work_days_remaining = 0.0F;
  // THE DAY IT WAS CUT, and it is the only trace the cut leaves. The phase
  // goes straight back to kGrowing below — the grass does stand again —
  // so without this day a mown meadow and an untouched one are the same
  // state, and the layer had nowhere to put a flower or a butterfly.
  field.last_mown_day = current.calendar.day;
  MoveFieldPhase(current, field, FieldPhase::kGrowing);  // the grass stands again next summer
}

void Harvest(const ProductionConfig& config,
             WorldState& current,
             FieldRow& field,
             const CropDef& crop) {
  const float soil_factor = field.fertility / config.farming.fertility_neutral;
  // The sum of the two, capped exactly where the single number was.
  const float stress_total = field.drought_stress + field.wet_stress;
  const float capped =
      stress_total > config.farming.stress_cap ? config.farming.stress_cap : stress_total;
  const float weather_factor = 1.0F - capped;
  const auto yield_grams =
      GramsFromKilograms(crop.yield_kg_per_ha * field.area_ga * soil_factor * weather_factor);
  // THE REAPED CROP STAYS ON THE FIELD. Until task A4 it went into the
  // stores in the same tick it was cut — the instant-delivery stub — and
  // only the remainder that would not fit stayed out. It all stays out
  // now: the field brigade's buffer of the transport design §9, and it
  // empties when somebody comes for it with a back or a cart
  // (SettleHauling). Nothing is lost here and nothing is forced in above
  // a ceiling; what the field gave is booked below either way.
  const Grams unplaced = yield_grams;
  if (unplaced > 0) {
    // A buffer already holding LAST year's produce of another crop cannot
    // hold this one too — one number names one resource. The old load has
    // stood a full season by now, so it is written off, loudly, rather
    // than silently relabelled as this year's.
    if (field.reaped_grams > 0 && field.reaped_resource.value != crop.resource.value) {
      AddLedgerAmount(
          current.ledger.current.lost_no_room, field.reaped_resource, field.reaped_grams);
      field.reaped_grams = 0;
      field.reaped_resource = ResourceId{};  // the invariant: empty means unnamed
    }
    field.reaped_grams += unplaced;
    field.reaped_resource = crop.resource;
  }
  // Booked whether or not a store took it in: what the field gave is what
  // the reconciliation compares against the yield tables, and a settlement
  // with nowhere to put its grain is a different finding entirely.
  AddLedgerAmount(current.ledger.current.harvest, crop.resource, yield_grams);
  current.ledger.current.area_harvested_ha += field.area_ga;
  // What this field gave, and of what: the three fields the kind's
  // contract names (event_state.h). Routine — a harvest is the year
  // working, not news — but the panel and the story layer both read the
  // journal, and a year of harvests that left no trace in it is a year
  // they cannot describe.
  SimEvent& reaped = EmitEvent(current, EventKind::kFieldHarvested);
  reaped.field = FieldIdOf(current, field);
  reaped.resource = crop.resource;
  reaped.amount = static_cast<std::int64_t>(yield_grams);
  // Straw is what the field leaves behind, and it is a feed of its own —
  // own and free, a reserve ration with a lowered effect but plainly there
  // in a winter manger (design db crop.straw_ratio).
  if (crop.straw_ratio > 0.0F) {
    const auto straw = GramsFromFloat(static_cast<float>(yield_grams) * crop.straw_ratio);
    const Grams straw_placed = DeliverToStores(current, config, config.straw_resource, straw);
    AddLedgerAmount(current.ledger.current.harvest, config.straw_resource, straw);
    // Straw has no buffer of its own — it is not why a field waits — so
    // what did not fit is gone, and gone with a line in the book.
    AddLedgerAmount(
        current.ledger.current.lost_no_room, config.straw_resource, straw - straw_placed);
  }
  // The district's plan accrues as the grain is reaped: it is "just a
  // number" in phase 1 (plan §11), a share of the year's own harvest,
  // handed over at the year's turn with no district mechanics behind it.
  if (config.plan_grain_share > 0.0F && IsPlanGrain(config, crop.resource)) {
    AddToStock(current.plan.due,
               crop.resource,
               GramsFromFloat(static_cast<float>(yield_grams) * config.plan_grain_share));
  }
  // Fertility bookkeeping (§2, §7, §8): the crop's delta, the manure
  // bonus, the growing repeat penalty.
  if (crop.is_perennial) {
    // A standing meadow is one sowing cut year after year, not a repeat
    // of that sowing: the rotation penalty must not accrue on it.
    field.repeat_years = 0;
  } else if (field.crop.value == field.last_crop.value) {
    field.repeat_years =
        field.repeat_years < 250 ? static_cast<std::uint8_t>(field.repeat_years + 1) : 250;
  } else {
    field.repeat_years = 0;
  }
  // The repeat penalty has a ceiling (boss answer Q3): the third year in a
  // row is the limit of the punishment, so a rotation mistake costs the
  // year and never the game. Uncapped it took a monocropped field from 65
  // to 2 in six years while the manure heap was still working.
  const auto repeated = static_cast<float>(field.repeat_years);
  const float charged = repeated < config.farming.repeat_penalty_max_years
                            ? repeated
                            : config.farming.repeat_penalty_max_years;
  field.fertility += crop.fertility_delta + ManureBonus(config, field) -
                     charged * config.farming.repeat_penalty_per_year;
  // And a floor under it: an exhausted field bears little, but it bears.
  const float floor_value = config.farming.fertility_floor;
  field.fertility = field.fertility < floor_value ? floor_value : field.fertility;
  field.fertility = field.fertility > 100.0F ? 100.0F : field.fertility;
  field.manure_applied = 0;
  field.last_crop = field.crop;
  ClearFieldWeather(field);
  // The reaping is over, so the PHASE seam is cleared — without this the
  // field never leaves kHarvest and the whole rotation stops, which is
  // what happened for one measured run when this line was swallowed by an
  // edit to the lines around it.
  field.work_days_remaining = 0.0F;
  // The load names the price of CARRYING it at once, in its own seam. It
  // has to be named here and not left to the evening: the first settlement
  // works out what was carried from what is missing from this number, and
  // a number nobody set reads as a full day's work — the instant-delivery
  // stub coming back in through the accounting, which is exactly what an
  // instrumented run caught it doing.
  if (field.reaped_grams > 0) {
    const Grams room = ReceivableRoom(config, current, field.reaped_resource);
    field.haul_days_remaining = HaulDaysFor(room < field.reaped_grams ? room : field.reaped_grams,
                                            FieldHaulRate(config, current, field),
                                            config.standard_day_hours);
  } else {
    field.haul_days_remaining = 0.0F;
  }
  // And the settlement's baseline with it. The guard above stopped the
  // FIRST evening from reading an unset number as a day's work; the same
  // reading came back through the other door, because the evening measured
  // today's demand against yesterday's leftover and the room grows every
  // day as the village eats (seventh reconciliation pass).
  field.haul_days_written = field.haul_days_remaining;
  if (crop.is_perennial && field.rotation_year1.value == field.crop.value) {
    MoveFieldPhase(current, field, FieldPhase::kGrowing);  // the stand yields again
    return;
  }
  // A perennial whose next slot is something else ends at this cut, not at
  // the year's turn: the field must be free in the autumn for the winter
  // crop the canon's ring puts after grass ("fallow or grass, then winter
  // rye"). Left standing till January it could only be sown a year late.
  if (crop.is_perennial) {
    field.last_crop = field.crop;
  }
  field.crop = CropId{};
  MoveFieldPhase(current, field, FieldPhase::kIdle);
}

/// @brief Opens the ploughing for `crop` (invalid = bare fallow). The
/// manure, if the winter's plan gave this field any, is already on the
/// row (PlanManure) and goes in with the plough (§8).
void OpenPlowing(const ProductionConfig& config,
                 WorldState& current,
                 FieldRow& field,
                 CropId crop) {
  field.crop = crop;
  if (field.manure_applied != 0) {
    const float share = static_cast<float>(field.manure_applied) / 100.0F;
    const auto dose =
        GramsFromKilograms(config.farming.manure_norm_kg_per_ha * field.area_ga * share);
    current.ledger.current.manure_plowed_in += dose;
    current.ledger.current.area_manured_ha += field.area_ga * share;
  }
  OpenPhase(config, current, field, FieldPhase::kPlowing);
}

}  // namespace

void ClearFieldWeather(FieldRow& field) {
  field.drought_stress = 0.0F;
  field.wet_stress = 0.0F;
  field.drought_run_days = 0;
  field.wet_run_days = 0;
  field.weather_state = FieldWeatherState::kNone;
}

FieldId FieldIdOf(const WorldState& current, const FieldRow& field) {
  const auto index = static_cast<std::size_t>(&field - current.fields.rows.data());
  assert(index < current.fields.row_ids.size());
  return index < current.fields.row_ids.size() ? current.fields.row_ids[index] : FieldId{};
}

void MoveFieldPhase(WorldState& current, FieldRow& field, FieldPhase phase) {
  if (field.phase == phase) {
    return;  // a phase that did not change is not news
  }
  field.phase = phase;
  SimEvent& event = EmitEvent(current, EventKind::kFieldPhaseChanged);
  event.field = FieldIdOf(current, field);
  event.amount = static_cast<std::int64_t>(phase);
}

void OpenPhase(const ProductionConfig& config,
               WorldState& current,
               FieldRow& field,
               FieldPhase phase) {
  MoveFieldPhase(current, field, phase);
  if (field.kind != LandKind::kArable) {
    // Grass is mown, never ploughed, harrowed or sown: the meadow has one
    // working phase in the year and one norm to size it.
    field.work_days_remaining = phase == FieldPhase::kHarvest
                                    ? config.farming.meadow_mow_days_per_ha * field.area_ga
                                    : 0.0F;
    return;
  }
  const CropId crop = field.crop;
  float norm = 0.0F;
  if (phase == FieldPhase::kPlowing) {
    norm = config.farming.plow_days_per_ha;
  } else if (phase == FieldPhase::kHarrowing) {
    norm = config.farming.harrow_days_per_ha;
  } else if (crop.value < config.crops.size()) {
    norm = phase == FieldPhase::kSowing ? config.crops[crop.value].sow_days_per_ha
                                        : config.crops[crop.value].harvest_days_per_ha;
  }
  field.work_days_remaining = norm * field.area_ga;
}

float ManureBonus(const ProductionConfig& config, const FieldRow& field) {
  return config.farming.manure_fertility_bonus * static_cast<float>(field.manure_applied) / 100.0F;
}

void RunMeadow(const ProductionConfig& config,
               WorldState& current,
               FieldRow& field,
               std::uint8_t month) {
  if (field.phase != FieldPhase::kGrowing) {
    return;  // already being mown, and one cut a year is all there is
  }
  if (month == config.farming.meadow_cut_month && current.calendar.date.day_in_month == 0) {
    OpenPhase(config, current, field, FieldPhase::kHarvest);
  }
}

void TrySowWinter(const ProductionConfig& config,
                  WorldState& current,
                  FieldRow& field,
                  std::uint8_t month,
                  float temperature) {
  if (field.rotation_year1.value >= config.crops.size()) {
    return;
  }
  const CropDef& next = config.crops[field.rotation_year1.value];
  if (!next.is_winter || month < next.sow_from_month || month > next.sow_to_month ||
      temperature < next.sow_min_temp_c) {
    return;
  }
  OpenPlowing(config, current, field, field.rotation_year1);
}

void TrySow(const ProductionConfig& config,
            WorldState& current,
            FieldRow& field,
            std::uint8_t month,
            float temperature) {
  if (field.rotation_year0.value >= config.crops.size()) {
    // A FALLOW YEAR IS PLOUGHED (farming design §7, "fallow is ploughed";
    // defect D11 of the reconciliation): the manure goes in with the
    // plough and the ground stands bare until the year turns, or until the
    // next slot's winter crop goes into it in the autumn (TrySowWinter).
    if (month == config.farming.fallow_plow_month && temperature >= 0.0F) {
      OpenPlowing(config, current, field, CropId{});
    }
    return;
  }
  const CropDef& crop = config.crops[field.rotation_year0.value];
  if (crop.is_winter && field.last_crop.value == field.rotation_year0.value) {
    // This year's winter crop was sown last autumn and is already off:
    // the field is idle because it was HARVESTED, not because the sowing
    // was missed. Sowing it again in August would put the same rye in two
    // years running and eat the next slot with it — the field sheet caught
    // exactly that. Only the next slot's winter crop may go in now.
    TrySowWinter(config, current, field, month, temperature);
    return;
  }
  // Otherwise a winter crop in THIS year's slot is the fallback path: it
  // was meant to go in last autumn (TrySowWinter) and that autumn was
  // missed, so it is sown in its window a year late. That costs the slot
  // after it, but a winter crop never sown costs the plan its bread.
  if (month < crop.sow_from_month || month > crop.sow_to_month ||
      temperature < crop.sow_min_temp_c) {
    TrySowWinter(config, current, field, month, temperature);
    return;
  }
  OpenPlowing(config, current, field, field.rotation_year0);
}

void FinishSowing(const ProductionConfig& config, WorldState& current, FieldRow& field) {
  const CropId crop_id = field.crop;
  if (crop_id.value == kInvalidDefIdValue) {
    // Bare fallow: ploughed and harrowed, nothing goes in. It stands as
    // ground with no crop until the year turns or a winter crop takes it.
    MoveFieldPhase(current, field, FieldPhase::kGrowing);
    field.work_days_remaining = 0.0F;
    return;
  }
  if (crop_id.value < config.crops.size()) {
    const CropDef& crop = config.crops[crop_id.value];
    // Sowing consumes ordinary produce of the same crop (§7); partial
    // seed sows the whole field anyway — the shortfall alarm is a UI
    // concern.
    if (crop.sowing_norm_kg_per_ha > 0.0F) {
      const auto need = GramsFromKilograms(crop.sowing_norm_kg_per_ha * field.area_ga);
      const Grams got = TakeFromStorage(current, config, crop.resource, need);
      AddLedgerAmount(current.ledger.current.seed, crop.resource, got);
      // Short seed sows the whole field anyway, and the player is told by
      // kSeedShort — standing from the day the rotation is set, not on the
      // morning of the sowing (task A3; farming design §7). Phase code does
      // not log (DEADLOCK-001).
    }
  }
  current.ledger.current.area_sown_ha += field.area_ga;
  field.crop = crop_id;
  MoveFieldPhase(current, field, FieldPhase::kGrowing);
  field.work_days_remaining = 0.0F;
  ClearFieldWeather(field);
}

void FinishHarvest(const ProductionConfig& config, WorldState& current, FieldRow& field) {
  if (field.kind != LandKind::kArable) {
    MowMeadow(config, current, field);
    return;
  }
  if (field.crop.value >= config.crops.size()) {
    MoveFieldPhase(current, field, FieldPhase::kIdle);
    return;
  }
  Harvest(config, current, field, config.crops[field.crop.value]);
}

}  // namespace core
