/// @file
/// @brief The run's chairman fells the nearest stand when the village has
/// fewer logs than one granary takes.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THE RUN FELLS. On the approved layout the store sites stood on logs
/// and nothing else: the 690 start logs were gone by year 7..14, and 47 of the
/// 55 store-class failed plan years of nine seeds had a granary site waiting
/// for logs (plan_shortfall, THE STORE SITES, 2026-09-13). The core had no
/// channel of logs at all; now it has felling (timber design §8a), and a run
/// has no chairman to mark a stand. So the run plays him, exactly as it plays
/// the building chairman (fixture_policy.h) and the yard (yard_policy.h).
///
/// ONE SIGNAL, ONE ANSWER (boss, parcel 181): fewer logs than one granary →
/// mark the nearest stand that still has timber for one granary's logs. It
/// is the answer a chairman gives to a site short of logs, and nothing
/// smarter: no planning a year ahead, no choosing a stand by its haul.
/// THE FIXTURE DIFFERS FROM THE START CANON, AND IT SAYS SO (Declare).

#ifndef TESTS_RUN_COMMON_FELLING_POLICY_H_
#define TESTS_RUN_COMMON_FELLING_POLICY_H_

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_catalog/timber_catalog.h"
#include "core_common/calendar.h"
#include "core_common/order_state.h"
#include "core_common/road_route.h"
#include "core_common/state_table_ops.h"
#include "core_common/timber_state.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"
#include "rise_watch.h"

namespace run {

class FellingPolicy {
 public:
  explicit FellingPolicy(const core::ITableSet& tables) {
    std::string error;
    ready_ = core::ParseTimberCatalog(tables, catalog_, error) &&
             catalog_.log_resource.value != core::kInvalidDefIdValue && catalog_.log_grams > 0 &&
             !catalog_.stands.empty();
    granary_logs_ = GranaryLogs(tables);
    logs_by_type_ = FirstLevelLogs(tables);
    ride_hours_per_km_ = static_cast<float>(core::kClockScale) /
                         Cell(tables, "transport", "horse_trot", "speed_kmh", 12.0F);
    ride_limit_hours_ = Cell(tables, "labor", "travel_limit_hours", "value", 6.0F);
  }

  /// @brief The fixture difference, in words, for the run to print BEFORE it
  /// measures anything.
  static void Declare(const char* run) {
    std::cout << run
              << ": FIXTURE DIFFERS FROM THE START CANON — the run's chairman FELLS the nearest "
                 "stand when the village has fewer logs than one granary takes (timber design "
                 "§8a; boss, 2026-09-13)\n";
  }

  /// @brief Counts the step the chairman's yard waits to take (rise_watch.h).
  void SetRiseWatch(RiseWatch watch) { rise_watch_ = std::move(watch); }

  /// @brief One day of the chairman's attention. Call once a day.
  /// @param saw_log_grams The logs the saw needs for the boards the sites still
  ///        lack (SawmillPolicy::LogsForMissingBoards); 0 in a run with no saw.
  void RunDay(core::ISimulation& simulation, core::Grams saw_log_grams = 0) {
    if (!ready_) {
      return;
    }
    if (cooldown_ > 0) {
      --cooldown_;
      return;
    }
    const core::WorldState& world = simulation.CompletedState();
    for (const core::TimberStandRow& stand : world.stands.rows) {
      if (stand.marked_m3 > 0.0F) {
        return;  // the run's chairman fells one stand at a time
      }
    }
    // LOGS IN HAND, and the logs already felled and lying count as in hand: a
    // chairman who has a heap in the grove does not fell another for want of
    // carts.
    //
    // A SITE'S LOGS ARE NOT IN HAND. They were delivered to their own building
    // and nothing else can take them (construction_system.cpp). Counting them
    // stopped the village for twenty years on 2026-09-13: a granary site held
    // its sixty logs and waited for boards, the stores had none, this policy
    // saw sixty in hand and never felled, and the sawmill had nothing to saw.
    core::Grams logs = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.level == 0) {
        continue;
      }
      logs += catalog_.log_resource.value < unit.stock.size()
                  ? unit.stock[catalog_.log_resource.value]
                  : 0;
    }
    for (const core::TimberStandRow& stand : world.stands.rows) {
      logs += stand.load_grams;
    }
    // AND THE LOGS THE SITES STILL WAIT FOR (2026-09-14). One granary's worth
    // was the whole signal while granaries were the only sites; since the
    // houses came off the stub, three house sites at 35 logs each waited on a
    // village holding 35, and felling never began because 35 was "enough" by
    // a granary's measure — nine houses in twelve years on seed 1929.
    core::Grams owed = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      // Marked and waiting for the recipe too: a start needs the whole recipe
      // in the village now (construction design §6), so a site waits marked.
      const bool waits = unit.construction.phase == core::ConstructionPhase::kDelivering ||
                         unit.construction.phase == core::ConstructionPhase::kMarked;
      if (unit.level != 0 || !waits || unit.type.value >= logs_by_type_.size()) {
        continue;
      }
      const core::Grams needed =
          static_cast<core::Grams>(logs_by_type_[unit.type.value]) * catalog_.log_grams;
      const core::Grams there = catalog_.log_resource.value < unit.stock.size()
                                    ? unit.stock[catalog_.log_resource.value]
                                    : 0;
      owed += needed > there ? needed - there : 0;
    }
    // And the logs of the step the chairman's yard waits to take (rise_watch.h):
    // the recipe of a level above the first is not in logs_by_type_, so it is
    // read off the core's own shortfall of that step.
    const std::uint32_t rising = rise_watch_ ? rise_watch_(world) : core::kNoRow;
    if (rising < world.units.rows.size()) {
      for (const core::MaterialShortfall& line :
           simulation.MaterialsShortFor(world.units.row_ids[rising])) {
        if (line.resource.value == catalog_.log_resource.value) {
          owed += line.needed;
        }
      }
    }
    const core::Grams wanted =
        (static_cast<core::Grams>(granary_logs_) * catalog_.log_grams) + owed + saw_log_grams;
    if (logs >= wanted) {
      return;
    }
    // ONE FELLING, SIZED TO THE WHOLE SHORTFALL — the sites' logs and the
    // saw's together (boss, parcel 281). "Одна активная задача на тип работ"
    // counts markings, not volume: a good chairman marks a grove for the
    // year's need at once and keeps the crew the tools and the people allow,
    // rather than felling sixty-five times by a granary's worth.
    const auto short_logs =
        static_cast<std::uint32_t>((wanted - logs + catalog_.log_grams - 1) / catalog_.log_grams);
    core::OrderRow order;
    if (!NearestMark(world, short_logs > granary_logs_ ? short_logs : granary_logs_, order)) {
      return;
    }
    simulation.StageOrders(std::span<const core::OrderRow>(&order, 1), {});
    ++marked_;
    cooldown_ = kCooldownDays;
  }

  /// @brief What the policy did, for the run to print at the end.
  void Report(const char* run, const core::WorldState& world) const {
    float felled_left = 0.0F;
    float started = 0.0F;
    for (const core::TimberStandRow& stand : world.stands.rows) {
      if (stand.kind == core::TimberStandKind::kForestOld ||
          stand.table_row >= catalog_.stands.size()) {
        continue;
      }
      felled_left += stand.stock_m3;
      started += core::StartStockM3(catalog_, catalog_.stands[stand.table_row]);
    }
    std::cout << run << ": the run's chairman marked " << marked_
              << " fellings; groves and belts hold " << std::lround(felled_left) << " of "
              << std::lround(started) << " m3\n";
  }

 private:
  static constexpr std::uint32_t kCooldownDays = 2;

  /// The logs a first-level granary takes (unit_level_cost.csv), read so the
  /// signal moves with the table.
  static std::uint32_t GranaryLogs(const core::ITableSet& tables) {
    constexpr std::uint32_t kFallback = 60;
    const core::ITable* const costs = tables.FindTable("unit_level_cost");
    if (costs == nullptr) {
      return kFallback;
    }
    const std::uint32_t unit_col = costs->FindColumn("unit");
    const std::uint32_t level_col = costs->FindColumn("level");
    const std::uint32_t resource_col = costs->FindColumn("resource");
    const std::uint32_t amount_col = costs->FindColumn("amount");
    for (std::uint32_t row = 0; row < costs->RowCount(); ++row) {
      if (costs->CellText(row, unit_col) == "granary" && costs->CellText(row, level_col) == "1" &&
          costs->CellText(row, resource_col) == "log") {
        const std::optional<float> amount = costs->CellReal(row, amount_col);
        return amount.has_value() && *amount > 0.0F ? static_cast<std::uint32_t>(*amount)
                                                    : kFallback;
      }
    }
    return kFallback;
  }

  /// The logs the first level of every unit type takes, by unit_types row
  /// (unit_level_cost.csv); zero for a type that takes none.
  static std::vector<std::uint32_t> FirstLevelLogs(const core::ITableSet& tables) {
    std::vector<std::uint32_t> logs;
    const core::ITable* const costs = tables.FindTable("unit_level_cost");
    const core::ITable* const types = tables.FindTable("unit_types");
    if (costs == nullptr || types == nullptr) {
      return logs;
    }
    logs.assign(types->RowCount(), 0U);
    const std::uint32_t unit_col = costs->FindColumn("unit");
    const std::uint32_t level_col = costs->FindColumn("level");
    const std::uint32_t resource_col = costs->FindColumn("resource");
    const std::uint32_t amount_col = costs->FindColumn("amount");
    for (std::uint32_t row = 0; row < costs->RowCount(); ++row) {
      if (costs->CellText(row, level_col) != "1" || costs->CellText(row, resource_col) != "log") {
        continue;
      }
      const std::uint32_t type_row = types->FindRowByKey(costs->CellText(row, unit_col));
      const std::optional<float> amount = costs->CellReal(row, amount_col);
      if (type_row < logs.size() && amount.has_value() && *amount > 0.0F) {
        logs[type_row] = static_cast<std::uint32_t>(*amount);
      }
    }
    return logs;
  }

  /// The village's centre, as the building chairman reckons it.
  static core::Vec2 Centre(const core::WorldState& world) {
    core::Vec2 sum{.x = 0.0F, .y = 0.0F};
    for (const core::UnitRow& unit : world.units.rows) {
      sum.x += unit.position.x;
      sum.y += unit.position.y;
    }
    const auto count = static_cast<float>(world.units.rows.size());
    return count > 0.0F ? core::Vec2{.x = sum.x / count, .y = sum.y / count} : sum;
  }

  /// @param logs How many logs the mark is for: one granary's, or what the
  ///        village and its sites are short of when that is more.
  /// Hours of the ride, one way, from the nearest lived-in house, at the
  /// labour model's harness speed (felling rides; labor_state.h, RidesOut).
  /// BY THE ROAD, as a team rides it (0.36.12): the core's own question for
  /// kFellingUnreachable (timber_felling.cpp, NearestHomeTravelHours) — the
  /// limit is by the network since 0.36.2, and until 0.36.12 this measured
  /// the straight line, so the canon marked stands the core then refused.
  /// THE HOUSES ARE FOUND ON THE NETWORK ONCE A STEP, the stand once a call:
  /// finding is the costly half of a query (road_route.h, NetworkPlace), and
  /// asking it anew for every house and every stand doubled timber_years'
  /// time (5.6 s -> 11.7 s). The answer is the same by construction.
  float RideHours(const core::WorldState& world, core::Vec2 place) const {
    const std::shared_ptr<const core::RoadIndex> index = core::RoadIndexOf(world);
    if (homes_tick_ != world.calendar.tick || homes_index_ != index.get() ||
        homes_units_ != world.units.rows.size()) {
      homes_.clear();
      for (const core::UnitRow& unit : world.units.rows) {
        if (unit.level == 0 || unit.household.value == core::kInvalidEntityIdValue) {
          continue;
        }
        homes_.push_back(index->Locate(core::TravelMode::kTeam, unit.position));
      }
      homes_tick_ = world.calendar.tick;
      homes_index_ = index.get();
      homes_units_ = world.units.rows.size();
      homes_owner_ = index;
    }
    const core::NetworkPlace there = index->Locate(core::TravelMode::kTeam, place);
    float best = 1.0e9F;
    for (const core::NetworkPlace& home : homes_) {
      best = std::min(best, index->EffectiveKm(home, there) * ride_hours_per_km_);
    }
    return best;
  }

  mutable std::vector<core::NetworkPlace> homes_;
  mutable std::uint64_t homes_tick_ = ~std::uint64_t{0};
  mutable const core::RoadIndex* homes_index_ = nullptr;
  mutable std::size_t homes_units_ = 0;
  /// Keeps the index the cached places were found on alive (a hand-built
  /// world's index is built on the spot).
  mutable std::shared_ptr<const core::RoadIndex> homes_owner_;

  /// A cell of a key/value table, or `fallback`.
  static float Cell(const core::ITableSet& tables,
                    std::string_view table_name,
                    std::string_view key,
                    std::string_view column,
                    float fallback) {
    const core::ITable* const table = tables.FindTable(table_name);
    if (table == nullptr) {
      return fallback;
    }
    const std::optional<float> cell =
        table->CellReal(table->FindRowByKey(key), table->FindColumn(column));
    return cell.has_value() && *cell > 0.0F ? *cell : fallback;
  }

  bool NearestMark(const core::WorldState& world, std::uint32_t logs, core::OrderRow& order) const {
    const core::Vec2 centre = Centre(world);
    std::uint32_t best = core::kNoRow;
    float best_distance = 0.0F;
    float best_volume = 0.0F;
    for (std::uint32_t row = 0; row < world.stands.rows.size(); ++row) {
      const core::TimberStandRow& stand = world.stands.rows[row];
      // A GROWN PLANTING IS FELLED TOO, at its species' log share
      // (timber_planting.h, PlantedStandDef). It has no row of the stands
      // table, and until the planting policy this loop skipped every stand
      // without one — a zone grown in the run would have stood uncut for ever.
      float share = 0.0F;
      if (stand.kind == core::TimberStandKind::kPlanted) {
        share = stand.species.value < catalog_.species.size()
                    ? catalog_.species[stand.species.value].log_share
                    : 0.0F;
      } else if (stand.table_row < catalog_.stands.size()) {
        const core::TimberStandDef& def = catalog_.stands[stand.table_row];
        share = stand.kind == core::TimberStandKind::kForestOld
                    ? def.log_share * catalog_.old_log_share_factor
                    : def.log_share;
      }
      if (!(share > 0.0F) || !(stand.stock_m3 > 0.0F)) {
        continue;
      }
      // Enough of the stand for the logs asked, or what it has left.
      const float wanted_m3 = static_cast<float>(logs) * catalog_.log_m3 / share;
      const float volume = wanted_m3 < stand.stock_m3 ? wanted_m3 : stand.stock_m3;
      // A MARK THAT YIELDS NOT ONE LOG IS NOT A MARK: an old-forest square
      // holds a few cubic metres of trunks, and marking its crumbs would fell
      // nothing and be marked again the next day, for ever.
      if (volume * share < catalog_.log_m3) {
        continue;
      }
      // NOT BEYOND THE BRIGADE'S RIDE (boss, parcel 308): a stand past the
      // road limit from every lived-in house is marked for nobody — on seed
      // 1929 one such mark stood from year 14 to the end with no feller.
      if (RideHours(world, stand.position) > ride_limit_hours_) {
        continue;
      }
      const float dx = stand.position.x - centre.x;
      const float dy = stand.position.y - centre.y;
      const float distance = std::sqrt((dx * dx) + (dy * dy));
      if (best == core::kNoRow || distance < best_distance) {
        best = row;
        best_distance = distance;
        best_volume = volume;
      }
    }
    if (best == core::kNoRow) {
      return false;
    }
    order.kind = core::OrderKind::kMarkFelling;
    order.stand = world.stands.row_ids[best];
    order.volume_m3 = best_volume;
    return true;
  }

  core::TimberCatalog catalog_;

  float ride_hours_per_km_ = 1.0F;

  /// labor.csv travel_limit_hours: six hours by the network since 0.36.9
  /// (decision 276), measured by RideHours along the roads since 0.36.12.
  float ride_limit_hours_ = 6.0F;

  bool ready_ = false;

  std::uint32_t granary_logs_ = 60;

  /// Logs a first level takes, by unit_types row: what a site still waits for.
  std::vector<std::uint32_t> logs_by_type_;

  RiseWatch rise_watch_;

  std::uint32_t cooldown_ = 0;

  std::uint32_t marked_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_FELLING_POLICY_H_
