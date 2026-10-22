/// @file
/// @brief The new cemetery the run's chairman marks when the old one is
/// nearly full (#1199's run line; boss, boss-core-epoch1-resume [23], [25]).
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THE RUN BUILDS THIS. With the old cemetery's own 14 m (41 graves) a run
/// reaches a full cemetery inside 12 years, and the game's stop (BurialHeld,
/// cemetery_rules.h) is the player's to answer: «игра останавливается, пока
/// председатель не выделит новый участок» (social.md «Два кладбища»). A run has
/// nobody to answer it, so the chairman marks one himself.
///
/// THE RULE (boss [25]): when the BUILT cemeteries hold ≥ 90 % of their graves
/// together, or the first dead waits for a grave, whichever comes first, and
/// no cemetery is marked or going up, mark one at the elder's point
/// (suggestions.csv suggest_cemetery_new_01), else at the nearest free plot to
/// the old one, and start it the next day. Its cost is nil (unit_levels
/// cemetery 1: plot), so no recipe holds it. A run whose dead do wait names it
/// in its report.

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
    if (const core::ITable* world = tables.FindTable("world_params")) {
      const std::uint32_t key_row = world->FindRowByKey("cemetery_m2_per_grave");
      const std::uint32_t value = world->FindColumn("value");
      if (key_row != core::kNoTableRow && value != core::kNoTableColumn) {
        if (const std::optional<float> m2 = world->CellReal(key_row, value)) {
          m2_per_grave_ = *m2;
        }
      }
    }
    std::string error;
    core::LoadDefinitions(tables, core::StubTables::kAllowed, definitions_, error);
  }

  /// The built cemeteries' fill at which a new one is marked (boss [25]).
  static constexpr float kMarkAtFill = 0.9F;

  /// @brief One day of the chairman's attention. Call once a day.
  void RunDay(core::ISimulation& simulation) {
    if (cemetery_.value == core::kInvalidDefIdValue) {
      return;
    }
    const core::WorldState& world = simulation.CompletedState();
    const std::span<const float> radii = definitions_.units.plot_radius_m;
    std::vector<core::OrderRow> orders;
    std::uint32_t graves = 0;
    std::uint32_t capacity = 0;
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
      graves += unit.graves;
      capacity += core::CemeteryCapacityGraves(core::PlotRadiusOf(unit, radii), m2_per_grave_);
    }
    if (world.burials_waiting > 0) {
      ++held_days_;
    }
    const bool nearly_full =
        capacity == 0 || static_cast<float>(graves) >= kMarkAtFill * static_cast<float>(capacity);
    if (!one_coming && cooldown_ == 0 && (nearly_full || world.burials_waiting > 0)) {
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
              << ": FIXTURE DIFFERS FROM THE START CANON — the run's chairman marks a NEW "
                 "CEMETERY when the built ones hold 90 % of their graves or the first dead "
                 "waits for a grave, on the elder's point first, and starts it the next day "
                 "(social.md «Два кладбища»: the game stops for the player; boss, 22 October "
                 "2026)\n";
  }

  /// @brief What the fixture did, for the run to print at the end.
  void Report(std::string_view run_name) const {
    std::cout << run_name << ": cemeteries — marked " << marked_ << " (first on day "
              << first_mark_day_ << "), start orders " << started_ << "; days the dead waited "
              << held_days_ << (held_days_ > 0 ? " — THE STOP FIRED IN A RUN" : "") << '\n';
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
  float m2_per_grave_ = core::kCemeteryM2PerGraveDefault;
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
