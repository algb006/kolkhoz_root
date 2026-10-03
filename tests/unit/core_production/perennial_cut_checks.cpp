// The checks of the perennial's cut (perennial_cut_checks.h).

#include "perennial_cut_checks.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include "core_common/calendar.h"
#include "core_common/land_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_production/production_system.h"
#include "core_tables/tables.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

/// The grass's harvest window: months 6-8 of the file, 0-based 5-7 — days
/// 20-31 of the year at four days a month.
constexpr core::SimDay kWindowOpens = 20;
constexpr core::SimDay kWindowShuts = 32;

std::unique_ptr<core::IProductionSystem> Production() {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "unit_core_production_perennial_cut";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  std::ofstream(root / "resources.csv") << "key,feed_value\nhay,0.5\n";
  std::ofstream(root / "crops.csv")
      << "key,resource,is_winter,is_perennial,sow_from_month,sow_to_month,sow_min_temp_c,"
         "growth_min_temp_c,harvest_from_month,harvest_to_month,harvest_min_temp_c,"
         "yield_kg_per_ha,sowing_norm_kg_per_ha,fertility_delta,drought_sensitivity,"
         "wet_sensitivity,sow_days_per_ha,harvest_days_per_ha,straw_ratio\n"
         "grass,hay,0,1,4,5,5,4,6,8,2,2000,0,3,0.3,0.2,2,8,0\n";
  std::ofstream(root / "farming.csv")
      << "key,value\nfertility_neutral,50\nmanure_norm_kg_per_ha,20000\n"
         "manure_fertility_bonus,10\nfallow_recovery,6\nrepeat_penalty_per_year,3\n"
         "drought_temp_c,25\nstress_per_day,0.02\nstress_cap,0.3\nweather_state_days,5\n"
         "perennial_first_cut_days,4\n";
  std::ofstream(root / "weather.csv")
      << "key,temp_mean_c,temp_spread_c,temp_amplitude_c,precipitation_chance_percent\n"
         "winter,-10,2,3,35\nspring,5,7,5,35\nsummer,19,5,6,25\nautumn,6,7,5,45\n";
  std::string error;
  const auto tables = core::LoadTableSet(root.string(), &error);
  if (tables == nullptr) {
    std::cout << error << '\n';
    return nullptr;
  }
  return core::CreateProductionSystem(*tables, core::StubTables::kAllowed);
}

/// One grass field standing from `start`, sown on `sown` (kNeverSownDay: an
/// old stand), last cut on `cut` (kNeverReapedDay: never); run day by day
/// through the window. The first day its cut opened, or -1.
std::int64_t FirstCutDay(core::IProductionSystem& production,
                         core::SimDay start,
                         core::SimDay sown,
                         core::SimDay cut) {
  core::WorldState world;
  world.weather.air_temperature_celsius = 18.0F;
  core::FieldRow field;
  field.area_ga = 5.0F;
  field.crop = core::CropId{0};
  field.rotation_year0 = core::CropId{0};
  field.rotation_year1 = core::CropId{0};
  field.phase = core::FieldPhase::kGrowing;
  field.sown_day = sown;
  field.last_cut_day = cut;
  core::AppendRow(world.fields, field);
  for (core::SimDay day = start; day < kWindowShuts; ++day) {
    for (std::uint32_t hour = 0; hour < core::kTicksPerDay; ++hour) {
      const core::WorldState previous = world;
      world.calendar.tick = (static_cast<core::Tick>(day) * core::kTicksPerDay) + hour;
      core::RefreshCalendarCaches(world.calendar);
      world.weather.air_temperature_celsius = 18.0F;
      world.weather.precipitation = core::Precipitation::kNone;
      production.RunProductionDecisions(previous, world);
      if (world.fields.rows[0].phase == core::FieldPhase::kHarvest) {
        return static_cast<std::int64_t>(day);
      }
    }
  }
  return -1;
}

/// One meadow growing from `start`, last mown on `mown` (kNeverMownDay:
/// never); run day by day through June and July. The first day its cut
/// opened, or -1.
std::int64_t FirstMeadowCutDay(core::IProductionSystem& production,
                               core::SimDay start,
                               core::SimDay mown) {
  core::WorldState world;
  core::FieldRow meadow;
  meadow.kind = core::LandKind::kMeadow;
  meadow.area_ga = 5.0F;
  meadow.phase = core::FieldPhase::kGrowing;
  meadow.last_mown_day = mown;
  core::AppendRow(world.fields, meadow);
  for (core::SimDay day = start; day < kWindowShuts - 4; ++day) {
    for (std::uint32_t hour = 0; hour < core::kTicksPerDay; ++hour) {
      const core::WorldState previous = world;
      world.calendar.tick = (static_cast<core::Tick>(day) * core::kTicksPerDay) + hour;
      core::RefreshCalendarCaches(world.calendar);
      world.weather.air_temperature_celsius = 18.0F;
      world.weather.precipitation = core::Precipitation::kNone;
      production.RunProductionDecisions(previous, world);
      if (world.fields.rows[0].phase == core::FieldPhase::kHarvest) {
        return static_cast<std::int64_t>(day);
      }
    }
  }
  return -1;
}

}  // namespace

int CheckThePerennialsCut() {
  int failures = 0;
  const auto production = Production();
  if (Expect(production != nullptr, "perennial's cut: the tables build a production system") != 0) {
    return 1;
  }
  // GRASS SOWN ON DAY 1 OF ITS HARVEST MONTH (the timothy of seed 1934, year
  // 2): grown four days, it is cut in the same window — until 0.37.167 the
  // cut opened only on the window's first day and this stand stood a year.
  const std::int64_t late =
      FirstCutDay(*production, kWindowOpens + 1, kWindowOpens + 1, core::kNeverReapedDay);
  // AN OLD STAND is cut on the window's first day, as always.
  const std::int64_t old_stand =
      FirstCutDay(*production, kWindowOpens - 1, core::kNeverSownDay, core::kNeverReapedDay);
  // A STAND CUT THIS YEAR is not cut again in the same window.
  const std::int64_t twice =
      FirstCutDay(*production, kWindowOpens + 1, core::kNeverSownDay, kWindowOpens);
  std::cout << "  perennial's cut, the first day it opened: sown on day 21 - " << late
            << ", an old stand - " << old_stand << ", cut on day 20 already - " << twice << '\n';
  failures += Expect(late == kWindowOpens + 1 + 4,
                     "perennial's cut: grass sown on day 1 of its harvest month is cut in the "
                     "same window, after its four days of growth");
  failures += Expect(old_stand == kWindowOpens,
                     "perennial's cut: an old stand is cut on the window's first day");
  failures +=
      Expect(twice == -1, "perennial's cut: a stand cut this year is not cut again in the window");
  // THE MEADOW, THE SAME FORM (boss, the queue thread [117]): a meadow that
  // first stands growing on day 21 — marked mid-June — opens its cut that
  // day; one mown on day 20 is not opened again in June or July.
  const std::int64_t marked_late =
      FirstMeadowCutDay(*production, kWindowOpens + 1, core::kNeverMownDay);
  const std::int64_t mown = FirstMeadowCutDay(*production, kWindowOpens + 1, kWindowOpens);
  std::cout << "  meadow's cut, the first day it opened: growing from day 21 - " << marked_late
            << ", mown on day 20 already - " << mown << '\n';
  failures += Expect(marked_late == kWindowOpens + 1,
                     "meadow's cut: a meadow first growing on day 21 opens its cut that day, not "
                     "a year later");
  failures += Expect(mown == -1, "meadow's cut: a meadow mown this year is not opened again");
  return failures;
}
