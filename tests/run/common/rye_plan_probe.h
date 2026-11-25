/// @file
/// @brief RYE2157 (#2157, host's football factor; boss's queue 25 Nov): in year 1, on chosen days,
///        the rye's plan rung and what feeds it - the due and the delivered, the rye lying, the
///        harvest the rung discounts the owed against (IProductionSystem::HarvestToComeThisYear),
///        next year's hold (NextYearHold) and the turn's seal (TurnPlanSeal), and every field whose
///        crop or first rotation slot is winter rye. Question: does the forecast count a winter
///        rye that cannot land before the turn (host: seed 9 reaps no rye in year 1)?
///        A probe print, test-side: the world is unchanged.
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.

#ifndef TESTS_RUN_COMMON_RYE_PLAN_PROBE_H_
#define TESTS_RUN_COMMON_RYE_PLAN_PROBE_H_

#include <algorithm>
#include <cstdint>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/fund_ladder.h"
#include "core_common/land_state.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_production/production_system.h"
#include "core_tables/tables.h"

namespace run {

class RyePlanProbe {
 public:
  explicit RyePlanProbe(const core::ITableSet& tables)
      : production_(core::CreateProductionSystem(tables, core::StubTables::kAllowed)) {
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      rye_ = resources->FindRowByKey("rye");
    }
    if (const core::ITable* const crops = tables.FindTable("crops")) {
      rye_crop_ = crops->FindRowByKey("rye_winter");
    }
    if (const core::ITable* const types = tables.FindTable("unit_types")) {
      church_ = types->FindRowByKey("church_store");
      food_yard_ = types->FindRowByKey("food_yard");
      granary_ = types->FindRowByKey("granary");
    }
  }

  /// @brief Once a day, after the day's step; prints nothing past year 1.
  void CountDay(const core::WorldState& world) {
    const std::uint32_t day = world.calendar.day;
    if (production_ == nullptr || rye_ == core::kNoTableRow || day >= core::kDaysPerYear) {
      return;
    }
    // The chosen days, 1-based as host's probe counts them: around the rye's season and turn.
    // EVERY DAY of year 1 (boss's church read needs the day the food yard stands and the
    // church starts emptying).
    const std::size_t rye = rye_;
    const auto at = [rye](const core::ResourceAmounts& amounts) -> core::Grams {
      return rye < amounts.size() ? amounts[rye] : 0;
    };
    const core::ResourceAmounts to_come = production_->HarvestToComeThisYear(world);
    const core::ResourceAmounts next_year = production_->NextYearHold(world);
    const core::ResourceAmounts seal = production_->TurnPlanSeal(world);
    core::Grams lying = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      lying += rye < unit.stock.size() ? unit.stock[rye] : 0;
    }
    std::ostringstream line;
    line << " day " << (day + 1U) << " ground=year1;rye_kg;rung_without_seed_part(held_above=0)"
         << " due " << Kg(at(world.plan.due)) << " delivered " << Kg(at(world.plan.delivered))
         << " lying " << Kg(lying) << " harvest_to_come " << Kg(at(to_come)) << " rung "
         << Kg(core::PlanRungGrams(world, rye, core::ResourceId{}, 0, at(to_come)))
         << " rung_whole_owed " << Kg(core::PlanRungGrams(world, rye)) << " next_year_hold "
         << Kg(at(next_year)) << " turn_seal " << Kg(at(seal)) << " rye_fields";
    std::uint32_t fields = 0;
    for (std::size_t row = 0; row < world.fields.rows.size(); ++row) {
      const core::FieldRow& field = world.fields.rows[row];
      const bool rye_now = field.crop.value == rye_crop_;
      const bool rye_slot = field.rotation_year0.value == rye_crop_;
      if (!rye_now && !rye_slot) {
        continue;
      }
      ++fields;
      line << " [row " << row << " ha " << (field.area_ga / 100.0F) << " crop_is_rye " << rye_now
           << " slot0_is_rye " << rye_slot << " phase " << static_cast<int>(field.phase)
           << " reaped_kg " << Kg(field.reaped_grams) << "]";
    }
    line << " count " << fields;
    // boss's #2157 read: the church's rye, its emptying, and the stores that could take rye.
    core::Grams church_rye = 0;
    int church_emptying = 0;
    int food_yard = 0;
    int granary = 0;
    int granary_site_phase = -1;  // -1: no granary site going up
    float granary_site_left = 0.0F;
    for (const core::UnitRow& unit : world.units.rows) {
      const bool built =
          unit.level >= 1 && unit.construction.phase == core::ConstructionPhase::kNone;
      if (unit.type.value == church_) {
        church_rye += rye < unit.stock.size() ? unit.stock[rye] : 0;
        church_emptying = std::max(church_emptying, static_cast<int>(unit.emptying));
      }
      food_yard += built && unit.type.value == food_yard_ ? 1 : 0;
      granary += built && unit.type.value == granary_ ? 1 : 0;
      // #2188: the granary's site, while it goes up - its phase and the labour left.
      if (unit.type.value == granary_ && !built) {
        granary_site_phase = static_cast<int>(unit.construction.phase);
        granary_site_left = unit.construction.labor_days_remaining;
      }
    }
    line << " church_rye " << Kg(church_rye) << " church_emptying " << church_emptying
         << " food_yards " << food_yard << " granaries " << granary << " granary_site_phase "
         << granary_site_phase << " granary_site_labor_left " << granary_site_left;
    lines_.push_back(line.str());
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    for (const std::string& line : lines_) {
      out << "population_curve: RYE2157 seed " << seed << line << '\n';
    }
  }

 private:
  static double Kg(core::Grams grams) { return static_cast<double>(grams) / 1000.0; }

  std::unique_ptr<core::IProductionSystem> production_;
  std::uint32_t rye_ = core::kNoTableRow;
  std::uint32_t rye_crop_ = core::kNoTableRow;
  std::uint32_t church_ = core::kNoTableRow;
  std::uint32_t food_yard_ = core::kNoTableRow;
  std::uint32_t granary_ = core::kNoTableRow;
  std::vector<std::string> lines_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_RYE_PLAN_PROBE_H_
