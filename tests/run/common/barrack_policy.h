/// @file
/// @brief The barracks the run's chairman puts up for the newcomers: one by
/// year 2, then one every three years.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THE RUN BUILDS THIS (boss, 11 October 2026, after econ's reading of the
/// housing pair, housing-pair-reading-2026-10-11; demography design, the
/// migrants' table: «Боту канона: барак к году 2, дальше раз в три года (ход
/// core)»). A migrant comes only to a free house or a barrack place
/// (housing.h, HomeForNewcomers). The canon built houses for its own couples
/// and no barrack at all: it kept the start's six places for eleven years and
/// took no arrival from year 8 on, so the village grew by births alone.
///
/// THE RULE IS THE DESIGN'S CANON, NOT A FORECAST: the barracks wanted in
/// year Y (1-based) are 1 + (Y - 1) / 3 — one marked in year 1 to stand by
/// year 2, the second in year 4 for year 5, and so on. Marked at the elder's
/// barrack point first (suggestions.csv), started once the chairman's yard
/// stands (the horses stabled), no site of the farm's own is open and the
/// first pen with cows is warm, its
/// recipe is covered and the farm has no
/// shortage of its own waiting. The houses do not hold it
/// back: a barrack is housing too, for those a house does not take.

#ifndef TESTS_RUN_COMMON_BARRACK_POLICY_H_
#define TESTS_RUN_COMMON_BARRACK_POLICY_H_

#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core_catalog/definitions.h"
#include "core_common/calendar.h"
#include "core_common/order_state.h"
#include "core_common/plot.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/stub_tables.h"
#include "core_tables/tables.h"
#include "core_world/world.h"
#include "house_policy.h"
#include "start_gate.h"
#include "suggested_place.h"

namespace run {

/// @brief Marks a barrack whenever fewer stand or go up than the canon's year
///        wants, and starts it when its recipe is covered.
class BarrackPolicy {
 public:
  explicit BarrackPolicy(const core::ITableSet& tables) : suggested_(tables, "barrack") {
    const core::ITable* types = tables.FindTable("unit_types");
    const std::uint32_t row = types == nullptr ? core::kNoTableRow : types->FindRowByKey("barrack");
    barrack_ = row == core::kNoTableRow ? core::UnitTypeId{}
                                        : core::UnitTypeId{static_cast<std::uint16_t>(row)};
    std::string error;
    core::LoadDefinitions(tables, core::StubTables::kAllowed, definitions_, error);
  }

  /// @brief The question asked before every start (start_gate.h).
  void SetStartGate(StartGate gate) { start_gate_ = std::move(gate); }

  /// @brief Whether the farm's own building comes first today — a site of the
  ///        farm's kinds open (FixturePolicy::FarmSiteOpen) or the first pen
  ///        with cows waiting for its warm (PenWaitingForWarm): no barrack
  ///        starts while it does.
  void SetPenWait(std::function<bool(const core::WorldState&)> pen_waits) {
    pen_waits_ = std::move(pen_waits);
  }

  /// @brief The barracks the canon wants standing or going up in `year`
  ///        (1-based): one by year 2, then one every three years.
  static std::uint32_t WantedInYear(std::uint32_t year) {
    return year == 0 ? 0 : 1 + (year - 1) / 3;
  }

  /// @brief One day of the chairman's attention. Call once a day, after the
  ///        houses.
  /// @param farm_first The farm's own shortage has a site waiting for its
  ///        recipe (FixturePolicy::HoldsHousesBack): nothing starts today.
  void RunDay(core::ISimulation& simulation, bool farm_first) {
    if (barrack_.value == core::kInvalidDefIdValue) {
      return;
    }
    const core::WorldState& world = simulation.CompletedState();
    std::vector<core::OrderRow> orders;
    std::uint32_t barracks = 0;
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      if (unit.type.value != barrack_.value || unit.dead != 0) {
        continue;
      }
      ++barracks;
      const bool marked =
          unit.level == 0 && unit.construction.phase == core::ConstructionPhase::kMarked;
      // NOT STARTED BEFORE THE FARM STANDS (the school's rule, school_policy.h:
      // its seed 1929 scar). Started in year 1, the first barrack took the logs
      // and the builders the chairman's yard waited on: no horse yard within
      // the year, no goods cart, and watchdog_threads found no wait to measure
      // (3334e5a's first suite). Marked on time, started after the horses.
      // AND NOT WHILE THE FARM'S OWN SITE STANDS OPEN OR THE FIRST PEN WAITS FOR
      // ITS WARM. 73e0fcc's barrack, started the day the horses were stabled
      // (seed 1931, day 14), took the logs and the crew of the granary marked
      // on day 11: the granary started on day 31 instead of 16 and stood on
      // day 47 instead of 23, and the farm marks nothing new while a site of
      // its own kinds is open (FixturePolicy::NextOrder) — the first pen was
      // marked on day 48 instead of 24, warmed on day 56, and 13 cows froze in
      // three villages (core-penline probe, m22; the warm line red, 0 dead
      // without barracks). 9056154's first guess, the pen's warm alone, moved
      // nothing: the pen did not exist yet.
      const bool pen_waits = pen_waits_ && pen_waits_(world);
      if (marked && !farm_first && !pen_waits && world.chairman.horses_stabled != 0 &&
          simulation.MaterialsShortFor(world.units.row_ids[row]).empty() &&
          GateOpen(start_gate_, world, unit.type, unit.construction.target_level)) {
        core::OrderRow start;
        start.kind = core::OrderKind::kStartBuild;
        start.unit = world.units.row_ids[row];
        orders.push_back(start);
        ++started_;
      }
    }
    const std::uint32_t year = world.calendar.day / core::kDaysPerYear + 1;
    if (barracks < WantedInYear(year) && cooldown_ == 0) {
      core::OrderRow mark;
      mark.kind = core::OrderKind::kBuildUnit;
      mark.unit_type = barrack_;
      const std::vector<float>& radii = definitions_.units.keep_out_radius_m;
      const float radius = barrack_.value < radii.size() ? radii[barrack_.value] : 0.0F;
      // ON THE ELDER'S POINT FIRST (suggested_place.h).
      const std::optional<core::Vec2> point = suggested_.Take(world, definitions_.Plots(), radius);
      mark.position =
          point ? *point
                : core::FreePlot(
                      world.units, definitions_.Plots(), HousePolicy::VillageCentre(world), radius);
      orders.push_back(mark);
      ++marked_;
      // An order lands at the day's close and shows the day after: two days
      // before the count is asked again, as the yard's policy waits.
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
    std::cout
        << run_name
        << ": FIXTURE DIFFERS FROM THE START CANON — the run's chairman marks BARRACKS by "
           "the design's canon, one by year 2 and one more every three years (1 + (Y - 1) / 3 "
           "standing or going up in year Y), on the barrack points first, and starts each "
           "once the chairman's yard stands, no site of the farm's own is open and the "
           "first pen with cows is warm, its "
           "recipe is covered and no farm shortage "
           "waits (demography design, the "
           "migrants' table; boss, 11 October 2026)\n";
  }

  /// @brief What the fixture did, for the run to print at the end.
  void Report(const core::WorldState& world, std::string_view run_name) const {
    std::uint32_t standing = 0;
    std::uint32_t sites = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.type.value == barrack_.value && unit.dead == 0) {
        standing += unit.level > 0 ? 1U : 0U;
        sites += unit.level == 0 ? 1U : 0U;
      }
    }
    std::cout << run_name << ": barracks — marked " << marked_ << ", start orders " << started_
              << "; " << standing << " standing, " << sites << " sites at the end\n";
    suggested_.Report(run_name);
  }

 private:
  static constexpr std::uint32_t kCooldownDays = 2;

  core::UnitTypeId barrack_;

  core::Definitions definitions_;

  /// The barrack points (suggestions.csv).
  SuggestedPlaces suggested_;

  std::uint32_t marked_ = 0;

  std::uint32_t started_ = 0;

  std::uint32_t cooldown_ = 0;

  StartGate start_gate_;

  std::function<bool(const core::WorldState&)> pen_waits_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_BARRACK_POLICY_H_
