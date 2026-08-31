// Implementation of the core_production boundary
// (include/core_production/production_system.h). Stage 4: the field life
// cycle (sowing by windows and temperature, growth under weather stress,
// harvest into storage, fertility bookkeeping, snow loss), herd feeding and
// the manure flow. Herd sizes stay static — offspring belongs to the
// feeding stage; horse offspring is additionally capacity-blocked until a
// stable exists (start rework parcel).
//
// Stage 5 wired the labor seam into it: a working phase is OPENED here with
// its demand (area x the phase's norm in game man-days) and closed here when
// the crew has drained FieldRow::work_days_remaining to zero. Production
// never calls labor and labor never calls production — the field row carries
// the whole contract (manual/65-labor-model.md §2). Deliveries stay instant
// (the phase-1 logistics stub): harvest lands in storage directly, hay at
// the stock yard, manure at the compost heap.

#include "core_production/production_system.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/ledger_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_log/log.h"
#include "core_tables/tables.h"
#include "herd_system.h"
#include "production_config.h"
#include "stock_ops.h"

namespace core {
namespace {

/// The production slot (phase 4), parallel by field: accumulates the
/// growth-season weather stress. Reads the calendar and the day's weather
/// from `current` — both blocks are written by phase 1 alone and frozen for
/// the rest of the step (buffer-law rule 4) — and writes only the owned
/// field rows.
class FieldGrowthPhase final : public IParallelPhase {
 public:
  explicit FieldGrowthPhase(const ProductionConfig& config) : config_(&config) {}

  std::uint32_t ParallelItemCount(const WorldState& current) const override {
    return static_cast<std::uint32_t>(current.fields.rows.size());
  }

  void RunItemRange(const WorldState& /*previous*/,
                    WorldState& current,
                    std::uint32_t begin_item,
                    std::uint32_t end_item) override {
    if (HourFromTick(current.calendar.tick) != 0) {
      return;  // stress is a daily quantity
    }
    const WeatherState& weather = current.weather;
    const FarmingConfig& farming = config_->farming;
    for (std::uint32_t item = begin_item; item < end_item; ++item) {
      FieldRow& field = current.fields.rows[item];
      if (field.phase != FieldPhase::kGrowing || field.crop.value >= config_->crops.size()) {
        continue;
      }
      const CropDef& crop = config_->crops[field.crop.value];
      float stress = 0.0F;
      // Drought is a matter of the AFTERNOON: the mean plus the season's
      // half-swing (camera design §4). On the mean alone the summer never
      // reached +25 and this branch was dead in every run before it.
      const auto season = static_cast<std::size_t>(current.calendar.season);
      const float afternoon =
          weather.air_temperature_celsius + (season < farming.temp_amplitude_by_season.size()
                                                 ? farming.temp_amplitude_by_season[season]
                                                 : 0.0F);
      if (weather.precipitation == Precipitation::kRain) {
        stress = farming.stress_per_day * crop.wet_sensitivity;
      } else if (afternoon >= farming.drought_temp_c) {
        stress = farming.stress_per_day * crop.drought_sensitivity;
      }
      field.weather_stress += stress;
      field.weather_stress =
          field.weather_stress > farming.stress_cap ? farming.stress_cap : field.weather_stress;
    }
  }

 private:
  const ProductionConfig* config_;
};

class ProductionSystem final : public IProductionSystem {
 public:
  explicit ProductionSystem(const ProductionConfig& config) : config_(config), phase_(config_) {}

  IParallelPhase& ProductionPhase() override { return phase_; }

  void RunProductionDecisions(const WorldState& previous, WorldState& current) override {
    if (config_.crops.empty()) {
      return;  // a table-less world idles (STUB)
    }
    // Finished work is picked up the same hour the crew finishes it: labor
    // runs earlier in this very slot, so a field ploughed by noon opens its
    // harrowing at noon instead of losing the afternoon.
    AdvanceFinishedPhases(current);
    if (current.calendar.tick == 1) {
      // The first winter's plan: genesis hands over a heap and a January, and
      // day zero is no year's turn for the daily bookkeeping below. Without
      // this the inherited 250 t lay untouched through the whole first year.
      PlanManure(current);
    }
    if (current.calendar.day == previous.calendar.day) {
      return;  // everything below is daily work
    }
    if (current.calendar.day % kDaysPerYear == 0) {
      RunYearStart(current);
    }
    RunFields(current);
    RunHerdDay(config_, current);
  }

 private:
  /// The labor seam, seen from the production side (land_state.h): a field
  /// stands in a working phase until its crew has drained
  /// work_days_remaining, and only then moves on. No workers, no progress —
  /// deliberately.
  void AdvanceFinishedPhases(WorldState& current) {
    for (FieldRow& field : current.fields.rows) {
      if (KindOfWorkingPhase(field.phase) == FieldPhase::kIdle ||
          field.work_days_remaining > 0.0F) {
        continue;
      }
      field.work_days_remaining = 0.0F;
      switch (field.phase) {
        case FieldPhase::kPlowing:
          OpenPhase(field, FieldPhase::kHarrowing);
          break;
        case FieldPhase::kHarrowing:
          if (field.crop.value == kInvalidDefIdValue) {
            FinishSowing(current, field);  // bare fallow: nothing to sow
          } else {
            OpenPhase(field, FieldPhase::kSowing);
          }
          break;
        case FieldPhase::kSowing:
          FinishSowing(current, field);
          break;
        case FieldPhase::kHarvest:
          FinishHarvest(current, field);
          break;
        default:
          break;
      }
    }
  }

  /// @brief kIdle for a phase that needs no work, the phase itself for the
  /// four working ones.
  static constexpr FieldPhase KindOfWorkingPhase(FieldPhase phase) {
    switch (phase) {
      case FieldPhase::kPlowing:
      case FieldPhase::kHarrowing:
      case FieldPhase::kSowing:
      case FieldPhase::kHarvest:
        return phase;
      case FieldPhase::kIdle:
      case FieldPhase::kGrowing:
        return FieldPhase::kIdle;
    }
    return FieldPhase::kIdle;
  }

  /// @brief Moves the field into a working phase and sizes its demand:
  /// area x the phase's norm. The crop is the one in the ground or, while
  /// the field is still being prepared, the one this year's rotation plans.
  void OpenPhase(FieldRow& field, FieldPhase phase) const {
    field.phase = phase;
    if (field.kind != LandKind::kArable) {
      // Grass is mown, never ploughed, harrowed or sown: the meadow has one
      // working phase in the year and one norm to size it.
      field.work_days_remaining = phase == FieldPhase::kHarvest
                                      ? config_.farming.meadow_mow_days_per_ha * field.area_ga
                                      : 0.0F;
      return;
    }
    const CropId crop = field.crop;
    float norm = 0.0F;
    if (phase == FieldPhase::kPlowing) {
      norm = config_.farming.plow_days_per_ha;
    } else if (phase == FieldPhase::kHarrowing) {
      norm = config_.farming.harrow_days_per_ha;
    } else if (crop.value < config_.crops.size()) {
      norm = phase == FieldPhase::kSowing ? config_.crops[crop.value].sow_days_per_ha
                                          : config_.crops[crop.value].harvest_days_per_ha;
    }
    field.work_days_remaining = norm * field.area_ga;
  }

  /// January 1: the rotation plan advances one year, and fallow that stood
  /// the whole year pays out its recovery.
  /// @brief Is this one of the six bread grains the plan counts?
  bool IsPlanGrain(ResourceId resource) const {
    for (const ResourceId grain : config_.plan_grain_resources) {
      if (grain.value == resource.value) {
        return true;
      }
    }
    return false;
  }

  /// The year's delivery: what the plan asked for leaves the stores and is
  /// recorded as delivered. A shortfall is simply a smaller delivery — the
  /// district has no mechanics in phase 1, and inventing consequences for it
  /// would be inventing the district.
  void DeliverPlan(WorldState& current) const {
    current.plan.delivered.assign(current.plan.due.size(), 0);
    for (std::uint32_t index = 0; index < current.plan.due.size(); ++index) {
      const ResourceId resource{static_cast<std::uint16_t>(index)};
      const Grams taken = TakeFromStorage(current, config_, resource, current.plan.due[index]);
      current.plan.delivered[index] = taken;
      AddLedgerAmount(current.ledger.current.delivered, resource, taken);
    }
    current.plan.due.assign(current.plan.due.size(), 0);
  }

  void RunYearStart(WorldState& current) const {
    DeliverPlan(current);
    for (FieldRow& field : current.fields.rows) {
      if (field.kind != LandKind::kArable) {
        continue;  // no rotation to shift, no fallow to pay out; derelict rests as it is
      }
      const bool bare =
          field.phase == FieldPhase::kGrowing && field.crop.value == kInvalidDefIdValue;
      if (bare) {
        field.phase = FieldPhase::kIdle;  // the ploughed fallow stood its year
        if (field.manure_applied != 0) {
          // A fallow has no harvest to settle its manure at: it settles here.
          field.fertility += ManureBonus(field);
          field.fertility = field.fertility > 100.0F ? 100.0F : field.fertility;
          field.manure_applied = 0;
        }
      }
      if ((field.phase == FieldPhase::kIdle) && field.rotation_year0.value == kInvalidDefIdValue) {
        field.fertility += config_.farming.fallow_recovery;
        field.fertility = field.fertility > 100.0F ? 100.0F : field.fertility;
        field.last_crop = CropId{};
        field.repeat_years = 0;
      }
      const CropId shifted = field.rotation_year0;
      field.rotation_year0 = field.rotation_year1;
      field.rotation_year1 = field.rotation_year2;
      field.rotation_year2 = shifted;
      // A perennial stand ends when the plan moves on (farming design §5).
      if (field.phase == FieldPhase::kGrowing && field.crop.value < config_.crops.size() &&
          config_.crops[field.crop.value].is_perennial &&
          field.rotation_year0.value != field.crop.value) {
        field.last_crop = field.crop;
        field.crop = CropId{};
        field.phase = FieldPhase::kIdle;
        field.weather_stress = 0.0F;
      }
    }
    PlanManure(current);
  }

  void RunFields(WorldState& current) {
    const auto month = static_cast<std::uint8_t>(current.calendar.date.month);
    const float temperature = current.weather.air_temperature_celsius;
    const bool snowing = current.weather.precipitation == Precipitation::kSnow;
    for (FieldRow& field : current.fields.rows) {
      if (field.kind == LandKind::kDerelict) {
        continue;  // unraised land: nothing happens here until it is raised
      }
      if (field.kind != LandKind::kArable) {
        RunMeadow(current, field, month);
        continue;
      }
      if (field.phase == FieldPhase::kIdle) {
        TrySow(current, field, month, temperature);
        continue;
      }
      const bool standing =
          field.phase == FieldPhase::kGrowing || field.phase == FieldPhase::kHarvest;
      if (standing && field.crop.value == kInvalidDefIdValue) {
        // Black fallow: ploughed this spring and standing bare (D11). It is
        // sown only from the NEXT slot, and only with a winter crop — the
        // canon's "fallow, then winter rye" — never re-ploughed as fallow.
        TrySowWinter(current, field, month, temperature);
        continue;
      }
      if (!standing || field.crop.value >= config_.crops.size()) {
        continue;  // being prepared, or a crop this config does not know
      }
      const CropDef& crop = config_.crops[field.crop.value];
      // Snow on an unharvested annual is the one total loss (§6) — and it
      // takes a field the crew is still reaping, which is exactly why the
      // harvest window outranks every other job (assignment.cpp). Winter
      // crops and perennials winter under snow by design.
      if (snowing && !crop.is_winter && !crop.is_perennial) {
        field.last_crop = field.crop;
        field.repeat_years = 0;
        field.crop = CropId{};
        field.phase = FieldPhase::kIdle;
        field.work_days_remaining = 0.0F;
        field.weather_stress = 0.0F;
        field.manure_applied = 0;
        current.ledger.current.area_lost_ha += field.area_ga;
        LogWarning("field lost to snow before harvest");
        continue;
      }
      if (field.phase != FieldPhase::kGrowing) {
        continue;  // already being reaped
      }
      const bool in_window = month >= crop.harvest_from_month && month <= crop.harvest_to_month;
      // A perennial stand stays growing after its cut, so gate it to one
      // cut a year — the first day of its window; an annual leaves the
      // growing phase at harvest and cannot double-fire.
      const bool cut_today = !crop.is_perennial || (month == crop.harvest_from_month &&
                                                    current.calendar.date.day_in_month == 0);
      if (in_window && cut_today) {
        OpenPhase(field, FieldPhase::kHarvest);
      }
    }
  }

  /// The meadow's whole year: it stands, and once a season the scythes go
  /// out. No sowing window, no temperature gate, no snow loss (grass winters
  /// where it grew), no fertility — a meadow is land, not a crop
  /// (land_state.h, LandKind; boss answer Q6).
  void RunMeadow(const WorldState& current, FieldRow& field, std::uint8_t month) const {
    if (field.phase != FieldPhase::kGrowing) {
      return;  // already being mown, and one cut a year is all there is
    }
    if (month == config_.farming.meadow_cut_month && current.calendar.date.day_in_month == 0) {
      OpenPhase(field, FieldPhase::kHarvest);
    }
  }

  /// @brief Puts a harvested load where it belongs: hay at the manger, the
  /// rest in the shared store (the phase-1 logistics stub).
  /// @return kNoRow when the settlement has nowhere at all to put it.
  std::uint32_t DeliverHarvest(WorldState& current, ResourceId resource, Grams amount) const {
    std::uint32_t destination = kNoRow;
    if (resource.value == config_.hay_resource.value) {
      destination = FindStockYardRow(current, config_);
    }
    if (destination == kNoRow) {
      destination = FindStorageRow(current, config_);
    }
    if (destination != kNoRow) {
      AddToStock(current.units.rows[destination].stock, resource, amount);
    }
    return destination;
  }

  /// The season's cut. The yield is the land's own rate for the WHOLE
  /// season, which is why one cut a year is not a simplification: the
  /// second cut is inside the number (farming.csv, meadow_yield_kg_per_ha).
  void MowMeadow(WorldState& current, FieldRow& field) const {
    const float rate = field.kind == LandKind::kFloodplainMeadow
                           ? config_.farming.meadow_floodplain_yield_kg_per_ha
                           : config_.farming.meadow_yield_kg_per_ha;
    const Grams hay = KilogramsToGrams(rate * field.area_ga);
    DeliverHarvest(current, config_.hay_resource, hay);
    AddLedgerAmount(current.ledger.current.harvest, config_.hay_resource, hay);
    current.ledger.current.area_harvested_ha += field.area_ga;
    field.work_days_remaining = 0.0F;
    field.phase = FieldPhase::kGrowing;  // the grass stands again next summer
  }

  /// The field year opens here: the sowing window and the temperature say
  /// "go", and the field enters plowing. What follows — harrowing, sowing —
  /// is paced by the crew, so the seed may well go into the ground after
  /// the window has closed. That is the point of the seam: the window is
  /// when the work STARTS, the crew decides when it ends.
  void TrySow(WorldState& current, FieldRow& field, std::uint8_t month, float temperature) {
    if (field.rotation_year0.value >= config_.crops.size()) {
      // A FALLOW YEAR IS PLOUGHED (farming design §7, "fallow is ploughed";
      // defect D11 of the reconciliation): the manure goes in with the
      // plough and the ground stands bare until the year turns, or until the
      // next slot's winter crop goes into it in the autumn (TrySowWinter).
      if (month == config_.farming.fallow_plow_month && temperature >= 0.0F) {
        OpenPlowing(current, field, CropId{});
      }
      return;
    }
    const CropDef& crop = config_.crops[field.rotation_year0.value];
    if (crop.is_winter && field.last_crop.value == field.rotation_year0.value) {
      // This year's winter crop was sown last autumn and is already off:
      // the field is idle because it was HARVESTED, not because the sowing
      // was missed. Sowing it again in August would put the same rye in two
      // years running and eat the next slot with it — the field sheet caught
      // exactly that. Only the next slot's winter crop may go in now.
      TrySowWinter(current, field, month, temperature);
      return;
    }
    // Otherwise a winter crop in THIS year's slot is the fallback path: it
    // was meant to go in last autumn (TrySowWinter) and that autumn was
    // missed, so it is sown in its window a year late. That costs the slot
    // after it, but a winter crop never sown costs the plan its bread.
    if (month < crop.sow_from_month || month > crop.sow_to_month ||
        temperature < crop.sow_min_temp_c) {
      TrySowWinter(current, field, month, temperature);
      return;
    }
    OpenPlowing(current, field, field.rotation_year0);
  }

  /// The autumn sowing (defect D12). A winter crop is harvested the summer
  /// AFTER it is sown, so the slot it belongs to is next year's — "winter rye
  /// goes into the ground in the autumn of the same year, and the ring starts
  /// turning in the second" (start canon §8). Sown from this year's slot it
  /// arrived a year late and ate the following spring as well.
  void TrySowWinter(WorldState& current, FieldRow& field, std::uint8_t month, float temperature) {
    if (field.rotation_year1.value >= config_.crops.size()) {
      return;
    }
    const CropDef& next = config_.crops[field.rotation_year1.value];
    if (!next.is_winter || month < next.sow_from_month || month > next.sow_to_month ||
        temperature < next.sow_min_temp_c) {
      return;
    }
    OpenPlowing(current, field, field.rotation_year1);
  }

  /// @brief Opens the ploughing for `crop` (invalid = bare fallow). The
  /// manure, if the winter's plan gave this field any, is already on the
  /// row (PlanManure) and goes in with the plough (§8).
  void OpenPlowing(WorldState& current, FieldRow& field, CropId crop) {
    field.crop = crop;
    if (field.manure_applied != 0) {
      const float share = static_cast<float>(field.manure_applied) / 100.0F;
      const auto dose =
          static_cast<Grams>(config_.farming.manure_norm_kg_per_ha * field.area_ga * share) *
          kGramsPerKilogram;
      current.ledger.current.manure_plowed_in += dose;
      current.ledger.current.area_manured_ha += field.area_ga * share;
    }
    OpenPhase(field, FieldPhase::kPlowing);
  }

  /// THE WINTER'S MANURE PLAN, made at the year's turn: the heap is dealt out
  /// in full doses to the fields that will be ploughed this year, POOREST
  /// FIELD FIRST, until it runs out. What the herd makes during the year
  /// waits for next winter's plan.
  ///
  /// It used to be first come, first served at the plough: a field took a
  /// full dose if the heap held one that morning, or nothing. The field sheet
  /// of the fifth reconciliation pass showed what that does — the twenty-one
  /// hectare potato field never once qualified in thirty years, because its
  /// dose is 420 t and the heap never holds that, while the ten-hectare field
  /// next to it was manured twenty-seven years out of thirty and stood at a
  /// hundred. The canon's "a hectare gets manure every four or five years"
  /// is a rotation of the manure. NOT a player's decision and not a stub for
  /// one: "the manure norm is a number, not a decision" (farming design §9,
  /// closed), and a yearly allocation of the heap across the fields is the
  /// agronomist's work — the very micromanagement the game removes epoch by
  /// epoch. The player's lever in this loop is how much livestock to keep.
  void PlanManure(WorldState& current) const {
    const std::uint32_t heap = FindUnitRowOfType(current, config_.compost_heap_type);
    if (heap == kNoRow) {
      return;
    }
    Grams held = StockOf(current.units.rows[heap].stock, config_.manure_resource);
    std::vector<std::uint32_t> candidates;
    for (std::uint32_t row = 0; row < current.fields.rows.size(); ++row) {
      const FieldRow& field = current.fields.rows[row];
      // Ploughed this year: idle arable, whether a crop is in the slot or it
      // is fallow. A winter crop already standing was manured when IT was
      // ploughed, last autumn.
      if (field.kind == LandKind::kArable && field.phase == FieldPhase::kIdle &&
          field.manure_applied == 0) {
        candidates.push_back(row);
      }
    }
    // Poorest first; equal fertility keeps row order, so the plan is the
    // same on every machine.
    std::stable_sort(candidates.begin(), candidates.end(), [&](std::uint32_t a, std::uint32_t b) {
      return current.fields.rows[a].fertility < current.fields.rows[b].fertility;
    });
    for (const std::uint32_t row : candidates) {
      if (held <= 0) {
        break;
      }
      FieldRow& field = current.fields.rows[row];
      const auto dose = static_cast<Grams>(config_.farming.manure_norm_kg_per_ha * field.area_ga) *
                        kGramsPerKilogram;
      if (dose <= 0) {
        continue;
      }
      // A PARTIAL DOSE IS A DOSE. The poorest field takes what the heap has,
      // up to its full norm, and its bonus scales with the share it got.
      // Whole doses or nothing meant the biggest field could never be
      // manured at all: 420 t for twenty-one hectares, and a herd of twenty
      // cows makes 250 a year.
      const Grams given = held < dose ? held : dose;
      held -= given;
      AddToStock(current.units.rows[heap].stock, config_.manure_resource, -given);
      const float share = static_cast<float>(given) / static_cast<float>(dose);
      field.manure_applied = static_cast<std::uint8_t>(share * 100.0F + 0.5F);
      if (field.manure_applied == 0) {
        field.manure_applied = 1;  // a dribble still counts as touched
      }
    }
  }

  /// @brief The manure bonus this field has coming, by the share of its dose
  /// it received (FieldRow::manure_applied is that share in percent).
  float ManureBonus(const FieldRow& field) const {
    return config_.farming.manure_fertility_bonus * static_cast<float>(field.manure_applied) /
           100.0F;
  }

  /// The seed goes into the ground when the sowing phase is worked through.
  void FinishSowing(WorldState& current, FieldRow& field) {
    const CropId crop_id = field.crop;
    if (crop_id.value == kInvalidDefIdValue) {
      // Bare fallow: ploughed and harrowed, nothing goes in. It stands as
      // ground with no crop until the year turns or a winter crop takes it.
      field.phase = FieldPhase::kGrowing;
      field.work_days_remaining = 0.0F;
      return;
    }
    if (crop_id.value < config_.crops.size()) {
      const CropDef& crop = config_.crops[crop_id.value];
      // Sowing consumes ordinary produce of the same crop (§7); partial
      // seed sows the whole field anyway — the shortfall alarm is a UI
      // concern.
      if (crop.sowing_norm_kg_per_ha > 0.0F) {
        const auto need =
            static_cast<Grams>(crop.sowing_norm_kg_per_ha * field.area_ga) * kGramsPerKilogram;
        const Grams got = TakeFromStorage(current, config_, crop.resource, need);
        AddLedgerAmount(current.ledger.current.seed, crop.resource, got);
        if (got < need) {
          LogWarning("sowing short of seed; sown anyway (STUB until alarms)");
        }
      }
    }
    current.ledger.current.area_sown_ha += field.area_ga;
    field.crop = crop_id;
    field.phase = FieldPhase::kGrowing;
    field.work_days_remaining = 0.0F;
    field.weather_stress = 0.0F;
  }

  /// The reaped field pays out and leaves the harvest phase.
  void FinishHarvest(WorldState& current, FieldRow& field) {
    if (field.kind != LandKind::kArable) {
      MowMeadow(current, field);
      return;
    }
    if (field.crop.value >= config_.crops.size()) {
      field.phase = FieldPhase::kIdle;
      return;
    }
    Harvest(current, field, config_.crops[field.crop.value]);
  }

  void Harvest(WorldState& current, FieldRow& field, const CropDef& crop) {
    const float soil_factor = field.fertility / config_.farming.fertility_neutral;
    const float weather_factor = 1.0F - field.weather_stress;
    const auto yield_grams =
        static_cast<Grams>(crop.yield_kg_per_ha * field.area_ga * soil_factor * weather_factor) *
        kGramsPerKilogram;
    // Instant delivery (logistics stub): hay feeds the stock yard, the rest
    // goes to shared storage.
    DeliverHarvest(current, crop.resource, yield_grams);
    // Booked whether or not a store took it in: what the field gave is what
    // the reconciliation compares against the yield tables, and a settlement
    // with nowhere to put its grain is a different finding entirely.
    AddLedgerAmount(current.ledger.current.harvest, crop.resource, yield_grams);
    current.ledger.current.area_harvested_ha += field.area_ga;
    // Straw is what the field leaves behind, and it is a feed of its own —
    // own and free, a reserve ration with a lowered effect but plainly there
    // in a winter manger (design db crop.straw_ratio).
    if (crop.straw_ratio > 0.0F) {
      const std::uint32_t straw_store = FindStorageRow(current, config_);
      const auto straw = static_cast<Grams>(static_cast<float>(yield_grams) * crop.straw_ratio);
      if (straw_store != kNoRow) {
        AddToStock(current.units.rows[straw_store].stock, config_.straw_resource, straw);
      }
      AddLedgerAmount(current.ledger.current.harvest, config_.straw_resource, straw);
    }
    // The district's plan accrues as the grain is reaped: it is "just a
    // number" in phase 1 (plan §11), a share of the year's own harvest,
    // handed over at the year's turn with no district mechanics behind it.
    if (config_.plan_grain_share > 0.0F && IsPlanGrain(crop.resource)) {
      AddToStock(current.plan.due,
                 crop.resource,
                 static_cast<Grams>(static_cast<float>(yield_grams) * config_.plan_grain_share));
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
    const float charged = repeated < config_.farming.repeat_penalty_max_years
                              ? repeated
                              : config_.farming.repeat_penalty_max_years;
    field.fertility += crop.fertility_delta + ManureBonus(field) -
                       charged * config_.farming.repeat_penalty_per_year;
    // And a floor under it: an exhausted field bears little, but it bears.
    const float floor_value = config_.farming.fertility_floor;
    field.fertility = field.fertility < floor_value ? floor_value : field.fertility;
    field.fertility = field.fertility > 100.0F ? 100.0F : field.fertility;
    field.manure_applied = 0;
    field.last_crop = field.crop;
    field.weather_stress = 0.0F;
    field.work_days_remaining = 0.0F;
    if (crop.is_perennial && field.rotation_year1.value == field.crop.value) {
      field.phase = FieldPhase::kGrowing;  // the stand yields again next summer
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
    field.phase = FieldPhase::kIdle;
  }

  ProductionConfig config_;

  FieldGrowthPhase phase_;
};

}  // namespace

std::unique_ptr<IProductionSystem> CreateProductionSystem(const ITableSet& tables) {
  ProductionConfig config;
  std::string error;
  if (!ParseProductionConfig(tables, config, error)) {
    LogError(error);
    return nullptr;
  }
  return std::make_unique<ProductionSystem>(config);
}

}  // namespace core
