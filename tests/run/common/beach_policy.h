/// @file
/// @brief The run's chairman builds the beach in year 1 at the elder's
/// suggested point - the reference player who answers rest.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THE RUN BUILDS THIS (#1373; econ, econ-boss-1373-rest-gates [1], arm A;
/// leisure.md §6, «Строить можно и стоит с года 1»): the beach is the answer
/// to the heat's price, three benches and a day's work. The era's social
/// objects (social_objects_policy.h) leave it out - its class column has no
/// Epoch I readiness weight - so no run ever built one, and the bathing after
/// work would have measured an empty world.
///
/// THE RULE IS THE RUN'S: marked from the entry morning of year 1 at
/// suggestions.csv suggest_beach (econ's pick, the lake's north shore; econ
/// [4], boss [5]); started when its recipe is covered and the start gate (the
/// saw's boards and the warm's hold) is open. ITS PATH IS THE MAP'S, not the
/// bot's: path_village_beach in roads.csv. The beach's +3 counts only with a
/// drawn path (econ [4]), so the bot lays none of its own - a straight path
/// of its own would have crossed the start's fields.
///
/// THE REACH PRINT: on the day the beach stands, the families whose yard
/// reaches it within the walk (labor.csv leisure_reach_m, effective metres on
/// foot) with the walk's last piece off the network at most kReachedOffRoadM,
/// and within the children's 1 600 m (leisure §4) for the teens' reading.
/// The control arm (--beach=off, econ's arm B) does nothing.

#ifndef TESTS_RUN_COMMON_BEACH_POLICY_H_
#define TESTS_RUN_COMMON_BEACH_POLICY_H_

#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_catalog/definitions.h"
#include "core_common/geometry.h"
#include "core_common/order_state.h"
#include "core_common/road_route.h"
#include "core_common/unit_state.h"
#include "core_common/work_seam.h"
#include "core_common/world_state.h"
#include "core_tables/stub_tables.h"
#include "core_tables/tables.h"
#include "core_world/world.h"
#include "start_gate.h"
#include "suggested_place.h"

namespace run {

/// @brief Marks and starts the village's first beach in year 1.
class BeachPolicy {
 public:
  explicit BeachPolicy(const core::ITableSet& tables) : places_(tables, "beach") {
    if (const core::ITable* const types = tables.FindTable("unit_types")) {
      const std::uint32_t row = types->FindRowByKey("beach");
      beach_ = row == core::kNoTableRow ? core::UnitTypeId{}
                                        : core::DefIdFromIndex<core::UnitTypeIdTag>(row);
    }
    entry_day_ =
        static_cast<core::SimDay>(Number(tables, "world_params", "player_entry_day", 12.0F));
    reach_m_ = Number(tables, "labor", "leisure_reach_m", 1000.0F);
    std::string error;
    core::LoadDefinitions(tables, core::StubTables::kAllowed, definitions_, error);
  }

  /// @brief econ's arm B: no beach.
  void SetOff() { on_ = false; }

  /// @brief The question asked before the start (start_gate.h).
  void SetStartGate(StartGate gate) { start_gate_ = std::move(gate); }

  /// @brief One day of the chairman's attention. Call once a day.
  void RunDay(core::ISimulation& simulation) {
    if (!on_ || beach_.value == core::kInvalidDefIdValue) {
      return;
    }
    const core::WorldState& world = simulation.CompletedState();
    const core::SimDay day = world.calendar.day;
    std::vector<core::OrderRow> orders;
    std::uint32_t row = core::kNoRow;
    for (std::uint32_t r = 0; r < world.units.rows.size(); ++r) {
      if (world.units.rows[r].type.value == beach_.value && world.units.rows[r].dead == 0) {
        row = r;
        break;
      }
    }
    if (row == core::kNoRow) {
      if (marked_day_ < 0 && day >= entry_day_) {
        const std::vector<float>& radii = definitions_.units.keep_out_radius_m;
        const float radius = beach_.value < radii.size() ? radii[beach_.value] : 0.0F;
        const std::optional<core::Vec2> point = places_.Take(world, definitions_.Plots(), radius);
        if (point) {
          core::OrderRow mark;
          mark.kind = core::OrderKind::kBuildUnit;
          mark.unit_type = beach_;
          mark.position = *point;
          orders.push_back(mark);
          marked_day_ = static_cast<std::int64_t>(day);
        } else {
          ++no_place_days_;
        }
      }
    } else {
      const core::UnitRow& unit = world.units.rows[row];
      if (unit.level > 0 && stands_day_ < 0) {
        stands_day_ = static_cast<std::int64_t>(day);
        CountReach(world, unit.position);
      }
      if (unit.level == 0 && unit.construction.phase == core::ConstructionPhase::kMarked &&
          simulation.MaterialsShortFor(world.units.row_ids[row]).empty() &&
          GateOpen(start_gate_, world, unit.type, 1)) {
        core::OrderRow start;
        start.kind = core::OrderKind::kStartBuild;
        start.unit = world.units.row_ids[row];
        orders.push_back(start);
        if (started_day_ < 0) {
          started_day_ = static_cast<std::int64_t>(day);
        }
      }
    }
    if (!orders.empty()) {
      simulation.StageOrders(std::span<const core::OrderRow>(orders.data(), orders.size()), {});
    }
  }

  /// @brief The fixture difference, in words, BEFORE the run measures.
  static void Declare(std::string_view run_name) {
    std::cout << run_name
              << ": FIXTURE DIFFERS FROM THE START CANON — the run's chairman marks the BEACH "
                 "from the entry morning of year 1 at suggest_beach; its path is the map's "
                 "(path_village_beach), the bot lays none (#1373, econ's arm A)\n";
  }

  /// @brief What the fixture did, for the run to print at the end.
  void Report(std::string_view run_name, std::uint64_t seed) const {
    std::cout << run_name << ": BEACH seed " << seed << (on_ ? "" : " (off: arm B)") << " marked "
              << marked_day_ << " started " << started_day_ << " stands " << stands_day_
              << " path_at_beach_m " << path_at_beach_m_ << " families " << families_
              << " in_reach_" << reach_m_ << "m " << reached_ << " within_1600m "
              << reached_children_ << " no_place_days " << no_place_days_;
    if (on_ && stands_day_ >= 0 && reached_ == 0) {
      std::cout << " — NO FAMILY IN REACH: the bathing after work at the beach measures nothing "
                   "(P1/P2 on the reached adults NOT MEASURED)";
    }
    std::cout << '\n';
  }

 private:
  /// The walk's last piece off the network at the beach, metres, at or below
  /// which its path counts as drawn - world_params road_access_m's default.
  static constexpr float kReachedOffRoadM = 10.0F;
  /// The children's walk to a bathing place (leisure §4, «1 600 м»).
  static constexpr float kChildrenReachM = 1600.0F;

  static float Number(const core::ITableSet& tables,
                      const char* table_name,
                      const char* key,
                      float fallback) {
    const core::ITable* const table = tables.FindTable(table_name);
    if (table == nullptr) {
      return fallback;
    }
    const std::uint32_t row = table->FindRowByKey(key);
    const std::uint32_t column = table->FindColumn("value");
    if (row == core::kNoTableRow || column == core::kNoTableColumn) {
      return fallback;
    }
    return static_cast<float>(table->CellReal(row, column).value_or(fallback));
  }

  void CountReach(const core::WorldState& world, core::Vec2 beach) {
    for (std::uint32_t row = 0; row < world.families.rows.size(); ++row) {
      core::Vec2 home;
      if (!core::HomePositionOf(world, world.families.row_ids[row], home)) {
        continue;
      }
      ++families_;
      const core::RouteMeasure way = core::RoadMeasure(world, core::TravelMode::kWalk, home, beach);
      path_at_beach_m_ = way.off_road_end_m;
      const float metres = way.effective_km * 1000.0F;
      const bool path = way.off_road_end_m <= kReachedOffRoadM;
      reached_ += path && metres <= reach_m_ ? 1U : 0U;
      reached_children_ += path && metres <= kChildrenReachM ? 1U : 0U;
    }
  }

  bool on_ = true;
  core::UnitTypeId beach_;
  SuggestedPlaces places_;
  core::Definitions definitions_;
  StartGate start_gate_;
  core::SimDay entry_day_ = 12;
  float reach_m_ = 1000.0F;
  std::int64_t marked_day_ = -1;
  std::int64_t started_day_ = -1;
  std::int64_t stands_day_ = -1;
  float path_at_beach_m_ = -1.0F;
  std::uint32_t families_ = 0;
  std::uint32_t reached_ = 0;
  std::uint32_t reached_children_ = 0;
  std::uint32_t no_place_days_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_BEACH_POLICY_H_
