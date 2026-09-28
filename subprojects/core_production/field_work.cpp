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
#include "core_common/crop_calendar.h"
#include "core_common/emit_event.h"
#include "core_common/haul.h"
#include "core_common/ledger_state.h"
#include "core_common/rain_stops_work.h"
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

}  // namespace

void LayMownShare(const ProductionConfig& config, WorldState& current, FieldRow& field) {
  if (field.kind == LandKind::kArable || field.phase != FieldPhase::kHarvest) {
    return;
  }
  // THE SHARE MOWN, off the labour, as the arable's reaping lays it
  // (LayReapedShare; boss-core-epoch1-queue-2026-09-28 [1], 0.37.11): until
  // 0.37.11 the meadow laid its whole season in the day its last hour was
  // mown, and on host's seed 7 thirty tonnes waited from October to December
  // behind 0.29 man-days left on one meadow — the wintering's feed days read
  // 17 against 20, the hay cut but not in the book.
  const float phase_days = PhaseTotalDays(config, current, field);
  float cut = phase_days > 0.0F ? 1.0F - (field.work_days_remaining / phase_days) : 1.0F;
  cut = cut < 0.0F ? 0.0F : (cut > 1.0F ? 1.0F : cut);
  if (field.work_days_remaining <= 0.0F) {
    cut = 1.0F;  // mown through: the last of it, whatever the arithmetic says
  }
  const float share = cut - field.harvest_laid_share;
  if (!(share > 0.0F)) {
    return;
  }
  // The land's own rate for the WHOLE season, which is why one cut a year is
  // not a simplification: the second cut is inside the number (farming.csv,
  // meadow_yield_kg_per_ha).
  const float rate = field.kind == LandKind::kFloodplainMeadow
                         ? config.farming.meadow_floodplain_yield_kg_per_ha
                         : config.farming.meadow_yield_kg_per_ha;
  const Grams hay_total = KilogramsToGrams(rate * field.area_ga);
  // The last lay takes exactly what is left, so the season lays to the gram.
  const Grams hay =
      cut >= 1.0F
          ? (hay_total > field.harvest_laid_grams ? hay_total - field.harvest_laid_grams : 0)
          : GramsFromFloat(static_cast<float>(hay_total) * share);
  field.harvest_laid_share = cut;
  field.harvest_laid_grams += hay;
  current.ledger.current.area_harvested_ha += field.area_ga * share;
  if (hay <= 0) {
    return;
  }
  // STRAIGHT TO THE MANGER AND THE STORES, as the whole cut always went: a
  // meadow has no reaped buffer of its own, so the share adds no load to the
  // carting. What finds no room is lost, and either way it is booked.
  const Grams hay_lost = DeliverHarvest(config, current, config.hay_resource, hay);
  AddLedgerAmount(current.ledger.current.harvest, config.hay_resource, hay);
  AddLedgerAmount(current.ledger.current.lost_no_room, config.hay_resource, hay_lost);
}

namespace {

/// The season's cut ends: the last share laid, the meadow back to grass.
void MowMeadow(const ProductionConfig& config, WorldState& current, FieldRow& field) {
  // The last of the cut, whatever the days before it laid (LayMownShare).
  field.work_days_remaining = 0.0F;
  LayMownShare(config, current, field);
  field.harvest_laid_share = 0.0F;
  field.harvest_laid_grams = 0;
  field.harvest_work_days = 0.0F;  // nought outside a reaping (land_state.h)
  // THE DAY IT WAS CUT, and it is the only trace the cut leaves. The phase
  // goes straight back to kGrowing below — the grass does stand again —
  // so without this day a mown meadow and an untouched one are the same
  // state, and the layer had nowhere to put a flower or a butterfly.
  field.last_mown_day = current.calendar.day;
  MoveFieldPhase(current, field, FieldPhase::kGrowing);  // the grass stands again next summer
}

/// What a late sowing costs this field: 1.0 sown inside its window, falling by
/// `late_sowing_yield_loss_per_day` for every day past it, never below the
/// floor.
///
/// THE SLOPE IS THE PRICE OF A BAND NOBODY CHOOSES TO ENTER, which is why it is
/// a slope. The price used to be the snow taking the crop whole, and that was
/// wrong in kind rather than in size: the chairman hands out rotations, the
/// accountant decides when to sow, and it sows whatever it can whenever it can.
/// A total price on a band entered by nobody is a trap (measured: the opening
/// year sowed 66.5 hectares and lost 28 of them to snow). Diminishing returns
/// put the decision back where one exists — upstream, in how much land to
/// raise, which is what the sowing alarm points at.
///
/// A field with no sowing day — one genesis stood up, or a world loaded from
/// before the rule — is not late: it is not judged by a day nobody recorded.
float LateSowingFactor(const ProductionConfig& config, const FieldRow& field) {
  if (field.sown_day == kNeverSownDay || field.crop.value >= config.crops.size()) {
    return 1.0F;
  }
  return LateFactorOnDay(config, config.crops[field.crop.value], field.sown_day);
}

}  // namespace

float LateFactorOnDay(const ProductionConfig& config, const CropDef& def, SimDay sown_day) {
  if (def.is_winter || def.is_perennial) {
    return 1.0F;  // sown in another year's reckoning; its window is not this one
  }
  const auto last_sowing =
      static_cast<std::int32_t>(((def.sow_to_month + 1U) * kDaysPerMonth) - 1U);
  // THE FIRST SPRING IS FREE OF IT, and the design's own grace period is the
  // reason rather than a kindness invented here (difficulty §3, "Первая весна:
  // ни распутицы, ни разлива"). Its stated cause is our case word for word:
  // the mire "would eat exactly the weeks in which the farm has to be set up
  // and the first fields laid, and the player would sit out the first sowing
  // waiting for the road to dry, and lose a year for nothing".
  //
  // Replace the mire with this slope and the sentence does not change. In the
  // first year the chairman has NO land decision — the layout was handed to
  // him, not chosen — so a late sowing is not his mistake, and charging the
  // full price for a band nobody entered is the trap this whole rule exists to
  // remove. From the second spring on it is his.
  if (sown_day < kDaysPerYear) {
    return 1.0F;
  }
  const auto sown_on = static_cast<std::int32_t>(sown_day % kDaysPerYear);
  const std::int32_t late_days = sown_on - last_sowing;
  if (late_days <= 0) {
    return 1.0F;
  }
  const float factor =
      1.0F - (static_cast<float>(late_days) * config.farming.late_sowing_yield_loss_per_day);
  return factor < config.farming.late_sowing_yield_floor ? config.farming.late_sowing_yield_floor
                                                         : factor;
}

bool LateSowingReturnsItsSeed(const ProductionConfig& config,
                              const FieldRow& field,
                              CropId crop_id,
                              SimDay today) {
  if (crop_id.value >= config.crops.size()) {
    return true;
  }
  const CropDef& crop = config.crops[crop_id.value];
  if (!(crop.sowing_norm_kg_per_ha > 0.0F)) {
    return true;  // nothing goes into the ground to lose
  }
  // The soil the yield will read, at most 100 (SoilFertility, 0.37.13).
  const float soil_factor = SoilFertility(field) / config.farming.fertility_neutral;
  const float expected = crop.yield_kg_per_ha * soil_factor * LateFactorOnDay(config, crop, today);
  return expected >= crop.sowing_norm_kg_per_ha;
}

bool SowingHasSeed(const ProductionConfig& config, const WorldState& world, CropId crop_id) {
  if (crop_id.value >= config.crops.size()) {
    return true;
  }
  const CropDef& crop = config.crops[crop_id.value];
  if (!(crop.sowing_norm_kg_per_ha > 0.0F)) {
    return true;  // sown by labour alone
  }
  return TakeableGrams(world, config, crop.resource) > 0;
}

float SoilFertility(const FieldRow& field) {
  return field.fertility > 100.0F ? 100.0F : field.fertility;
}

void CapRestedFertility(const ProductionConfig& config, FieldRow& field) {
  const float ceiling = 100.0F + (field.manure_booked != 0 ? ManureBonus(config, field) : 0.0F);
  if (field.fertility > ceiling) {
    field.fertility = ceiling;
  }
}

Grams FieldYieldGrams(const ProductionConfig& config, const FieldRow& field, const CropDef& crop) {
  // At most 100 however far a paid dose lifts the row before its harvest
  // settles the cap (0.37.13; SoilFertility).
  const float soil_factor = SoilFertility(field) / config.farming.fertility_neutral;
  // The sum of the two, capped exactly where the single number was.
  const float stress_total = field.drought_stress + field.wet_stress;
  const float capped =
      stress_total > config.farming.stress_cap ? config.farming.stress_cap : stress_total;
  const float weather_factor = 1.0F - capped;
  // ON THE SOWN SHARE (FieldRow::sown_share): the part the seed did not
  // cover grows nothing.
  return GramsFromKilograms(crop.yield_kg_per_ha * field.area_ga * field.sown_share * soil_factor *
                            weather_factor * LateSowingFactor(config, field));
}

Grams StandingYieldGrams(const ProductionConfig& config,
                         const FieldRow& field,
                         const CropDef& crop) {
  const float standing = 1.0F - field.harvest_laid_share;
  return standing > 0.0F
             ? GramsFromFloat(static_cast<float>(FieldYieldGrams(config, field, crop)) * standing)
             : 0;
}

void LayReapedShare(const ProductionConfig& config, WorldState& current, FieldRow& field) {
  if (field.kind != LandKind::kArable || field.phase != FieldPhase::kHarvest ||
      field.crop.value >= config.crops.size()) {
    return;
  }
  const CropDef& crop = config.crops[field.crop.value];
  // THE SHARE CUT, off the labour: what is left against what the phase
  // costs — the number OpenPhase wrote, frozen since 0.36.20 (PhaseTotalDays,
  // harvest_work_days). One measure for every hand that drains it — the crew,
  // the MTS column, the avral.
  const float phase_days = PhaseTotalDays(config, current, field);
  float cut = phase_days > 0.0F ? 1.0F - (field.work_days_remaining / phase_days) : 1.0F;
  cut = cut < 0.0F ? 0.0F : (cut > 1.0F ? 1.0F : cut);
  if (field.work_days_remaining <= 0.0F) {
    cut = 1.0F;  // worked through: the last of it, whatever the arithmetic says
  }
  const float share = cut - field.harvest_laid_share;
  if (!(share > 0.0F)) {
    return;
  }
  const Grams yield_grams = FieldYieldGrams(config, field, crop);
  // The last lay takes exactly what is left of the whole, so the reaping
  // lays its yield to the gram however the days divided it.
  const Grams laid =
      cut >= 1.0F
          ? (yield_grams > field.harvest_laid_grams ? yield_grams - field.harvest_laid_grams : 0)
          : GramsFromFloat(static_cast<float>(yield_grams) * share);
  field.harvest_laid_share = cut;
  field.harvest_laid_grams += laid;
  current.ledger.current.area_harvested_ha += field.area_ga * share;
  if (laid <= 0) {
    return;
  }
  // THE REAPED CROP STAYS ON THE FIELD — the field brigade's buffer of the
  // transport design §9 (task A4), emptied by whoever comes for it with a
  // back or a cart (SettleHauling). A buffer already holding LAST year's
  // produce of another crop cannot hold this one too — one number names one
  // resource — so the old load, a full season old, is written off loudly.
  if (field.reaped_grams > 0 && field.reaped_resource.value != crop.resource.value) {
    AddLedgerAmount(current.ledger.current.lost_no_room, field.reaped_resource, field.reaped_grams);
    field.reaped_grams = 0;
    field.reaped_resource = ResourceId{};  // the invariant: empty means unnamed
    // And its carting's price goes with it (static review of 0.34.44): the
    // old load is gone, and its price standing beside the new one was
    // demand for a heap that no longer lies there.
    field.haul_days_remaining = 0.0F;
    field.haul_days_written = 0.0F;
  }
  const Grams heap_before = field.reaped_grams;
  field.reaped_grams += laid;
  field.reaped_resource = crop.resource;
  // Booked whether or not a store took it in: what the field gave is what
  // the reconciliation compares against the yield tables.
  AddLedgerAmount(current.ledger.current.harvest, crop.resource, laid);
  // Straw is what the cut leaves behind, a feed of its own (design db
  // crop.straw_ratio). It has no buffer — it is not why a field waits — so
  // what does not fit is gone, with a line in the book.
  if (crop.straw_ratio > 0.0F) {
    const auto straw = GramsFromFloat(static_cast<float>(laid) * crop.straw_ratio);
    const Grams straw_placed = DeliverToStores(current, config, config.straw_resource, straw);
    AddLedgerAmount(current.ledger.current.harvest, config.straw_resource, straw);
    AddLedgerAmount(
        current.ledger.current.lost_no_room, config.straw_resource, straw - straw_placed);
  }
  // THE CARTING'S PRICE GROWS BY THE SAME LOAD, in both its numbers. The
  // evening settlement reads what was carried as written less remaining
  // (field_haul.cpp, SettleLoad), so a load added to one and not the other
  // would read as carted, or as a day's work nobody did.
  //
  // AND ONLY BY THE ROOM THE OLD HEAP HAS NOT SPOKEN FOR (static review of
  // 0.34.44). The price standing already is the old heap's, capped at the
  // room; pricing the new part against the whole room again sent carters for
  // up to twice what the stores could take, and the evening credited only
  // the room — half the carting into a closed door, every reaping day.
  const Grams room = ReceivableRoom(config, current, field.reaped_resource);
  const Grams spoken_for = heap_before < room ? heap_before : room;
  const Grams room_left = room - spoken_for;
  const Grams priced = room_left < laid ? room_left : laid;
  if (priced > 0) {
    const float more =
        HaulDaysFor(priced, FieldHaulRate(config, current, field), config.standard_day_hours);
    field.haul_days_remaining += more;
    field.haul_days_written += more;
  }
}

void LoseFieldToSnow(const ProductionConfig& config,
                     WorldState& current,
                     FieldRow& field,
                     const CropDef& crop) {
  // WHAT THE REAPING CUT IS LAID FIRST (the harvest by parts, farming design
  // §6, 24 September 2026): the snow takes what still stands, not the day's
  // work. And the HEAP IS NOT TOUCHED here, nor by any snow since 0.37.11
  // (the decision of 13 September); the first flake took it until 0.34.44,
  // and the lying snow until 0.37.11.
  LayReapedShare(config, current, field);
  // THE STANDING CROP, BOOKED. Only the hectares and the heap were written
  // until 2026-09-18, and host found a seed's 150 t of potato in no column
  // (econ-host-lever-pass3 seq 35): nothing vanishes without a line. The
  // harvest's own estimate of what still stands, taken before the crop is
  // cleared.
  const Grams crop_lost = StandingYieldGrams(config, field, crop);
  const float area_lost = field.area_ga * (1.0F - field.harvest_laid_share);
  AddLedgerAmount(current.ledger.current.lost_to_snow, crop.resource, crop_lost);
  field.last_crop = field.crop;
  field.repeat_years = 0;
  field.crop = CropId{};
  MoveFieldPhase(current, field, FieldPhase::kIdle);
  field.work_days_remaining = 0.0F;
  ClearFieldWeather(field);
  field.manure_applied = 0;
  field.manure_booked = 0;                 // settled with the crop the snow took
  field.fertility = SoilFertility(field);  // and its dose's cap with it (0.37.13)
  current.ledger.current.area_lost_ha += area_lost;
  // THE PART DUG IS SAID TOO (static review of 0.34.44): a field reaped to
  // 70 % and then snowed on gave 7 t to the heap and the book, and the
  // journal knew only the 3 t it lost.
  if (field.harvest_laid_grams > 0) {
    SimEvent& reaped = EmitEvent(current, EventKind::kFieldHarvested);
    reaped.field = FieldIdOf(current, field);
    reaped.resource = crop.resource;
    reaped.amount = static_cast<std::int64_t>(field.harvest_laid_grams);
  }
  field.harvest_laid_share = 0.0F;
  field.harvest_laid_grams = 0;
  field.harvest_work_days = 0.0F;
  // AND HERE IT IS SAID. A comment that once stood where this was called
  // claimed the loss "is an event already — kFieldLost, emitted where the
  // events slot folds it". It was not: the kind had no emitter anywhere in
  // the core, and the sentence describing the emission outlived the emission
  // it described (boss, 2026-09-05). Snow on an unreaped field is the only
  // TOTAL loss of a harvest in the game, so it interrupts a fast-forward: the
  // player is entitled to see the day it happened, not the year's total.
  SimEvent& lost = EmitEvent(current, EventKind::kFieldLost, EventSeverity::kInterrupting);
  lost.field = FieldIdOf(current, field);
  // What and how much, as kFieldHarvested says them: the event carried
  // amount 0 until 2026-09-18, and the journal could not tell a lost strip
  // from a lost year.
  lost.resource = crop.resource;
  lost.amount = static_cast<std::int64_t>(crop_lost);
}

namespace {

void Harvest(const ProductionConfig& config,
             WorldState& current,
             FieldRow& field,
             const CropDef& crop) {
  // THE LAST OF THE REAPING, LAID (the harvest by parts, farming design §6,
  // 24 September 2026). Until 0.34.44 the whole yield was laid HERE, once,
  // when the phase finished: every day's cut stood uncounted until the last,
  // and a field dug to 96 % lay under the snow whole — seed 1939, 148 t of
  // potato under the snow and none in the barn. The heap, the book, the
  // straw and the carting's price are laid day by day (LayReapedShare); what
  // is left of them is laid now.
  LayReapedShare(config, current, field);
  // What this field gave, and of what: the three fields the kind's
  // contract names (event_state.h) — for the whole reaping, once. Routine —
  // a harvest is the year working, not news — but the panel and the story
  // layer both read the journal, and a year of harvests that left no trace
  // in it is a year they cannot describe.
  SimEvent& reaped = EmitEvent(current, EventKind::kFieldHarvested);
  reaped.field = FieldIdOf(current, field);
  reaped.resource = crop.resource;
  reaped.amount = static_cast<std::int64_t>(field.harvest_laid_grams);
  field.harvest_laid_share = 0.0F;
  field.harvest_laid_grams = 0;
  field.harvest_work_days = 0.0F;
  // THE PLAN NO LONGER ACCRUES HERE, and its absence is the point of the
  // 2026-09-12 pass. A share of the reaping made the plan a function of the
  // harvest: a poor year asked for less, so every year was met and the
  // verdict on it could not fail. The district names its figure in
  // plan.csv and hands it down at the year's turn (production_system.cpp).
  // Fertility bookkeeping (§2, §7): the crop's delta and the growing repeat
  // penalty. The manure's bonus is not here: its furrow paid it before this
  // crop grew (OpenPlowing, 0.37.12), and here only its mark is cleared.
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
  field.fertility += crop.fertility_delta - charged * config.farming.repeat_penalty_per_year;
  // And a floor under it: an exhausted field bears little, but it bears.
  const float floor_value = config.farming.fertility_floor;
  field.fertility = field.fertility < floor_value ? floor_value : field.fertility;
  field.fertility = field.fertility > 100.0F ? 100.0F : field.fertility;
  field.manure_applied = 0;
  field.manure_booked = 0;  // settled at the harvest: the next dose books anew
  field.last_crop = field.crop;
  ClearFieldWeather(field);
  // The reaping is over, so the PHASE seam is cleared — without this the
  // field never leaves kHarvest and the whole rotation stops, which is
  // what happened for one measured run when this line was swallowed by an
  // edit to the lines around it.
  field.work_days_remaining = 0.0F;
  // THE CARTING'S PRICE is named as each part is laid (LayReapedShare), in
  // both its numbers — the written and the remaining — so the evening's
  // settlement never reads a load nobody priced as a day's work (seventh
  // reconciliation pass). It is not re-priced here.
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
  // The day the field gave its crop and went idle: the seed fund owes this
  // calendar year nothing more for it (land_state.h, reaped_day).
  field.reaped_day = current.calendar.day;
  MoveFieldPhase(current, field, FieldPhase::kIdle);
}

/// Where a preparation's work begins (OpenPlowing).
enum class PreparationStart : std::uint8_t {
  kGround,       ///< Idle ground: the plough, or the harrow on an autumn furrow.
  kBlackFallow,  ///< This year's black fallow: the harrow, its furrow and manure spent,
                 ///< its summer's rest owed to the sowing.
};

/// @brief Opens the ploughing for `crop` (invalid = bare fallow). The
/// manure, if the winter's plan gave this field any, is already on the
/// row (PlanManure) and goes in with the plough (§8).
void OpenPlowing(const ProductionConfig& config,
                 WorldState& current,
                 FieldRow& field,
                 CropId crop,
                 PreparationStart start = PreparationStart::kGround) {
  // AN AUTUMN FURROW LEFT PART-TURNED BY THE TURN (option «г»; land_state.h,
  // autumn_furrowing): the rest it owes, read before the phase below writes
  // the full norm, and the mark spent — this is its spring.
  const bool part_turned = field.autumn_furrowing != 0 && field.phase == FieldPhase::kIdle;
  const float rest_owed = field.work_days_remaining;
  field.autumn_furrowing = 0;
  field.crop = crop;
  // No furrow of THIS preparation yet (land_state.h, furrow_day): the
  // plough's end sets it, and the turn's release reads it.
  field.furrow_day = kNoFurrowDay;
  // AND THE CHAIN'S FIRST SEASON IS SPENT HERE, which is the one place every
  // way of using a rotation passes through: this year's crop, a fallow year's
  // ploughing, and the autumn sowing of the next slot's winter crop all open
  // their work by this call. Until it happens the year's turn holds the chain
  // still (land_state.h, rotation_skips_turn), so a chairman's first named
  // crop cannot be carried away by a January that arrived while his field was
  // busy, or cold, or already sown.
  //
  // EXCEPT A WINTER CROP NAMED FIRST (0.36.10): its season is the year AFTER
  // the autumn it goes in, so its ploughing uses the chain without yet
  // reaching its year. The mark stands, and the turn that brings that year
  // holds the chain once more and spends it (production_system.cpp).
  const bool first_slot_winter =
      field.rotation_skips_turn != 0 && crop.value < config.crops.size() &&
      crop.value == field.rotation_year0.value && config.crops[crop.value].is_winter;
  if (!first_slot_winter) {
    field.rotation_skips_turn = 0;
  }
  // AND THE WEEDS GO WITH THE FIRST FURROW. "Одна вспашка возвращает всё
  // назад. Ступень сбрасывается сразу" (farming design) — a black field
  // looks like a black field however many years it stood. The byte was set
  // at genesis and never cleared for one afternoon, so ground the village
  // had ploughed six times running went on reading overgrown from the road.
  field.overgrown = 0;
  // THE MANURE GOES INTO THE BOOK ONCE A FIELD'S CYCLE (boss-core-epoch1-
  // queue [94] (2); land_state.h, manure_booked): the furrow that turns it in
  // books it, and it stays on the row until the harvest or the fallow's turn
  // settles it. A preparation let go at the turn carries it, paid, to the
  // next spring's plough; a winter crop on the black fallow stands on the
  // fallow's furrow, which booked it. Until 0.37.9 each of those booked it
  // again (manure_plowed_in, area_manured_ha).
  //
  // AND THE SAME FURROW PAYS IT (farming design, «Навоз платит культуре, под
  // которую запахан», boss 28 September 2026; 0.37.12): the bonus goes into
  // the fertility here, before the crop it goes under is reaped. Until 0.37.12
  // it was paid at the harvest AFTER the yield was counted (or at a bare
  // fallow's turn), so a manured field gave the same crop and the manure fed
  // the next one: on the canon the fallow's May manure reached the oats after
  // the rye it was ploughed in for (econ, fallow-chain.md §6.2).
  if (field.manure_applied != 0 && field.manure_booked == 0) {
    const float share = static_cast<float>(field.manure_applied) / 100.0F;
    const auto dose =
        GramsFromKilograms(config.farming.manure_norm_kg_per_ha * field.area_ga * share);
    current.ledger.current.manure_plowed_in += dose;
    current.ledger.current.area_manured_ha += field.area_ga * share;
    // NOT CAPPED HERE (boss-core-epoch1-queue-2026-09-29 [7]): the cap of 100
    // bites where the cycle ends, after the crop's delta, as it bit when the
    // bonus came at the harvest — min(100, f + manure + delta). Capped at the
    // furrow, a field near 100 under a crop with a negative delta lost up to
    // |delta| a cycle to where the clamp stood. Above 100 only while the paid
    // dose is on the row (manure_booked); the yield reads at most 100
    // (FieldYieldGrams).
    field.fertility += ManureBonus(config, field);
    field.manure_booked = 1;
  }
  // THE BLACK FALLOW'S FURROW IS THE WINTER CROP'S (0.37.6): opened at the
  // harrow, not ploughed a second time. And its summer's rest is the winter
  // crop's too, paid the day it is sown (FinishSowing; land_state.h,
  // fallow_rest_owed).
  field.fallow_rest_owed = start == PreparationStart::kBlackFallow ? 1U : 0U;
  if (start == PreparationStart::kBlackFallow) {
    OpenPhase(config, current, field, FieldPhase::kHarrowing);
    return;
  }
  // GROUND THAT CAME OUT OF THE AUTUMN BLACK OWES NO SPRING FURROW. The byte
  // is spent here, at the one call every way of using a rotation passes
  // through, so a field gets its free ploughing exactly once and the next
  // year's work opens normally.
  //
  // The manure above still goes in: it was dealt out at the year's turn onto
  // ground that had not been worked yet, and the harrow turns it under just as
  // the plough would have.
  if (field.autumn_plowed != 0) {
    field.autumn_plowed = 0;
    OpenPhase(config, current, field, FieldPhase::kHarrowing);
    return;
  }
  OpenPhase(config, current, field, FieldPhase::kPlowing);
  // The part the autumn turned is not ploughed twice: only the rest.
  if (part_turned && rest_owed > 0.0F && rest_owed < field.work_days_remaining) {
    field.work_days_remaining = rest_owed;
  }
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
  if (phase == FieldPhase::kSowing) {
    // THE DAY RIPENING IS COUNTED FROM, stamped WHERE THE SOWING OPENS and not
    // where it closes — because that is the same instant SowingMayOpen asked
    // its question about, and boss's rule is one inequality with the sowing
    // day on both sides: "день сева + срок вызревания ≤ последний день окна
    // уборки".
    //
    // Stamping it at FinishSowing instead put the two ends days apart: the
    // gate cleared a field on the day the crew STARTED, the seed landed
    // whenever the crew got through, and the ripening then ran from the later
    // day and missed the reaping the gate had just guaranteed. Measured with
    // the two ends split: the first year reaped 0.0375 Gkcal against 0.29, and
    // the plan failed twenty-seven years of thirty.
    field.sown_day = current.calendar.day;
  }
  field.work_days_remaining = PhaseWorkDays(config, current, field, phase);
  // The reaping's whole work, frozen (land_state.h, harvest_work_days).
  field.harvest_work_days = phase == FieldPhase::kHarvest ? field.work_days_remaining : 0.0F;
}

float PhaseTotalDays(const ProductionConfig& config,
                     const WorldState& current,
                     const FieldRow& field) {
  if (field.phase == FieldPhase::kHarvest && field.harvest_work_days > 0.0F) {
    return field.harvest_work_days;
  }
  return PhaseWorkDays(config, current, field, field.phase);
}

float PhaseWorkDays(const ProductionConfig& config,
                    const WorldState& current,
                    const FieldRow& field,
                    FieldPhase phase) {
  if (field.kind != LandKind::kArable) {
    // Grass is mown, never ploughed, harrowed or sown: the meadow has one
    // working phase in the year and one norm to size it.
    return phase == FieldPhase::kHarvest ? config.farming.meadow_mow_days_per_ha * field.area_ga
                                         : 0.0F;
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
  if (horse_pulled) {
    const float factor = TractionFactor(config, current.traction_ration);
    norm = factor > 0.0F ? norm / factor : norm;
  }
  // AND THE REAPED CROP CARRIED TO THE FIELD'S HEAP (0.36.20): part of the
  // reaping, booked as harvest, derived from the field and the transport
  // (field_haul.h, CarryToHeapDays) — not a norm. Until 0.36.20 the reaping's
  // norm laid the whole yield at the heap on the field's edge (0.36.15) with
  // no one carrying it there.
  float carry = 0.0F;
  if (phase == FieldPhase::kHarvest && crop.value < config.crops.size()) {
    carry = CarryToHeapDays(
        config, current, field, FieldYieldGrams(config, field, config.crops[crop.value]));
  }
  return (norm * field.area_ga) + carry;
}

float TractionFactor(const ProductionConfig& config, float traction_ration) {
  const float hungry = config.farming.traction_hungry_factor;
  if (!(hungry > 0.0F)) {
    return 1.0F;
  }
  return hungry + ((1.0F - hungry) * traction_ration);
}

void RescaleHorseWorkForRation(const ProductionConfig& config,
                               float traction_ration_was,
                               WorldState& current) {
  if (traction_ration_was == current.traction_ration) {
    return;
  }
  const float was = TractionFactor(config, traction_ration_was);
  const float now = TractionFactor(config, current.traction_ration);
  if (!(was > 0.0F) || !(now > 0.0F)) {
    return;
  }
  // Days go as one over the pull: priced at `was`, worked at `now`.
  const float scale = was / now;
  for (FieldRow& field : current.fields.rows) {
    // And the rest a part-turned autumn furrow owes over the winter (option
    // «г»): idle, it is still plough work, priced at today's pull — or the
    // spring would compare a December price with a spring norm (static review
    // of 0.37.18).
    const bool part_turned = field.phase == FieldPhase::kIdle && field.autumn_furrowing != 0;
    if (field.kind == LandKind::kArable && (IsHorseWork(KindOfPhase(field.phase)) || part_turned)) {
      field.work_days_remaining *= scale;
    }
  }
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

std::int32_t RipenDays(const ProductionConfig& config, CropId crop) {
  if (crop.value >= config.crops.size()) {
    return 0;
  }
  const CropDef& def = config.crops[crop.value];
  // Reaped in another year: the gap runs backwards and says nothing. The
  // arithmetic lives in core_common/crop_calendar.h, where labor reads it too.
  return RipenGapDays(def.sow_to_month, def.harvest_from_month, def.is_winter || def.is_perennial);
}

bool CropHasRipened(const ProductionConfig& config, const FieldRow& field, SimDay day) {
  const std::int32_t ripen = RipenDays(config, field.crop);
  if (ripen == 0) {
    return true;  // winter or perennial: ruled by its windows, as it always was
  }
  if (field.sown_day == kNeverSownDay) {
    // A CROP STANDING WITH NO SOWING DAY HAS ALREADY RIPENED, and the first
    // draft of this line said the opposite "to be safe". It was the unsafe
    // direction and the run said so at once: genesis hands the village fields
    // with the crop already in the ground, none of which carries a day, so
    // they became unreapable FOR EVER — the first year's harvest fell from
    // 0.29 Gkcal to 0.0375 and the plan failed twenty-seven years of thirty.
    //
    // The honest reading is the other one: a stand whose sowing this core
    // never saw has been there at least as long as the core has been counting,
    // which is longer than any ripening. Same for a world loaded from a save
    // older than this rule — it keeps the calendar-only behaviour it was saved
    // under, for the one spell it is already in, and every sowing after that
    // carries a real day.
    return true;
  }
  return static_cast<std::int64_t>(day) - static_cast<std::int64_t>(field.sown_day) >= ripen;
}

bool ReleaseUnsownPreparation(WorldState& current, FieldRow& field) {
  // AN AUTUMN FURROW NOT FINISHED BY THE TURN KEEPS ITS WORK (boss-core-
  // epoch1-queue-2026-09-29 [32], option «г»): the field goes idle with the
  // mark still on it and the rest owed in `work_days_remaining`, and the
  // spring's own ploughing (OpenPlowing) opens it and ploughs only that rest.
  // The first draft let it go — «полборозды — не зябь» — and with the zyab
  // opening only after the potato was carted (option «б»), 6 577 ha of
  // furrows begun in November were thrown away at the turn on 138 of the
  // canon's 180 year-ends, against 1 260 ha of zyab finished (core [31]).
  // The second kept it UNDER THE PLOUGH across the turn and it was ploughed
  // from the 1st of January on frozen ground, released the spring's oats to
  // the herds in the winter, and missed the turn's manure plan (it deals to
  // idle fields): hungry family-days +29 %, the potato's harvest −24 %, its
  // fields sown at fertility 59.5 against 83.0 (the static review of «г»,
  // and a sowing trace). Idle, it is none of that: the winter is the
  // winter's, the manure is dealt, and the spring opens it as any spring
  // furrow — on a thaw, in its window, eating the spring's oats. A finished
  // one is idle already, `autumn_plowed`.
  if (field.autumn_furrowing != 0) {
    if (field.phase == FieldPhase::kPlowing) {
      field.furrow_day = kNoFurrowDay;
      MoveFieldPhase(current, field, FieldPhase::kIdle);  // the rest stays owed
      return true;
    }
    // STILL PART-TURNED AT A SECOND TURN: the spring it was owed to passed
    // without opening it (its chain withdrawn, no chain to open it), and a
    // furrow a year and more old is no furrow — the next chain ploughs the
    // whole (static review of 0.37.18).
    field.autumn_furrowing = 0;
    field.work_days_remaining = 0.0F;
    return true;
  }
  const bool preparing =
      field.phase == FieldPhase::kPlowing || field.phase == FieldPhase::kHarrowing;
  if (!preparing || field.crop.value == kInvalidDefIdValue) {
    return false;
  }
  // ZYAB IS THE AUTUMN FURROW ON THE STUBBLE (boss-core-epoch1-queue [94]
  // (1); farming design: «Зябь — та же вспашка, только сразу после
  // уборки»): the furrow is kept only when this preparation's own plough
  // ended after the field's reaping of the same year. April's furrow for
  // potatoes left unsown, May's on the black fallow, a furrow on ground that
  // gave nothing this year — none is zyab for the next spring. Until 0.37.9
  // every harrowing let go at the turn became zyab; 0.37.9 kept it on any
  // furrow the preparation turned itself.
  const bool autumn_furrow = field.furrow_day != kNoFurrowDay &&
                             field.reaped_day != kNeverReapedDay &&
                             field.furrow_day / kDaysPerYear == field.reaped_day / kDaysPerYear &&
                             field.furrow_day >= field.reaped_day;
  if (field.phase == FieldPhase::kHarrowing && autumn_furrow) {
    field.autumn_plowed = 1;  // the furrow is turned; only the harrow is owed
  }
  field.furrow_day = kNoFurrowDay;
  // The black fallow's rest goes with its unsown winter crop: the turn has
  // already rested the field by its own rule (production_system.cpp).
  field.fallow_rest_owed = 0;
  field.crop = CropId{};
  MoveFieldPhase(current, field, FieldPhase::kIdle);
  field.work_days_remaining = 0.0F;
  return true;
}

bool ReapingMayOpen(const ProductionConfig& config,
                    const FieldRow& field,
                    std::uint8_t month,
                    SimDay day) {
  if (field.crop.value >= config.crops.size()) {
    return false;  // nothing standing, or a crop this build does not know
  }
  const CropDef& crop = config.crops[field.crop.value];
  if (month < crop.harvest_from_month) {
    return false;
  }
  // THE BACK EDGE HOLDS ONLY FOR WINTER AND PERENNIAL CROPS (UB-001, repaired
  // 2026-09-13). "Окно уборки говорит, когда убирать СЛЕДУЕТ, а не когда
  // МОЖНО" (farming design): a same-year crop sown late ripens late and STANDS
  // until reaped or taken by the snow (RunFields) — the third region of the
  // design, "за окном уборки, вызревание ДО СНЕГА". Until the repair it was
  // never opened at all, and the snow took a crop nobody was allowed to cut.
  // A winter crop's window is its calendar; a perennial is cut once a year on
  // its window's first day (RunFields, cut_today).
  //
  // The first attempt of the same morning was held back because the late
  // reaping outranks carting in the work queue (its deadline is already
  // past); it was measured then on the old yards and before the store limit
  // was understood. The measurement for this commit is in its message.
  const bool calendar_crop = crop.is_winter || crop.is_perennial;
  if (calendar_crop && month > crop.harvest_to_month) {
    return false;
  }
  return CropHasRipened(config, field, day);
}

bool SowingMayOpen(const ProductionConfig& config,
                   CropId crop,
                   std::uint8_t month,
                   std::uint32_t day_of_year,
                   float temperature) {
  if (crop.value >= config.crops.size()) {
    return false;  // bare fallow, or a crop this build does not know
  }
  const CropDef& def = config.crops[crop.value];
  // THE FRONT EDGE AND THE TEMPERATURE, AND DELIBERATELY NOT `sow_to_month`.
  // The back edge of the SOWING window is the crew's, not the calendar's: a
  // sowing that began in its window finishes when the hands finish it, and a
  // field that reached the harrow late is sown late rather than lost. Closing
  // this test on `sow_to_month` costs the canonical village its first harvest
  // and puts it on trial in the third year (production_system.cpp,
  // AdvanceFinishedPhases).
  if (month < def.sow_from_month || temperature < def.sow_min_temp_c) {
    return false;
  }
  // THE BACK EDGE IS A QUESTION ABOUT THE OUTCOME, not about the calendar:
  // "поле сеют, пока посеянное успевает вызреть; не успевает — НЕ СЕЮТ, и
  // семена остаются в фонде" (boss, 2026-09-13). Seed that cannot ripen in
  // time to be reaped is seed spent for nothing — the first snow takes an
  // unreaped annual whole, which is the one TOTAL loss of a harvest in the
  // game — so the field is left unsown instead: a readable lost year rather
  // than a silent loss of next spring's seed.
  //
  // AND THE LIMIT IS THE SNOW, NOT THE REAPING WINDOW. Boss's rule of
  // 2026-09-13, second redaction, and the correction is the whole of it:
  //
  //   THE HARVEST WINDOW SAYS WHEN A CROP SHOULD BE CUT. THE SNOW SAYS WHEN IT
  //   CAN NO LONGER BE CUT AT ALL.
  //
  // Grain that ripens after its window does not vanish — it STANDS, and it can
  // be cut late, worse, and at risk. Refusing the sowing at the window's edge
  // forbade the merely UNPROFITABLE rather than the impossible, and it showed:
  // with the edge there, the gambling band was four game days wide for oats
  // and not one hectare in twelve years went into the ground past its window.
  // A cost with no way to incur it is not a cost.
  //
  // So the band between the reaping window and the snow is open, and it is
  // exactly the gamble the design wants: sow late, cut late, and the first
  // snow may take the lot (production_system.cpp — the one TOTAL loss of a
  // harvest in the game). Past the snow there is nothing to gamble on, and the
  // seed stays in the fund.
  // A WINTER CROP HAS NO SOWING AFTER ITS WINDOW (fields design §4, «раньше и
  // позже нельзя»; §7, «Сева после окна нет»; boss, boss-core-epoch1-resume
  // [34], 0.36.16). The crew's back edge above is the spring crops': an
  // annual sown late still ripens before the snow or does not, and the snow
  // gate below says which. A winter crop has no such gate — its ripening is
  // next year's — so it passed here whenever its harrow finished, and the
  // host's trace sowed rye in NOVEMBER. Past the window the field waits
  // harrowed, the year's turn lets the preparation go, and the slot is lost
  // (question 278, WinterSlotLost).
  if (def.is_winter && month > def.sow_to_month) {
    return false;
  }
  const std::int32_t ripen = RipenDays(config, crop);
  if (ripen == 0) {
    return true;  // winter or perennial: reaped in another year, no gap to miss
  }
  return static_cast<std::int32_t>(day_of_year) + ripen <=
         static_cast<std::int32_t>(config.growing_season_last_day);
}

namespace {

/// Whether `slot` is a winter crop whose autumn window is open and the day
/// warm enough to begin it.
bool WinterWindowOpen(const ProductionConfig& config,
                      CropId slot,
                      std::uint8_t month,
                      float temperature) {
  if (slot.value >= config.crops.size()) {
    return false;
  }
  const CropDef& crop = config.crops[slot.value];
  return crop.is_winter && month >= crop.sow_from_month && month <= crop.sow_to_month &&
         temperature >= crop.sow_min_temp_c;
}

}  // namespace

void TrySowWinter(const ProductionConfig& config,
                  WorldState& current,
                  FieldRow& field,
                  std::uint8_t month,
                  float temperature) {
  if (WinterWindowOpen(config, field.rotation_year1, month, temperature)) {
    OpenPlowing(config, current, field, field.rotation_year1);
  }
}

void TrySowOnBlackFallow(const ProductionConfig& config,
                         WorldState& current,
                         FieldRow& field,
                         std::uint8_t month,
                         float temperature) {
  // THE BLACK FALLOW'S FURROW IS THE RYE'S (boss-core-epoch1-queue [81]-[82]).
  // Until 0.37.6 the rye opened a ploughing from nothing on a fallow the
  // village had ploughed and harrowed that summer. On seed 1945 the second
  // furrow ran from day 30 to day 33, the field was ready on day 34, and
  // September's last two days were at 4.4 and 2.9 °C against the rye's 8: the
  // slot was lost, and year 2 delivered 19 kg of 1116 kg of rye.
  const CropId slot = field.rotation_skips_turn != 0 ? field.rotation_year0 : field.rotation_year1;
  if (WinterWindowOpen(config, slot, month, temperature)) {
    OpenPlowing(config, current, field, slot, PreparationStart::kBlackFallow);
  }
}

namespace {

/// THE ZYAB IS DUE (register 13, decided 29 September 2026; farming design:
/// «правило хозяйства; ставит учётчик по правилу, объясняет староста» — no
/// agronomist in Epoch I; «Зябь — та же вспашка, только сразу после
/// уборки»; boss-core-epoch1-queue-2026-09-29 [20], [22], [23];
/// 0.37.18): an idle field of a chain, reaped this calendar year, whose next
/// sowing is a SPRING crop — the next slot's for a running chain, the first
/// for a chain named this year and not yet used — and whose furrow is not
/// turned yet. A winter crop next is sown this autumn (TrySowWinter) and is
/// no zyab's; a fallow next is ploughed in its own May.
bool ZyabDue(const ProductionConfig& config, const FieldRow& field, SimDay today) {
  if (field.autumn_plowed != 0 || !HasRotation(field) || field.reaped_day == kNeverReapedDay ||
      field.reaped_day / kDaysPerYear != today / kDaysPerYear) {
    return false;
  }
  const CropId next = field.rotation_skips_turn != 0 ? field.rotation_year0 : field.rotation_year1;
  return next.value < config.crops.size() && !config.crops[next.value].is_winter;
}

/// THE HARVEST IN AND CARTED FIRST (boss-core-epoch1-queue-2026-09-29 [27],
/// option «б»: «сначала убери и вывези, потом паши»): no arable field of the
/// farm has a heap lying, is being reaped, or grows a spring crop still to be
/// reaped this year — the potato the last of them. The first draft ranked the
/// zyab below the carting by the accountant's key alone, and the canon's
/// heaps on 1 December went 12.7 t -> 72.0 t, potato rotting in them on
/// three seed-years and hungry family-days +3.8 % (core [26]). A winter crop
/// growing does not hold it: in the autumn it is the one sown this autumn,
/// reaped next year; before July it is last autumn's, due in the summer —
/// then a zyab opens only on stubble reaped before July beside no spring
/// crop growing anywhere, which the canon never shows, and the rule lets it
/// be (static review of «б»). A perennial is cut, not dug, and neither its
/// standing nor its cut holds it — its hay heap does, as any heap to cart. A
/// meadow's hay goes straight to the stores (LayMownShare) and a meadow is
/// not asked: the rule is the arable's.
///
/// ONLY A HEAP THAT WILL BE CARTED holds it (boss [32]; static review of
/// «б»): one with nowhere to go — no room in the stores for it, flax with no
/// store that takes flax (ReceivableRoom, the carting's own door) — is not
/// carted at all, and holding the zyab behind it closed the field for the
/// year for a reason the player cannot see. The alarm that the load waits
/// says that trouble (kHarvestWaitingOnField).
bool HarvestInAndCarted(const ProductionConfig& config, const WorldState& world) {
  for (const FieldRow& other : world.fields.rows) {
    if (other.kind != LandKind::kArable) {
      continue;
    }
    if (other.reaped_grams > 0 && ReceivableRoom(config, world, other.reaped_resource) > 0) {
      return false;
    }
    const bool standing =
        other.phase == FieldPhase::kGrowing || other.phase == FieldPhase::kHarvest;
    if (standing && other.crop.value < config.crops.size()) {
      const CropDef& crop = config.crops[other.crop.value];
      if (crop.is_perennial) {
        continue;
      }
      if (other.phase == FieldPhase::kHarvest || !crop.is_winter) {
        return false;
      }
    }
  }
  return true;
}

/// Opens the autumn furrow: a plough and no harrow, no crop on the row. Not
/// through OpenPlowing, and on purpose: it spends no chain's first season
/// (rotation_skips_turn — the crop it is for is sown next spring, by the
/// spring's own opening), books no manure (the spring furrow books and pays
/// the dose the winter's plan deals), and owes no fallow's rest.
void OpenZyab(const ProductionConfig& config, WorldState& current, FieldRow& field) {
  field.crop = CropId{};
  field.furrow_day = kNoFurrowDay;
  field.autumn_furrowing = 1;
  field.overgrown = 0;  // one furrow brings the field back (OpenPlowing)
  OpenPhase(config, current, field, FieldPhase::kPlowing);
}

}  // namespace

void TrySow(const ProductionConfig& config,
            WorldState& current,
            FieldRow& field,
            std::uint8_t month,
            float temperature) {
  // THE STUBBLE FIRST, WHILE THE GROUND IS OPEN (register 13; ZyabDue): the
  // autumn furrow for next spring's crop, opened on a day at or above
  // nought, the spring ploughing's own gate. What comes of it is the
  // accountant's: the first of the jobs with no window (assignment.cpp), so
  // the reaping, the carting and the winter crop go before it and the
  // building and the felling after. And not before the farm's harvest is in
  // and carted (HarvestInAndCarted, boss [27] «б»).
  if (temperature >= 0.0F && ZyabDue(config, field, current.calendar.day) &&
      HarvestInAndCarted(config, current)) {
    OpenZyab(config, current, field);
    return;
  }
  if (field.rotation_year0.value >= config.crops.size()) {
    // A FALLOW YEAR IS PLOUGHED (farming design §7, "fallow is ploughed";
    // defect D11 of the reconciliation): the manure goes in with the
    // plough and the ground stands bare until the year turns, or until the
    // next slot's winter crop goes into it in the autumn (TrySowOnBlackFallow).
    //
    // BUT A FIELD WITH NO CHAIN AT ALL IS NOT ON FALLOW — NOBODY HAS TOLD IT
    // ANYTHING. The player gives each field a chain of three seasons, crop or
    // fallow (farming design §7); a field that has never been ASSIGNED one is
    // the one meant here — FieldRow::rotation_assigned, and no longer inferred
    // from the slots: emptiness INSIDE a chain is a rested season and this
    // guard lets it through, emptiness of the whole chain is the chairman
    // withdrawing his word and clears the byte (SetRotation). The start hands
    // over ninety-three hectares that were never assigned at all. Ploughing
    // them would be the core making the player's decision for him — and it
    // would cost the village about a hundred and fifty man-days a year nobody
    // asked for, which is defect D11 under a new name. It became reachable on 2026-09-12, when
    // LandKind::kDerelict went and that ground stopped being skipped whole.
    //
    // HasRotation asks it in one place for every asker (land_state.h).
    if (HasRotation(field) && month == config.farming.fallow_plow_month && temperature >= 0.0F) {
      OpenPlowing(config, current, field, CropId{});
    }
    return;
  }
  const CropDef& crop = config.crops[field.rotation_year0.value];
  // A FRESH CHAIN'S FIRST SLOT HAS NOT BEEN USED (land_state.h,
  // rotation_skips_turn; 0.36.10): it is neither "already off this year" nor
  // "given up past its window" — both readings below belong to a chain that
  // has been running. It waits for its own season, and the year's turn holds
  // the chain still until then. Until 0.36.10 a chain named in August as
  // (oats, winter rye, …) read the oats' closed window as given up and put
  // the rye in that September, ahead of the oats named first.
  const bool fresh_chain = field.rotation_skips_turn != 0;
  // A RUNNING CHAIN'S WINTER CROP THAT MISSED ITS AUTUMN IS LOST (fields design
  // §7, «Озимая, не посеянная в своё окно, пропадает»; question 278, 0.36.13).
  // The field is idle with this year's winter crop neither standing nor
  // reaped — so it was not sown last autumn — and the slot lies FALLOW this
  // year: ploughed as a fallow, and the next slot's winter crop goes into the
  // bare ground in its own autumn (RunFields, TrySowOnBlackFallow). Until 0.36.13 it
  // was ploughed in the spring and sown the autumn after, a year late, and it
  // stood through the next slot's only spring: the oats of a chain (potatoes,
  // rye, oats) whose rye missed its window were lost under it. A FRESH chain's
  // first slot is not this: it waits for its season (above, rotation_skips_turn).
  if (WinterSlotLost(field, crop.is_winter, current.calendar.day)) {
    if (month == config.farming.fallow_plow_month && temperature >= 0.0F) {
      OpenPlowing(config, current, field, CropId{});
      return;
    }
    // Not ploughed as a fallow (its month went by while the field was busy):
    // the next slot's winter crop may still go into it in its own autumn.
    TrySowWinter(config, current, field, month, temperature);
    return;
  }
  // Not lost and not fresh, a winter crop of this year's slot at an idle field
  // was reaped this year (WinterSlotLost says which) — no longer asked of
  // `last_crop`, which cannot tell this year's reaping from an older one.
  if (crop.is_winter && !fresh_chain) {
    // This year's winter crop was sown last autumn and is already off:
    // the field is idle because it was HARVESTED, not because the sowing
    // was missed. Sowing it again in August would put the same rye in two
    // years running and eat the next slot with it — the field sheet caught
    // exactly that. Only the next slot's winter crop may go in now.
    TrySowWinter(config, current, field, month, temperature);
    return;
  }
  // THE WINDOW IS STILL WHAT SAYS THE YEAR IS OVER FOR THIS CROP. Past
  // `sow_to_month` the slot cannot be sown at all, so there is nothing to
  // prepare the ground for, and the field falls through to the autumn path —
  // exactly as before. What is gone from this test is its FRONT half:
  // `month < crop.sow_from_month` used to send the field away too, which made
  // the sowing window a gate on the ploughing (see the header).
  if (month > crop.sow_to_month) {
    if (!fresh_chain) {
      TrySowWinter(config, current, field, month, temperature);
    }
    return;
  }
  // AND THE GROUND'S OWN CONDITION IN ITS PLACE, which is the thaw and nothing
  // else. It is the same test the fallow ploughing has always used above —
  // `temperature >= 0.0F` — and that is the point: a fallow field and a field
  // waiting for its oats are the same ground under the same plough, and they
  // disagreed about when it could be broken for no reason anybody had written
  // down.
  if (temperature < 0.0F) {
    return;
  }
  OpenPlowing(config, current, field, field.rotation_year0);
}

void FinishSowing(const ProductionConfig& config, WorldState& current, FieldRow& field) {
  const CropId crop_id = field.crop;
  // The preparation is over, sown or standing bare: its furrow mark means
  // nothing past the harrow (land_state.h, furrow_day).
  field.furrow_day = kNoFurrowDay;
  // THE BLACK FALLOW RESTS THE DAY ITS WINTER CROP IS SOWN (farming design,
  // «Чёрный пар, простоявший лето, восстанавливается в день сева озимой по
  // нему»; 0.37.12): the rye goes in before the turn and grows through it, so
  // the turn's rest, which asks for ground standing bare, never reached the
  // one crop the fallow is kept for — +0 on 189 seed-years of 189 (econ,
  // fallow-chain.md §6.1). Paid as the turn pays it (production_system.cpp):
  // the rest, and the rotation's memory cleared.
  if (field.fallow_rest_owed != 0 && crop_id.value != kInvalidDefIdValue) {
    field.fertility += config.farming.fallow_recovery;
    CapRestedFertility(config, field);
    field.last_crop = CropId{};
    field.repeat_years = 0;
  }
  field.fallow_rest_owed = 0;
  if (crop_id.value == kInvalidDefIdValue) {
    // Bare fallow: ploughed and harrowed, nothing goes in. It stands as
    // ground with no crop until the year turns or a winter crop takes it.
    MoveFieldPhase(current, field, FieldPhase::kGrowing);
    field.work_days_remaining = 0.0F;
    return;
  }
  // SOWN AS FAR AS THE SEED GOES (farming design §7, «Сеется столько, на
  // сколько хватило семян», decided 5 September 2026; boss seq 16). Until
  // 0.34.50 a short seed sowed the whole field anyway, even with none, and
  // the yield did not depend on the seed taken — so the seed fund's door
  // (0.34.49) held a number the harvest never read. Now the sown share is
  // what the seed covered, and the crop grows and is reaped on it
  // (FieldYieldGrams); the rest of the field stands unsown and says so.
  field.sown_share = 1.0F;
  if (crop_id.value < config.crops.size()) {
    const CropDef& crop = config.crops[crop_id.value];
    if (crop.sowing_norm_kg_per_ha > 0.0F) {
      const auto need = GramsFromKilograms(crop.sowing_norm_kg_per_ha * field.area_ga);
      const Grams got = TakeFromStorage(current, config, crop.resource, need);
      AddLedgerAmount(current.ledger.current.seed, crop.resource, got);
      field.sown_share =
          need > 0 ? static_cast<float>(static_cast<double>(got) / static_cast<double>(need))
                   : 1.0F;
      // The player is told before the morning of the sowing: kSeedShort stands
      // from the day the rotation is set (task A3; farming design §7). Phase
      // code does not log (DEADLOCK-001).
    }
  }
  current.ledger.current.area_sown_ha += field.area_ga * field.sown_share;
  field.crop = crop_id;
  MoveFieldPhase(current, field, FieldPhase::kGrowing);
  field.work_days_remaining = 0.0F;
  ClearFieldWeather(field);
}

void AdvanceFinishedField(const ProductionConfig& config, WorldState& current, FieldRow& field) {
  if (KindOfWorkingPhase(field.phase) == FieldPhase::kIdle || field.work_days_remaining > 0.0F) {
    return;
  }
  const auto month = static_cast<std::uint8_t>(current.calendar.date.month);
  const std::uint32_t day_of_year = current.calendar.day % kDaysPerYear;
  const float temperature = current.weather.air_temperature_celsius;
  field.work_days_remaining = 0.0F;
  switch (field.phase) {
    case FieldPhase::kPlowing:
      if (field.autumn_furrowing != 0) {
        // THE AUTUMN FURROW TURNED: zyab, and the field idle till the spring
        // opens it at the harrow (OpenPlowing spends `autumn_plowed`).
        field.autumn_furrowing = 0;
        field.autumn_plowed = 1;
        field.furrow_day = kNoFurrowDay;
        MoveFieldPhase(current, field, FieldPhase::kIdle);
        break;
      }
      OpenPhase(config, current, field, FieldPhase::kHarrowing);
      field.furrow_day = current.calendar.day;  // this preparation's furrow, and its day
      break;
    case FieldPhase::kHarrowing:
      if (field.crop.value == kInvalidDefIdValue) {
        FinishSowing(config, current, field);  // bare fallow: nothing to sow
      } else if (SowingMayOpen(config, field.crop, month, day_of_year, temperature) &&
                 LateSowingReturnsItsSeed(config, field, field.crop, current.calendar.day) &&
                 SowingHasSeed(config, current, field.crop)) {
        // AND NOT WITH NO SEED AT ALL (farming design §7; boss, 2026-09-25,
        // core's finding on 0.35.14): a pea field was opened and "sown" at
        // share 0.000 with nothing in the stores, and the sowing crew's day
        // went into bare ground. It waits harrowed, like the late sowing
        // above, and opens the day a loan or an exchange brings its seed.
        // AND NOT FOR LESS THAN ITS SEED (boss, boss-core-epoch1-5 seq 53;
        // fields design, «Три области», 0.35.6): the snow's rule let a
        // potato ploughed in August be sown on day 32 of seed 1939's third
        // year, and at the late factor's floor its 35 t of seed gave 8.8 t
        // back — the farm lost its seed for a crop it could not repay, and
        // with it the next four years. Such a field stays harrowed, the seed
        // stays in the fund, and the year's turn lets it go (TrySow).
        OpenPhase(config, current, field, FieldPhase::kSowing);
      }
      // THE SOWING MAY NOT START EARLY, AND MAY STILL FINISH LATE — and
      // that asymmetry is the whole of this repair. Both halves were
      // measured wrong before they were measured right.
      //
      // Boss's decision of 2026-09-13: "пахать можно, как только земля
      // открыта; сеять — только в свой агрономический срок". The plough
      // half is done in field_work.h (TrySow). This is the sowing half, and
      // SowingMayOpen asks the crop's own front edge and its temperature.
      //
      // WITHOUT IT the repair sowed oats in January. Moving the window off
      // the plough and putting nothing in its place left the seed following
      // the plough straight into frozen ground, below the crop's own
      // growth temperature: oat_balance went to all zeros — a kilogram of
      // seed giving back nothing, every year of the run.
      //
      // WITH IT ON BOTH EDGES the opposite cliff appeared. A field harrowed
      // one day after its window shut was then never sown at all: the first
      // year put in 10.5 hectares instead of 66.5, five fields stood
      // harrowed to the end of the year, satiety fell by a fifth and the
      // plan was failed SIX YEARS RUNNING — the village reached «Под суд»
      // in its third year with the chairman doing nothing wrong. That reads
      // straight against "никаких безвыходных ситуаций" and "не наказывать
      // за непредвидимое".
      //
      // So the back edge stays open and the old seam is kept where it was
      // right: the window says when the sowing may START, the crew decides
      // when it ends. A field that misses its window entirely is a
      // different question — sowing late for a smaller crop is a YIELD
      // mechanic, and boss added it on 2026-09-13 (LateSowingFactor,
      // field_work.h); a field that cannot ripen before snow is not sown
      // at all (SowingMayOpen).
      break;
    case FieldPhase::kSowing:
      // THE SEED DOES NOT GO IN IN THE RAIN (core_common/rain_stops_work.h),
      // whoever drained the work: a crew cannot, the accountant sends none,
      // but the MTS column finishes a field's work in one budget, and its
      // sowing waits here, owed nothing, for the first dry hour.
      if (RainStopsWork(current.weather.precipitation, WorkKind::kSowing)) {
        break;
      }
      FinishSowing(config, current, field);
      break;
    case FieldPhase::kHarvest:
      FinishHarvest(config, current, field);
      break;
    default:
      break;
  }
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
