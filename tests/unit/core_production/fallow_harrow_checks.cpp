// The checks of the fresh fallow's harrow (fallow_harrow_checks.h; #1837).
//
// THE KNOWN ANSWERS, by hand: a field whose chain is (fallow, winter rye) stands bare in growing,
// its bare harrow ended on day D, the rye's window open, rye in the barn, warm. On day D + 1 the
// black fallow's turn opens the rye's SOWING (no second harrow). On day D + 10 (past 4 days) it
// opens a HARROW at its norm. With the knob at 0 a day-after field also gets the harrow.

#include "fallow_harrow_checks.h"

#include <cstdint>
#include <iostream>
#include <string>

#include "core_common/calendar.h"
#include "core_common/ids.h"
#include "core_common/land_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "field_work.h"
#include "production_config.h"
#include "stock_ops.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: fresh fallow: " << label << '\n';
    return 1;
  }
  return 0;
}

core::CropId WinterCropOf(const core::ProductionConfig& config, core::ResourceId resource) {
  for (std::size_t index = 0; index < config.crops.size(); ++index) {
    if (config.crops[index].resource == resource && config.crops[index].is_winter != 0 &&
        config.crops[index].sowing_norm_kg_per_ha > 0.0F) {
      return core::DefIdFromIndex<core::CropIdTag>(index);
    }
  }
  return core::CropId{};
}

void PutInTheBarn(const core::ProductionConfig& config,
                  core::WorldState& world,
                  core::ResourceId resource,
                  core::Grams grams) {
  core::UnitRow barn;
  barn.level = 1;
  for (std::uint32_t type = 0; type < config.unit_types.size(); ++type) {
    barn.type = core::UnitTypeId{static_cast<std::uint16_t>(type)};
    if (core::StoresGoods(barn, config) && core::StorageCapacityGrams(barn, config) > 0 &&
        core::TypeIsHomeOf(barn, config, resource)) {
      break;
    }
  }
  barn.stock.assign(static_cast<std::size_t>(resource.value) + 1U, 0);
  barn.stock[resource.value] = grams;
  core::AppendRow(world.units, barn);
}

/// The rye's window's first day of year one, the bare fallow harrowed `days_before` it, today.
core::WorldState FallowWorld(const core::ProductionConfig& config,
                             core::CropId rye,
                             std::uint32_t days_since_harrow) {
  core::WorldState world;
  const std::uint32_t window_day =
      static_cast<std::uint32_t>(config.crops[rye.value].sow_from_month) * core::kDaysPerMonth;
  world.calendar.tick = static_cast<core::Tick>(window_day + 1U) * core::kTicksPerDay;
  core::RefreshCalendarCaches(world.calendar);
  world.weather.air_temperature_celsius = 15.0F;
  core::FieldRow field;
  field.kind = core::LandKind::kArable;
  field.area_ga = 10.0F;
  field.phase = core::FieldPhase::kGrowing;
  field.crop = core::CropId{};  // bare: this year's slot is the fallow
  field.rotation_year1 = rye;
  field.rotation_assigned = 1;
  field.bare_harrowed_day = world.calendar.day - days_since_harrow;
  core::AppendRow(world.fields, field);
  PutInTheBarn(config, world, config.crops[rye.value].resource, 10'000'000);
  return world;
}

core::FieldRow Turned(const core::ProductionConfig& config, core::WorldState& world) {
  core::FieldRow& field = world.fields.rows[0];
  core::TrySowOnBlackFallow(config,
                            world,
                            field,
                            static_cast<std::uint8_t>(world.calendar.date.month),
                            world.weather.air_temperature_celsius);
  return field;
}

}  // namespace

int CheckTheFreshFallowIsNotHarrowedTwice() {
  std::string error;
  const auto tables = core::LoadTableSet(KOLKHOZ_TABLES_DIR, &error);
  core::ProductionConfig config;
  if (Expect(tables != nullptr && core::ParseProductionConfig(*tables, config, error),
             "the shipped tables parse") != 0) {
    std::cout << error << '\n';
    return 1;
  }
  const core::ITable* const resources = tables->FindTable("resources");
  const std::uint32_t rye_row =
      resources == nullptr ? core::kNoTableRow : resources->FindRowByKey("rye");
  const core::CropId rye =
      rye_row == core::kNoTableRow
          ? core::CropId{}
          : WinterCropOf(config, core::DefIdFromRow<core::ResourceIdTag>(rye_row));
  if (Expect(rye.value != core::kInvalidDefIdValue, "the tables name a winter rye") != 0) {
    return 1;
  }
  int failures = 0;
  failures += Expect(config.farming.fallow_fresh_harrow_days == 4, "the STUB knob reads 4");
  {
    core::WorldState world = FallowWorld(config, rye, 1);
    const core::FieldRow field = Turned(config, world);
    std::cout << "  day after: phase " << static_cast<int>(field.phase) << ", seam "
              << field.work_days_remaining << '\n';
    failures += Expect(field.phase == core::FieldPhase::kSowing && field.crop == rye &&
                           field.work_days_remaining > 0.0F,
                       "harrowed the day before: the rye's sowing opens, no second harrow");
  }
  {
    core::WorldState world = FallowWorld(config, rye, 10);
    const core::FieldRow field = Turned(config, world);
    failures +=
        Expect(field.phase == core::FieldPhase::kHarrowing && field.work_days_remaining > 0.0F,
               "harrowed ten days before: the harrow reopens at its norm");
  }
  {
    core::ProductionConfig off = config;
    off.farming.fallow_fresh_harrow_days = 0;
    core::WorldState world = FallowWorld(off, rye, 1);
    const core::FieldRow field = Turned(off, world);
    failures += Expect(field.phase == core::FieldPhase::kHarrowing,
                       "the knob at 0: the day-after field is harrowed again, as before");
  }
  return failures;
}
