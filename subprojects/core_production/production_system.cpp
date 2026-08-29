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
#include "production_config.h"

namespace core {
namespace {

/// @brief Grows a stock vector on demand and adds grams (may be negative;
/// clamps at zero).
void AddToStock(ResourceAmounts& stock, ResourceId resource, Grams amount) {
  if (resource.value == kInvalidDefIdValue) {
    return;
  }
  if (stock.size() <= resource.value) {
    stock.resize(resource.value + 1U, 0);
  }
  Grams& cell = stock[resource.value];
  cell += amount;
  cell = cell < 0 ? 0 : cell;
}

Grams StockOf(const ResourceAmounts& stock, ResourceId resource) {
  if (resource.value == kInvalidDefIdValue || stock.size() <= resource.value) {
    return 0;
  }
  return stock[resource.value];
}

/// @brief First unit able to store goods (storage capacity > 0); kNoRow if
/// none. Phase-1 routing: one shared storage pool, capacity overflow is a
/// logged STUB until real logistics.
std::uint32_t FindStorageRow(const WorldState& world, const ProductionConfig& config) {
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    const UnitRow& unit = world.units.rows[row];
    if (unit.type.value < config.unit_types.size() &&
        config.unit_types[unit.type.value].storage_capacity_kg > 0.0F) {
      return row;
    }
  }
  return kNoRow;
}

/// @brief First unit of the given type; kNoRow if none. An invalid type
/// matches nothing: otherwise it would match every unit whose type is also
/// unset (a table-less world's houses) and index the config out of bounds.
std::uint32_t FindUnitRowOfType(const WorldState& world, UnitTypeId type) {
  if (type.value == kInvalidDefIdValue) {
    return kNoRow;
  }
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    if (world.units.rows[row].type.value == type.value) {
      return row;
    }
  }
  return kNoRow;
}

/// @brief Storage capacity of a unit in grams; 0 when its type is unknown
/// to the config (a hand-built world, or a table set without unit types).
Grams StorageCapacityGrams(const UnitRow& unit, const ProductionConfig& config) {
  if (unit.type.value >= config.unit_types.size()) {
    return 0;
  }
  return static_cast<Grams>(config.unit_types[unit.type.value].storage_capacity_kg) *
         kGramsPerKilogram;
}

/// @brief Takes up to `wanted` grams of `resource` from any storing unit;
/// returns what was actually taken.
Grams TakeFromStorage(WorldState& world,
                      const ProductionConfig& config,
                      ResourceId resource,
                      Grams wanted) {
  Grams taken = 0;
  for (std::uint32_t row = 0; row < world.units.rows.size() && taken < wanted; ++row) {
    UnitRow& unit = world.units.rows[row];
    if (unit.type.value >= config.unit_types.size() ||
        config.unit_types[unit.type.value].storage_capacity_kg <= 0.0F) {
      continue;
    }
    const Grams here = StockOf(unit.stock, resource);
    const Grams take = here < wanted - taken ? here : wanted - taken;
    AddToStock(unit.stock, resource, -take);
    taken += take;
  }
  return taken;
}

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
    RunHerds(current);
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
  void RunYearStart(WorldState& current) const {
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

  /// Feeding and manure, daily. Herds at units eat from their unit's stock
  /// (shortage has no consequence yet — a stage-6 STUB); herds at family
  /// yards feed themselves and their manure leaks to the owners (start
  /// canon), so neither is booked. Manure flows to the compost heap,
  /// clamped at its capacity.
  void RunHerds(WorldState& current) const {
    const bool winter = current.calendar.season == Season::kWinter;
    const std::uint32_t heap = FindUnitRowOfType(current, config_.compost_heap_type);
    for (const HerdRow& herd : current.herds.rows) {
      if (herd.unit.value == kInvalidEntityIdValue || herd.kind.value >= config_.livestock.size()) {
        continue;
      }
      const LivestockDef& kind = config_.livestock[herd.kind.value];
      const std::uint32_t home = FindRow(current.units, herd.unit);
      if (home == kNoRow) {
        continue;
      }
      const auto heads = static_cast<float>(herd.adult_count);
      if (winter && kind.hay_kg_per_day_winter > 0.0F) {
        const auto need =
            static_cast<Grams>(kind.hay_kg_per_day_winter * heads) * kGramsPerKilogram;
        const Grams have = StockOf(current.units.rows[home].stock, config_.hay_resource);
        AddToStock(
            current.units.rows[home].stock, config_.hay_resource, -(have < need ? have : need));
      }
      if (heap != kNoRow && kind.manure_kg_per_year > 0.0F) {
        // Multiply into grams BEFORE the cast: a hen makes far less than a
        // kilogram a day, and casting kilograms first truncated her to zero
        // forever.
        const auto daily =
            static_cast<Grams>(kind.manure_kg_per_year * heads / static_cast<float>(kDaysPerYear) *
                               static_cast<float>(kGramsPerKilogram));
        UnitRow& compost = current.units.rows[heap];
        const Grams capacity = StorageCapacityGrams(compost, config_);
        const Grams held = StockOf(compost.stock, config_.manure_resource);
        const Grams room = capacity > held ? capacity - held : 0;
        AddToStock(compost.stock, config_.manure_resource, daily < room ? daily : room);
      }
    }
  }

  ProductionConfig config_;

  FieldGrowthPhase phase_;
};

/// @brief ResourceId by key of the resources table; invalid when absent.
ResourceId ResourceByKey(const ITable* resources, std::string_view key) {
  if (resources == nullptr) {
    return ResourceId{};
  }
  const std::uint32_t row = resources->FindRowByKey(key);
  return row == kNoTableRow ? ResourceId{} : ResourceId{static_cast<std::uint16_t>(row)};
}

/// @brief Reads a cell as float with `fallback` for an empty cell; a present
/// non-numeric cell fails the parse. The value is range-checked here, at
/// parse time: everything downstream casts these numbers to integers or
/// multiplies them into grams, and a NaN, an infinity or an absurd
/// magnitude would be undefined behaviour there instead of a clear error.
bool CellOrDefault(const ITable& table,
                   std::uint32_t row,
                   std::uint32_t column,
                   float fallback,
                   float low,
                   float high,
                   float& value,
                   std::string& error) {
  if (column == kNoTableColumn || table.CellText(row, column).empty()) {
    value = fallback;
    return true;
  }
  const std::optional<float> cell = table.CellReal(row, column);
  if (!cell) {
    error = "a cell is not a number";
    return false;
  }
  // Written as a positive test so that NaN fails it: NaN compares false
  // against everything, including itself.
  if (!(*cell >= low && *cell <= high)) {
    error = "a cell is out of range";
    return false;
  }
  value = *cell;
  return true;
}

bool ParseCrops(const ITable& table,
                const ITable* resources,
                std::vector<CropDef>& crops,
                std::string& error) {
  const std::uint32_t resource_col = table.FindColumn("resource");
  if (resource_col == kNoTableColumn) {
    error = "crops: no resource column";
    return false;
  }

  struct Column {
    const char* name;
    float fallback;
    float low;  ///< Inclusive bounds, checked at parse time.
    float high;
  };

  // Months are 1..12 here, so the shift to the core's 0-based Month enum
  // below can never produce a negative value.
  constexpr std::array<Column, 15> kColumns = {{{"is_winter", 0, 0, 1},
                                                {"is_perennial", 0, 0, 1},
                                                {"sow_from_month", 1, 1, 12},
                                                {"sow_to_month", 1, 1, 12},
                                                {"sow_min_temp_c", 0, -50, 50},
                                                {"harvest_from_month", 1, 1, 12},
                                                {"harvest_to_month", 1, 1, 12},
                                                {"harvest_min_temp_c", 0, -50, 50},
                                                {"yield_kg_per_ha", 0, 0, 1e6F},
                                                {"sowing_norm_kg_per_ha", 0, 0, 1e6F},
                                                {"fertility_delta", 0, -100, 100},
                                                {"drought_sensitivity", 0, 0, 1},
                                                {"wet_sensitivity", 0, 0, 1},
                                                // REAL man-days per hectare, as the
                                                // agronomy books write them; the grain
                                                // anchor 3 + 8 is the default until the
                                                // columns exist.
                                                {"sow_days_per_ha", 3, 0, 1000},
                                                {"harvest_days_per_ha", 8, 0, 1000}}};
  std::array<std::uint32_t, kColumns.size()> columns{};
  for (std::uint32_t index = 0; index < kColumns.size(); ++index) {
    columns[index] = table.FindColumn(kColumns[index].name);
  }
  crops.resize(table.RowCount());
  for (std::uint32_t row = 0; row < table.RowCount(); ++row) {
    std::array<float, kColumns.size()> values{};
    for (std::uint32_t index = 0; index < kColumns.size(); ++index) {
      if (!CellOrDefault(table,
                         row,
                         columns[index],
                         kColumns[index].fallback,
                         kColumns[index].low,
                         kColumns[index].high,
                         values[index],
                         error)) {
        error = "crops: " + error;
        return false;
      }
    }
    CropDef& crop = crops[row];
    crop.resource = ResourceByKey(resources, table.CellText(row, resource_col));
    crop.is_winter = values[0] != 0.0F;
    crop.is_perennial = values[1] != 0.0F;
    // Table months are human 1..12; the core's Month enum is 0-based.
    crop.sow_from_month = static_cast<std::uint8_t>(values[2] - 1.0F);
    crop.sow_to_month = static_cast<std::uint8_t>(values[3] - 1.0F);
    crop.sow_min_temp_c = values[4];
    crop.harvest_from_month = static_cast<std::uint8_t>(values[5] - 1.0F);
    crop.harvest_to_month = static_cast<std::uint8_t>(values[6] - 1.0F);
    crop.harvest_min_temp_c = values[7];
    crop.yield_kg_per_ha = values[8];
    crop.sowing_norm_kg_per_ha = values[9];
    crop.fertility_delta = values[10];
    crop.drought_sensitivity = values[11];
    crop.wet_sensitivity = values[12];
    crop.sow_days_per_ha = values[13] / kRealDaysPerGameDay;
    crop.harvest_days_per_ha = values[14] / kRealDaysPerGameDay;
  }
  return true;
}

bool ParseLivestock(const ITable& table, std::vector<LivestockDef>& livestock, std::string& error) {
  const std::uint32_t manure_col = table.FindColumn("manure_kg_per_year");
  const std::uint32_t hay_col = table.FindColumn("hay_kg_per_day_winter");
  const std::uint32_t grain_col = table.FindColumn("grain_kg_per_day");
  livestock.resize(table.RowCount());
  for (std::uint32_t row = 0; row < table.RowCount(); ++row) {
    LivestockDef& kind = livestock[row];
    if (!CellOrDefault(table, row, manure_col, 0, 0, 1e6F, kind.manure_kg_per_year, error) ||
        !CellOrDefault(table, row, hay_col, 0, 0, 1e4F, kind.hay_kg_per_day_winter, error) ||
        !CellOrDefault(table, row, grain_col, 0, 0, 1e4F, kind.grain_kg_per_day, error)) {
      error = "livestock: " + error;
      return false;
    }
  }
  return true;
}

bool ParseUnitTypes(const ITable& table, std::vector<UnitTypeDef>& types, std::string& error) {
  const std::uint32_t storage_col = table.FindColumn("storage_capacity_kg");
  const std::uint32_t livestock_col = table.FindColumn("livestock_capacity_head");
  types.resize(table.RowCount());
  for (std::uint32_t row = 0; row < table.RowCount(); ++row) {
    UnitTypeDef& type = types[row];
    if (!CellOrDefault(table, row, storage_col, 0, 0, 1e9F, type.storage_capacity_kg, error) ||
        !CellOrDefault(
            table, row, livestock_col, 0, 0, 1e6F, type.livestock_capacity_head, error)) {
      error = "unit_types: " + error;
      return false;
    }
  }
  return true;
}

bool ParseFarming(const ITable& table, FarmingConfig& farming, std::string& error) {
  const std::uint32_t value_col = table.FindColumn("value");
  if (value_col == kNoTableColumn) {
    error = "farming: no value column";
    return false;
  }

  struct Entry {
    const char* key;
    float* value;
  };

  const Entry entries[] = {{"fertility_neutral", &farming.fertility_neutral},
                           {"manure_norm_kg_per_ha", &farming.manure_norm_kg_per_ha},
                           {"manure_fertility_bonus", &farming.manure_fertility_bonus},
                           {"fallow_recovery", &farming.fallow_recovery},
                           {"repeat_penalty_per_year", &farming.repeat_penalty_per_year},
                           {"drought_temp_c", &farming.drought_temp_c},
                           {"stress_per_day", &farming.stress_per_day},
                           {"stress_cap", &farming.stress_cap}};
  for (const Entry& entry : entries) {
    const std::uint32_t row = table.FindRowByKey(entry.key);
    const std::optional<float> cell = table.CellReal(row, value_col);
    if (row == kNoTableRow || !cell) {
      error = std::string("farming: row '") + entry.key + "' is missing or not numeric";
      return false;
    }
    // Positive test: NaN and the infinities fail it (see CellOrDefault).
    if (!(*cell >= -1.0e6F && *cell <= 1.0e6F)) {
      error = std::string("farming: value of '") + entry.key + "' is out of range";
      return false;
    }
    *entry.value = *cell;
  }
  // The two labor norms are optional while the column set grows: their
  // defaults are the canonical 10 and 3 real man-days per hectare.
  const Entry optional[] = {{"plow_days_per_ha", &farming.plow_days_per_ha},
                            {"harrow_days_per_ha", &farming.harrow_days_per_ha}};
  for (const Entry& entry : optional) {
    const std::uint32_t row = table.FindRowByKey(entry.key);
    if (row == kNoTableRow) {
      continue;
    }
    const std::optional<float> cell = table.CellReal(row, value_col);
    const bool sane = cell && *cell >= 0.0F && *cell <= 1000.0F;
    if (!sane) {
      error = std::string("farming: value of '") + entry.key + "' is missing or out of range";
      return false;
    }
    *entry.value = *cell / kRealDaysPerGameDay;  // the table keeps REAL man-days
  }
  if (!(farming.fertility_neutral > 0.0F)) {
    error = "farming: fertility_neutral must be positive";
    return false;
  }
  return true;
}

}  // namespace

std::unique_ptr<IProductionSystem> CreateProductionSystem(const ITableSet& tables) {
  ProductionConfig config;
  std::string error;
  const ITable* resources = tables.FindTable("resources");
  if (const ITable* crops = tables.FindTable("crops")) {
    if (!ParseCrops(*crops, resources, config.crops, error)) {
      LogError(error);
      return nullptr;
    }
  }
  if (const ITable* livestock = tables.FindTable("livestock")) {
    if (!ParseLivestock(*livestock, config.livestock, error)) {
      LogError(error);
      return nullptr;
    }
  }
  if (const ITable* unit_types = tables.FindTable("unit_types")) {
    if (!ParseUnitTypes(*unit_types, config.unit_types, error)) {
      LogError(error);
      return nullptr;
    }
    const std::uint32_t heap = unit_types->FindRowByKey("compost_heap");
    if (heap != kNoTableRow) {
      config.compost_heap_type = UnitTypeId{static_cast<std::uint16_t>(heap)};
    }
  }
  if (const ITable* farming = tables.FindTable("farming")) {
    if (!ParseFarming(*farming, config.farming, error)) {
      LogError(error);
      return nullptr;
    }
  }
  config.manure_resource = ResourceByKey(resources, "manure");
  config.hay_resource = ResourceByKey(resources, "hay");
  return std::make_unique<ProductionSystem>(config);
}

}  // namespace core
