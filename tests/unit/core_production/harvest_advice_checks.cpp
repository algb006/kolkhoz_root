// The checks of the store lamp's move (harvest_advice_checks.h).

#include "harvest_advice_checks.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/calendar.h"
#include "core_common/land_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_production/production_system.h"
#include "core_tables/tables.h"

namespace {

constexpr core::Grams kTonne = 1'000'000;

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

/// The rye's lamps of `world`: each one's advice, type, amount and sum.
std::vector<core::Alarm> RyeLamps(const core::IProductionSystem& system,
                                  const core::WorldState& world) {
  std::vector<core::Alarm> alarms;
  system.CollectAlarms(world, alarms);
  std::vector<core::Alarm> rye;
  for (const core::Alarm& alarm : alarms) {
    if (alarm.kind == core::AlarmKind::kHarvestWillNotFit && alarm.resource.value == 0) {
      rye.push_back(alarm);
    }
  }
  return rye;
}

core::FieldRow RyeField(float hectares) {
  core::FieldRow field;
  field.kind = core::LandKind::kArable;
  field.area_ga = hectares;
  field.fertility = 50.0F;
  field.phase = core::FieldPhase::kGrowing;
  field.crop = core::CropId{0};
  return field;
}

}  // namespace

int CheckTheStoreLampsMove() {
  int failures = 0;
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "unit_core_production_store_lamps_move";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  std::ofstream(root / "resources.csv") << "key,feed_value\nrye,1.15\npotato,0.3\n";
  std::ofstream(root / "crops.csv")
      << "key,resource,is_winter,is_perennial,sow_from_month,sow_to_month,sow_min_temp_c,"
         "growth_min_temp_c,harvest_from_month,harvest_to_month,harvest_min_temp_c,"
         "yield_kg_per_ha,sowing_norm_kg_per_ha,fertility_delta,drought_sensitivity,"
         "wet_sensitivity,sow_days_per_ha,harvest_days_per_ha,straw_ratio\n"
         "rye,rye,0,0,4,5,5,5,8,8,2,1000,0,-1,0,0,3,8,0\n";
  std::ofstream(root / "farming.csv")
      << "key,value\nfertility_neutral,50\nmanure_norm_kg_per_ha,20000\n"
         "manure_fertility_bonus,10\nfallow_recovery,6\nrepeat_penalty_per_year,3\n"
         "drought_temp_c,25\nstress_per_day,0.02\nstress_cap,0.3\n"
         "weather_state_days,5\n";
  std::ofstream(root / "unit_types.csv") << "key,capacity_by_plot\ngranary,0\n";
  std::ofstream(root / "unit_levels.csv") << "unit,level,storage_capacity_t\ngranary,1,150\n";
  std::ofstream(root / "resource_stores.csv") << "resource,unit,storage\nrye,granary,granary\n";
  std::string error;
  const auto tables = core::LoadTableSet(root.string(), &error);
  const auto system = tables == nullptr
                          ? nullptr
                          : core::CreateProductionSystem(*tables, core::StubTables::kAllowed);
  if (Expect(system != nullptr, "store lamp's move: the table set builds a production system") !=
      0) {
    std::cout << error << '\n';
    return 1;
  }

  // 1. No store at all, three fields of 20 t: each field alone is under the
  // third (50 t of a 150 t granary), the village's 60 t is over it.
  core::WorldState world;
  core::RefreshCalendarCaches(world.calendar);
  for (int field = 0; field < 3; ++field) {
    core::AppendRow(world.fields, RyeField(20.0F));
  }
  std::vector<core::Alarm> lamps = RyeLamps(*system, world);
  bool all_build = lamps.size() == 3;
  for (const core::Alarm& lamp : lamps) {
    all_build = all_build && lamp.advice == core::AlarmAdvice::kBuildStore &&
                lamp.advice_unit_type.value == 0 && lamp.amount == 20 * kTonne &&
                lamp.amount_more == 60 * kTonne;
  }
  failures += Expect(all_build,
                     "store lamp's move: 60 t summed over three fields of 20 t reaches the third "
                     "- every lamp of the rye says «build a granary», the sum beside each");

  // 2. Two fields, 40 t: under the third. No plan: no move, the loss alone.
  world.fields.rows.pop_back();
  world.fields.row_ids.pop_back();
  lamps = RyeLamps(*system, world);
  bool all_none = lamps.size() == 2;
  for (const core::Alarm& lamp : lamps) {
    all_none =
        all_none && lamp.advice == core::AlarmAdvice::kNone && lamp.amount_more == 40 * kTonne;
  }
  failures += Expect(all_none,
                     "store lamp's move: 40 t is under the third and no plan is named - no "
                     "store advised, no move");

  // 3. The same 40 t, a full granary of rye and the rye a plan position: the
  // plan door, the shortfall capped at the stores.
  core::UnitRow granary;
  granary.type = core::UnitTypeId{0};
  granary.level = 1;
  granary.stock = {150 * kTonne, 0};
  core::AppendRow(world.units, granary);
  world.plan.announced = 1;
  world.plan.due = {10 * kTonne, 0};
  lamps = RyeLamps(*system, world);
  bool all_deliver = lamps.size() == 2;
  for (const core::Alarm& lamp : lamps) {
    all_deliver = all_deliver && lamp.advice == core::AlarmAdvice::kDeliverAbovePlan &&
                  lamp.advice_resource.value == 0 && lamp.advice_amount == 40 * kTonne;
  }
  failures += Expect(all_deliver,
                     "store lamp's move: under the third with the rye a plan position and in the "
                     "stores - «deliver 40 t above the plan»");
  return failures;
}
