/// @file
/// @brief The cemetery the run's chairman marks when the world has none built
/// (#1199's run line; boss, boss-core-epoch1-resume [23], [25]; since #1367
/// core-boss-1421-arms [8]).
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THE RUN BUILDS THIS. Until #1367 a full cemetery stopped the game for
/// the player's new plot, and a run marked one at 90 % fill. Since #1367 the
/// capacity is unlimited and there is no stop (social.md «Где хоронят»), so
/// the fill trigger is dropped. What remains: a world with NO cemetery built
/// (the canon's old one is never taken down, so the canon never meets it)
/// keeps its dead waiting; the chairman marks one at the elder's point
/// (suggestions.csv suggest_cemetery_new_01), else the nearest free plot, and
/// starts it the next day. Its cost is nil (unit_levels cemetery 1: plot). A
/// run whose dead do wait names it in its report.

#ifndef TESTS_RUN_COMMON_CEMETERY_POLICY_H_
#define TESTS_RUN_COMMON_CEMETERY_POLICY_H_

#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_catalog/definitions.h"
#include "core_common/cemetery_rules.h"
#include "core_common/order_state.h"
#include "core_common/plot.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/stub_tables.h"
#include "core_tables/tables.h"
#include "core_world/world.h"
#include "suggested_place.h"

namespace run {

/// @brief Marks and starts a new cemetery when the built ones are nearly full.
class CemeteryPolicy {
 public:
  explicit CemeteryPolicy(const core::ITableSet& tables)
      : suggested_(tables, core::kCemeteryTypeKey) {
    const core::ITable* types = tables.FindTable("unit_types");
    const std::uint32_t row =
        types == nullptr ? core::kNoTableRow : types->FindRowByKey(core::kCemeteryTypeKey);
    cemetery_ = row == core::kNoTableRow ? core::UnitTypeId{}
                                         : core::UnitTypeId{static_cast<std::uint16_t>(row)};
    std::string error;
    core::LoadDefinitions(tables, core::StubTables::kAllowed, definitions_, error);
  }

  /// @brief One day of the chairman's attention. Call once a day.
  void RunDay(core::ISimulation& simulation) {
    if (cemetery_.value == core::kInvalidDefIdValue) {
      return;
    }
    const core::WorldState& world = simulation.CompletedState();
    std::vector<core::OrderRow> orders;
    bool one_built = false;
    bool one_coming = false;
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      if (unit.type.value != cemetery_.value) {
        continue;
      }
      if (unit.level == 0) {
        one_coming = true;
        if (unit.construction.phase == core::ConstructionPhase::kMarked) {
          core::OrderRow start;
          start.kind = core::OrderKind::kStartBuild;
          start.unit = world.units.row_ids[row];
          orders.push_back(start);
          ++started_;
        }
        continue;
      }
      one_built = true;
    }
    if (world.burials_waiting > 0) {
      ++held_days_;
    }
    // NO FILL TRIGGER SINCE #1367 (boss, core-boss-1421-arms [8]): with no area
    // limit a 90 % mark measured nothing the rule has, and a second cemetery
    // kept by habit would make burial by road look tested where the player has
    // one. Only a world with NO cemetery built gets one.
    if (!one_coming && cooldown_ == 0 && (!one_built || world.burials_waiting > 0)) {
      core::OrderRow mark;
      mark.kind = core::OrderKind::kBuildUnit;
      mark.unit_type = cemetery_;
      const std::vector<float>& keep_out = definitions_.units.keep_out_radius_m;
      const float radius = cemetery_.value < keep_out.size() ? keep_out[cemetery_.value] : 0.0F;
      // ON THE ELDER'S POINT FIRST (suggested_place.h).
      const std::optional<core::Vec2> point = suggested_.Take(world, definitions_.Plots(), radius);
      mark.position =
          point ? *point : core::FreePlot(world.units, definitions_.Plots(), OldOne(world), radius);
      orders.push_back(mark);
      ++marked_;
      if (first_mark_day_ < 0) {
        first_mark_day_ = static_cast<std::int64_t>(world.calendar.day);
      }
      cooldown_ = kCooldownDays;
    } else if (cooldown_ > 0) {
      --cooldown_;
    }
    if (!orders.empty()) {
      simulation.StageOrders(std::span<const core::OrderRow>(orders.data(), orders.size()), {});
    }
  }

  /// @brief The fixture difference, in words, BEFORE the run measures.
  static void Declare(std::string_view run_name) {
    std::cout << run_name
              << ": FIXTURE DIFFERS FROM THE START CANON — the run's chairman marks a "
                 "CEMETERY only when none is built, on the elder's point first, and starts it "
                 "the next day; since #1367 no fill trigger (capacity unlimited, burial at the "
                 "nearest by road; boss, 2 November 2026)\n";
  }

  /// @brief What the fixture did, for the run to print at the end.
  void Report(std::string_view run_name) const {
    std::cout << run_name << ": cemeteries — marked " << marked_ << " (first on day "
              << first_mark_day_ << "), start orders " << started_ << "; days the dead waited "
              << held_days_
              << (held_days_ > 0 ? " — A RUN'S DEAD WAITED: NO CEMETERY WAS BUILT" : "") << '\n';
    suggested_.Report(run_name);
  }

 private:
  static constexpr std::uint32_t kCooldownDays = 2;

  /// The first built cemetery's place, the old one's: where a new one is
  /// sought when the elder's point is taken.
  core::Vec2 OldOne(const core::WorldState& world) const {
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.type.value == cemetery_.value && unit.level > 0) {
        return unit.position;
      }
    }
    return core::Vec2{};
  }

  core::UnitTypeId cemetery_;
  core::Definitions definitions_;
  SuggestedPlaces suggested_;
  std::uint32_t marked_ = 0;
  std::uint32_t started_ = 0;
  std::uint32_t held_days_ = 0;
  std::int64_t first_mark_day_ = -1;
  std::uint32_t cooldown_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_CEMETERY_POLICY_H_
