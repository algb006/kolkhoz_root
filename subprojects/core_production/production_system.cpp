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

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core_common/calendar.h"
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
      if (weather.precipitation == Precipitation::kRain) {
        stress = farming.stress_per_day * crop.wet_sensitivity;
      } else if (weather.air_temperature_celsius >= farming.drought_temp_c) {
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
          OpenPhase(field, FieldPhase::kSowing);
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
    const CropId crop = field.crop.value != kInvalidDefIdValue ? field.crop : field.rotation_year0;
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
    }
    current.plan.due.assign(current.plan.due.size(), 0);
  }

  void RunYearStart(WorldState& current) const {
    DeliverPlan(current);
    for (FieldRow& field : current.fields.rows) {
      if (field.phase == FieldPhase::kIdle && field.rotation_year0.value == kInvalidDefIdValue) {
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
  }

  void RunFields(WorldState& current) {
    const auto month = static_cast<std::uint8_t>(current.calendar.date.month);
    const float temperature = current.weather.air_temperature_celsius;
    const bool snowing = current.weather.precipitation == Precipitation::kSnow;
    for (FieldRow& field : current.fields.rows) {
      if (field.phase == FieldPhase::kIdle) {
        TrySow(current, field, month, temperature);
        continue;
      }
      const bool standing =
          field.phase == FieldPhase::kGrowing || field.phase == FieldPhase::kHarvest;
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

  /// The field year opens here: the sowing window and the temperature say
  /// "go", and the field enters plowing. What follows — harrowing, sowing —
  /// is paced by the crew, so the seed may well go into the ground after
  /// the window has closed. That is the point of the seam: the window is
  /// when the work STARTS, the crew decides when it ends.
  void TrySow(WorldState& current, FieldRow& field, std::uint8_t month, float temperature) {
    if (field.rotation_year0.value >= config_.crops.size()) {
      return;  // fallow year
    }
    const CropDef& crop = config_.crops[field.rotation_year0.value];
    if (month < crop.sow_from_month || month > crop.sow_to_month ||
        temperature < crop.sow_min_temp_c) {
      return;
    }
    // Manure is plowed in, never spread separately (§8): the dose leaves
    // the heap when the plowing starts.
    const std::uint32_t heap = FindUnitRowOfType(current, config_.compost_heap_type);
    if (heap != kNoRow) {
      const auto dose = static_cast<Grams>(config_.farming.manure_norm_kg_per_ha * field.area_ga) *
                        kGramsPerKilogram;
      if (StockOf(current.units.rows[heap].stock, config_.manure_resource) >= dose) {
        AddToStock(current.units.rows[heap].stock, config_.manure_resource, -dose);
        field.manure_applied = 1;
      }
    }
    OpenPhase(field, FieldPhase::kPlowing);
  }

  /// The seed goes into the ground when the sowing phase is worked through.
  void FinishSowing(WorldState& current, FieldRow& field) {
    const CropId crop_id = field.rotation_year0;
    if (crop_id.value < config_.crops.size()) {
      const CropDef& crop = config_.crops[crop_id.value];
      // Sowing consumes ordinary produce of the same crop (§7); partial
      // seed sows the whole field anyway — the shortfall alarm is a UI
      // concern.
      if (crop.sowing_norm_kg_per_ha > 0.0F) {
        const auto need =
            static_cast<Grams>(crop.sowing_norm_kg_per_ha * field.area_ga) * kGramsPerKilogram;
        const Grams got = TakeFromStorage(current, config_, crop.resource, need);
        if (got < need) {
          LogWarning("sowing short of seed; sown anyway (STUB until alarms)");
        }
      }
    }
    field.crop = crop_id;
    field.phase = FieldPhase::kGrowing;
    field.work_days_remaining = 0.0F;
    field.weather_stress = 0.0F;
  }

  /// The reaped field pays out and leaves the harvest phase.
  void FinishHarvest(WorldState& current, FieldRow& field) {
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
    std::uint32_t destination = kNoRow;
    if (crop.resource.value == config_.hay_resource.value) {
      for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
        const UnitRow& unit = current.units.rows[row];
        if (unit.type.value < config_.unit_types.size() &&
            config_.unit_types[unit.type.value].livestock_capacity_head > 0.0F) {
          destination = row;
          break;
        }
      }
    }
    if (destination == kNoRow) {
      destination = FindStorageRow(current, config_);
    }
    if (destination != kNoRow) {
      AddToStock(current.units.rows[destination].stock, crop.resource, yield_grams);
    }
    // Straw is what the field leaves behind, and it is a feed of its own —
    // own and free, a reserve ration with a lowered effect but plainly there
    // in a winter manger (design db crop.straw_ratio).
    if (crop.straw_ratio > 0.0F) {
      const std::uint32_t straw_store = FindStorageRow(current, config_);
      if (straw_store != kNoRow) {
        AddToStock(current.units.rows[straw_store].stock,
                   config_.straw_resource,
                   static_cast<Grams>(static_cast<float>(yield_grams) * crop.straw_ratio));
      }
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
    field.fertility +=
        crop.fertility_delta +
        (field.manure_applied != 0 ? config_.farming.manure_fertility_bonus : 0.0F) -
        static_cast<float>(field.repeat_years) * config_.farming.repeat_penalty_per_year;
    field.fertility = field.fertility < 0.0F ? 0.0F : field.fertility;
    field.fertility = field.fertility > 100.0F ? 100.0F : field.fertility;
    field.manure_applied = 0;
    field.last_crop = field.crop;
    field.weather_stress = 0.0F;
    field.work_days_remaining = 0.0F;
    if (crop.is_perennial) {
      field.phase = FieldPhase::kGrowing;  // the stand yields again next summer
      return;
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
