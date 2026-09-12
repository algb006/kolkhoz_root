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
#include "core_common/work_seam.h"
#include "core_common/world_state.h"
#include "field_haul.h"
#include "production_config.h"
#include "stock_ops.h"

namespace core {
namespace {

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
      // What the manger TOOK, not what it was offered. The two used to be
      // assumed equal and the function answered a flat 0 — "nothing was left
      // over" — for a cut that had not been stored at all: an unnamed
      // hay_resource makes the branch above true by two 0xFFFF sentinels
      // comparing equal, and AddToStock then writes nothing (0.17.79).
      return amount - AddToStock(current.units.rows[manger].stock, resource, amount);
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
  // THE PLAN NO LONGER ACCRUES HERE, and its absence is the point of the
  // 2026-09-12 pass. A share of the reaping made the plan a function of the
  // harvest: a poor year asked for less, so every year was met and the
  // verdict on it could not fail. The district names its figure in
  // plan.csv and hands it down at the year's turn (production_system.cpp).
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
  // AND THE WEEDS GO WITH THE FIRST FURROW. "Одна вспашка возвращает всё
  // назад. Ступень сбрасывается сразу" (farming design) — a black field
  // looks like a black field however many years it stood. The byte was set
  // at genesis and never cleared for one afternoon, so ground the village
  // had ploughed six times running went on reading overgrown from the road.
  field.overgrown = 0;
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
  // ASKED THROUGH THE SEAM'S OWN FUNCTIONS AND NOT BY A LOCAL BOOL. The
  // bool is how the numerator and this divisor came to describe different
  // horses for a day; they agree now because they read the same two
  // functions, which is a stronger thing than agreeing because somebody kept
  // them in step.
  //
  // AND KindOfPhase COMES FROM core_common/work_seam.h, where it already
  // was. The first draft of this repair wrote a second copy of it, here, in
  // the very edit whose subject is two places answering one question — and
  // work_seam.h's own header forbids that copy in as many words. The
  // analysis found it; I did not.
  const bool horse_pulled = IsHorseWork(KindOfPhase(phase));
  if (phase == FieldPhase::kPlowing) {
    norm = config.farming.plow_days_per_ha;
  } else if (phase == FieldPhase::kHarrowing) {
    norm = config.farming.harrow_days_per_ha;
  } else if (crop.value < config.crops.size()) {
    // AND SOWING IS NOT AMONG THEM, though it was from 0.17.87 to 2026-09-12.
    // Three places in this tree said it is hand work and one said it is not,
    // and the one was here: WorkKind::kSowing's own comment says "by hand in
    // Epoch I", IsHorseWork excludes it from the kinds a horse is harnessed
    // to, and labor_day sends the sower at walking speed and not at the
    // harness's. So a sower needed no horse, was capped by no horse and
    // walked to the field — and his work still got longer when the horses
    // were hungry.
    //
    // BOSS SETTLED IT BY THE REGISTRY — the design said "a seed drill or by
    // hand" and the equipment registry has no seed drill in any era, a
    // promise with nothing behind it (2026-09-12; architecture §8бн).
    //
    // THAT ARGUMENT PROVES TOO MUCH AND THE ANALYSIS SAID SO: the same
    // registry has no plough and no harrow either, and both of those are
    // horse work beyond dispute — the design keeps those in the start
    // inventory instead. Nor is the registry implement-free, which was this
    // block's first attempt at saying why: `horse_mower` sits in it as a
    // trailed implement. So the registry simply does not settle the
    // question either way.
    //
    // WHAT CARRIES THE CHANGE is the core's own three statements, which are
    // not silences: WorkKind::kSowing's comment, the crew cap that never
    // takes a horse for it, and the walking speed it is sent at.
    norm = phase == FieldPhase::kSowing ? config.crops[crop.value].sow_days_per_ha
                                        : config.crops[crop.value].harvest_days_per_ha;
  }
  // THE TRACTION RATION LENGTHENS THE WORK THE HORSE PULLS, and only that
  // work (boss's decision of 2026-09-12). Hay keeps a horse alive; fodder
  // grain makes it pull, and until today the model said "alive, therefore
  // full strength" — so unsealing the fodder fund cost the chairman nothing
  // at all. This is the price, and the chairman reads it off the sowing
  // calendar the same spring, which is the only place he could.
  //
  // NOT A STOP AT THE BOTTOM: an empty fodder fund makes the PLOUGHING
  // about two fifths longer — the norm divided by traction_hungry_factor,
  // 0.7, so 1.43 times — and it still finishes. A chairman who fed his
  // people out of the horses' grain loses a week, not a year: "никаких
  // безвыходных ситуаций" read forwards.
  //
  // THE SENTENCE HAS BEEN WRONG TWICE IN ONE EVENING and is worth the extra
  // line for it. It said "the SOWING" until 2026-09-12 and survived by an
  // hour the repair that took sowing out of this rule; then it said "half
  // again", which is 1.5 and not 1.43. Across the settlement's ploughing,
  // harrowing and sowing together the price is about a ninth — 184.5 to
  // 203.8 game man-days (reconciliation §16.5), because two of those three
  // are not divided at all.
  //
  // The harvest is left out on purpose: reaping is a scythe and a sickle in
  // Epoch I. Sowing likewise since 2026-09-12 — see the branch above.
  //
  // AND MOWING IS NOT LEFT OUT FOR THAT REASON, though this block said so
  // until the analysis checked it. The meadow cut HARNESSES A HORSE: the
  // registry carries `horse_mower` in Epoch I, labor_system sets
  // `job.harnessed` for meadow land, and the labour model says in as many
  // words that three works go out with traction — ploughing, harrowing and
  // THE HAY CUT. So mowing is pulled and is not lengthened, and the same is
  // true of a cart on the hauling.
  //
  // AND THEY CANNOT BE BROUGHT UNDER A KIND-KEYED RULE AT ALL, which is the
  // half this block missed at first: the meadow cut is not a WorkKind of its
  // own — its kind is kHarvest, and the horse is keyed off the LAND
  // (AssignmentJob::harnessed). A predicate over kinds that took it in would
  // drag the arable reaping in with it, and reaping is a sickle.
  //
  // NAMED AND NOT CHANGED: the hay cut is 229 man-days of the settlement's
  // year, and moving it under the rule would move every balance number in
  // the reconciliation. Whether an underfed horse mows slower is a design
  // question and boss's to answer (2026-09-12).
  //
  // THE NUMERATOR AND THIS CONSUMER MUST TRAVEL AS A PAIR, and for a day
  // they did not. herd_system sums the working share over IsHorseWork — two
  // kinds — while this divided the norms of three, so the two numbers were
  // plausible apart and described different horses together (architecture
  // §8вб). They read the same predicate now, through KindOfPhase, rather
  // than being kept in step by hand — but `AssignmentJob::harnessed` is a
  // THIRD way of saying "a horse is in this", and until all three are one
  // question this can open again.
  if (horse_pulled && config.farming.traction_hungry_factor > 0.0F) {
    const float factor = config.farming.traction_hungry_factor +
                         (1.0F - config.farming.traction_hungry_factor) * current.traction_ration;
    norm = factor > 0.0F ? norm / factor : norm;
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
    //
    // BUT A FIELD WITH NO CHAIN AT ALL IS NOT ON FALLOW — NOBODY HAS TOLD IT
    // ANYTHING. The player gives each field a chain of three seasons, crop or
    // fallow (farming design §7); a field that has never been ASSIGNED one is
    // the one meant here — FieldRow::rotation_assigned, and no longer inferred
    // from the slots, because since 2026-09-12 three empty slots on an
    // assigned field are three deliberate fallow years and are ploughed. The
    // start hands over ninety-three hectares that were never assigned at all. Ploughing them would
    // be the core making the player's decision for him — and it would cost the village about a
    // hundred and fifty man-days a year nobody asked for, which is defect D11
    // under a new name. It became reachable on 2026-09-12, when
    // LandKind::kDerelict went and that ground stopped being skipped whole.
    //
    // HasRotation asks it in one place for every asker (land_state.h).
    if (HasRotation(field) && month == config.farming.fallow_plow_month && temperature >= 0.0F) {
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
