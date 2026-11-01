/// @file
/// @brief What the farm's sites waited on, years 1-6: every farm site's mark,
/// start, works and standing day; the first granary's and the first long
/// store's wait, day run by day run, with what held them; and the church's
/// grain carted out after the granary stands. #1285 G1's probe.
/// @threading SINGLE_THREADED
/// Test-side code, read from the thread that owns the simulation.
///
/// WHY IT EXISTS (#1285; boss, boss-core-canon-granary [3]): G1's pair put the
/// granary up 61 days earlier, but one village (1940) waited 35 days from the
/// mark to the start, two (1937, 1940) held grain in the church past day 80,
/// and in 9 of 32 the long store did not stand by the end of year 3 - and the
/// G1285 line named none of the causes. Read-only: it changes no world.
///
/// A WAIT RUN is a span of days with one state of the watched site: not
/// marked yet (and the farm site going up ahead of it in the one-site queue,
/// fixture_policy.h NextOrder), marked (the start gate - the saw's boards
/// and the warm's hold together - the saw's half alone, the recipe short at
/// the stores, the farm site ahead), delivering (carriers on its supply task at
/// noon), building (a crew at noon). A run's extra numbers (tonnes awaiting,
/// hands) are its FIRST day's; the state alone splits the runs.

#ifndef TESTS_RUN_COMMON_SITE_WAIT_TALLY_H_
#define TESTS_RUN_COMMON_SITE_WAIT_TALLY_H_

#include <array>
#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "core_common/hauled_book.h"
#include "core_common/labor_state.h"
#include "core_common/logistics_state.h"
#include "core_common/quantities.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"
#include "fixture_policy.h"
#include "sawmill_policy.h"

namespace run {

class SiteWaitTally {
 public:
  /// The last year the sites are followed (boss [3]: the long store to y6).
  static constexpr std::uint32_t kLastYear = 6;
  /// The church's carting is followed in years 1-3, as G1285's gates.
  static constexpr std::uint32_t kChurchLastYear = 3;
  /// A village's church lines are capped: past it, a count.
  static constexpr std::size_t kMaxChurchLines = 60;

  explicit SiteWaitTally(const core::ITableSet& tables) {
    if (const core::ITable* const types = tables.FindTable("unit_types")) {
      const std::uint32_t key = types->FindColumn("key");
      for (std::uint32_t row = 0; row < types->RowCount(); ++row) {
        type_keys_.emplace_back(key == core::kNoTableColumn
                                    ? std::to_string(row)
                                    : std::string(types->CellText(row, key)));
      }
      granary_ = RowOf(*types, "granary");
      long_store_ = RowOf(*types, "vegetable_store");
      church_ = RowOf(*types, "church_store");
    }
    const core::ITable* const resources = tables.FindTable("resources");
    const core::ITable* const stores = tables.FindTable("resource_stores");
    if (resources != nullptr) {
      const std::uint32_t key = resources->FindColumn("key");
      for (std::uint32_t row = 0; row < resources->RowCount(); ++row) {
        resource_keys_.emplace_back(key == core::kNoTableColumn
                                        ? std::to_string(row)
                                        : std::string(resources->CellText(row, key)));
      }
      grain_.assign(resources->RowCount(), 0);
    }
    if (resources != nullptr && stores != nullptr) {
      const std::uint32_t res_col = stores->FindColumn("resource");
      const std::uint32_t storage_col = stores->FindColumn("storage");
      for (std::uint32_t row = 0; row < stores->RowCount(); ++row) {
        const std::uint32_t r = resources->FindRowByKey(stores->CellText(row, res_col));
        if (r < grain_.size() && stores->CellText(row, storage_col) == "granary") {
          grain_[r] = 1;
        }
      }
    }
  }

  /// @brief Reads the crews on the sites and the carriers on their tasks.
  ///        Call once a day, at noon, while the day's assignments stand.
  void CountNoon(const core::WorldState& world) {
    crew_.clear();
    carriers_.clear();
    carts_by_kind_.fill(0);
    xfer_hands_ = 0;
    for (const core::ResidentRow& resident : world.residents.rows) {
      const core::WorkAssignment& work = resident.work;
      if (work.kind == core::WorkKind::kConstruction && work.unit.value != 0) {
        ++crew_[work.unit.value];
      }
      const std::uint32_t task = core::TaskRowOfWork(world, work);
      if (task == core::kNoRow) {
        continue;
      }
      const core::LogisticsTaskRow& row = world.logistics_tasks.rows[task];
      const auto kind = static_cast<std::size_t>(row.load_kind);
      if (resident.work.rides_horse != 0) {
        ++carts_by_kind_[kind < carts_by_kind_.size() ? kind : carts_by_kind_.size() - 1];
      }
      if (row.load_kind == core::LogisticsLoadKind::kSiteSupply) {
        ++carriers_[row.unit.value];
      }
      if (row.load_kind == core::LogisticsLoadKind::kStoreTransfer) {
        ++xfer_hands_;
      }
    }
  }

  /// @brief One day's reading of the sites and the church. Call once a day
  ///        after the chairman's day, with the campaign year (1-based).
  void CountDay(const core::WorldState& world,
                const core::ISimulation& simulation,
                const FixturePolicy& fixture,
                const SawmillPolicy& sawmill,
                std::uint32_t year) {
    if (year > kLastYear) {
      return;
    }
    const auto day = static_cast<std::int64_t>(world.calendar.day);
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      if (!fixture.IsFarmSiteType(unit.type) && unit.type.value != long_store_) {
        continue;
      }
      const std::uint32_t id = world.units.row_ids[row].value;
      auto [it, fresh] = sites_.try_emplace(id);
      Site& site = it->second;
      if (fresh) {
        site.type = unit.type.value;
        site.standing_at_start = unit.level > 0 && day == 0;
        order_.push_back(id);
      }
      Mark(site.marked,
           unit.construction.phase >= core::ConstructionPhase::kMarked || unit.level > 0,
           day);
      Mark(site.started,
           unit.construction.phase >= core::ConstructionPhase::kDelivering || unit.level > 0,
           day);
      Mark(site.building,
           unit.construction.phase == core::ConstructionPhase::kBuilding || unit.level > 0,
           day);
      Mark(site.stands, unit.level > 0, day);
    }
    if (year <= kChurchLastYear) {
      Watch(granary_watch_, granary_, world, simulation, fixture, sawmill, day);
    }
    Watch(store_watch_, long_store_, world, simulation, fixture, sawmill, day);
    if (year <= kChurchLastYear) {
      CountChurch(world, day);
    }
  }

  /// @brief Prints the village's lines: G1285E (every farm site), G1285W (the
  ///        granary's and the long store's wait runs), G1285C (the church).
  void Print(std::ostream& out, std::uint64_t seed) const {
    out << "population_curve: G1285E seed " << seed << " farm sites y1-y" << kLastYear
        << " key@mark/start/works/stands (-1 = not by y" << kLastYear << "):";
    for (const std::uint32_t id : order_) {
      const Site& site = sites_.at(id);
      if (site.standing_at_start) {
        continue;
      }
      out << ' ' << KeyOf(site.type) << '@' << site.marked << '/' << site.started << '/'
          << site.building << '/' << site.stands;
    }
    out << '\n';
    PrintRuns(out, seed, "granary", granary_watch_);
    PrintRuns(out, seed, "vegetable_store", store_watch_);
    for (const std::string& line : church_lines_) {
      out << "population_curve: G1285C seed " << seed << ' ' << line << '\n';
    }
    if (church_lines_dropped_ > 0) {
      out << "population_curve: G1285C seed " << seed << " ... " << church_lines_dropped_
          << " more days not printed\n";
    }
  }

 private:
  struct Site {
    std::uint16_t type = 0;
    bool standing_at_start = false;
    std::int64_t marked = -1, started = -1, building = -1, stands = -1;
  };

  struct WaitRun {
    std::int64_t first = 0, last = 0;
    std::string state;  // what splits the runs
    std::string extra;  // the first day's numbers
  };

  struct Watched {
    std::uint32_t unit = 0;  // 0 until the first row of the type appears
    bool done = false;
    std::vector<WaitRun> runs;
  };

  static std::uint16_t RowOf(const core::ITable& types, const char* key) {
    const std::uint32_t row = types.FindRowByKey(key);
    return row == core::kNoTableRow ? std::uint16_t{0xFFFFU} : static_cast<std::uint16_t>(row);
  }

  static void Mark(std::int64_t& slot, bool now, std::int64_t day) {
    if (now && slot < 0) {
      slot = day;
    }
  }

  std::string KeyOf(std::uint32_t type) const {
    return type < type_keys_.size() ? type_keys_[type] : std::to_string(type);
  }

  static const char* PhaseName(core::ConstructionPhase phase) {
    switch (phase) {
      case core::ConstructionPhase::kMarked:
        return "marked";
      case core::ConstructionPhase::kDelivering:
        return "delivering";
      case core::ConstructionPhase::kBuilding:
        return "building";
      default:
        return "other";
    }
  }

  /// The farm site the one-site queue would start or wait on before `skip`:
  /// the first farm-site row at level 0 in row order (NextOrder's loop).
  std::string SiteAhead(const core::WorldState& world,
                        const FixturePolicy& fixture,
                        std::uint32_t skip) const {
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      if (world.units.row_ids[row].value == skip || unit.level != 0 || unit.dead != 0 ||
          !fixture.IsFarmSiteType(unit.type)) {
        continue;
      }
      return KeyOf(unit.type.value) + ":" + PhaseName(unit.construction.phase);
    }
    return "-";
  }

  void Watch(Watched& watched,
             std::uint16_t type,
             const core::WorldState& world,
             const core::ISimulation& simulation,
             const FixturePolicy& fixture,
             const SawmillPolicy& sawmill,
             std::int64_t day) {
    if (watched.done || type == 0xFFFFU) {
      return;
    }
    std::uint32_t row = core::kNoRow;
    for (std::uint32_t r = 0; r < world.units.rows.size(); ++r) {
      const bool same = watched.unit != 0 ? world.units.row_ids[r].value == watched.unit
                                          : world.units.rows[r].type.value == type;
      if (same) {
        row = r;
        break;
      }
    }
    std::string state;
    std::ostringstream extra;
    if (row == core::kNoRow) {
      if (watched.unit != 0) {
        state = "gone";
      } else {
        state = "unmarked ahead " + SiteAhead(world, fixture, 0);
      }
    } else {
      watched.unit = world.units.row_ids[row].value;
      const core::UnitRow& unit = world.units.rows[row];
      if (unit.level > 0) {
        watched.done = true;
        Push(watched, day, "STANDS", "");
        return;
      }
      const core::ConstructionPhase phase = unit.construction.phase;
      state = PhaseName(phase);
      if (phase == core::ConstructionPhase::kMarked) {
        const std::uint8_t level =
            unit.construction.target_level > 0 ? unit.construction.target_level : 1;
        const bool gate = fixture.StartGateOpen(world, unit.type, level);
        const bool saw = sawmill.SparesBoardsFor(world, unit.type, level);
        std::string short_of;
        for (const core::MaterialShortfall& lack :
             simulation.MaterialsShortFor(world.units.row_ids[row])) {
          const std::string key = lack.resource.value < resource_keys_.size()
                                      ? resource_keys_[lack.resource.value]
                                      : std::to_string(lack.resource.value);
          short_of += (short_of.empty() ? "" : ",") + key;
          extra << ' ' << key << " need_kg " << lack.needed / 1000 << " held_kg "
                << lack.held / 1000;
        }
        state += std::string(" gate ") + (gate ? "1" : "0") + " saw " + (saw ? "1" : "0") +
                 " short [" + short_of + "] ahead " + SiteAhead(world, fixture, watched.unit);
      } else if (phase == core::ConstructionPhase::kDelivering) {
        core::Grams awaiting = 0;
        for (const core::Grams grams : unit.construction.awaiting) {
          awaiting += grams;
        }
        const auto hands = carriers_.find(watched.unit);
        const std::uint32_t carried = hands == carriers_.end() ? 0 : hands->second;
        state += std::string(" carriers ") + (carried > 0 ? ">0" : "0");
        extra << " awaiting_t " << awaiting / core::kGramsPerTonne << " carriers " << carried;
      } else if (phase == core::ConstructionPhase::kBuilding) {
        const auto hands = crew_.find(watched.unit);
        const std::uint32_t crew = hands == crew_.end() ? 0 : hands->second;
        state +=
            std::string(" crew ") + (crew > 0 ? ">0" : "0") + (unit.paused != 0 ? " paused" : "");
        extra << " crew " << crew << " left_md " << unit.construction.labor_days_remaining;
      }
    }
    Push(watched, day, state, extra.str());
  }

  static void Push(Watched& watched,
                   std::int64_t day,
                   const std::string& state,
                   const std::string& extra) {
    if (!watched.runs.empty() && watched.runs.back().state == state) {
      watched.runs.back().last = day;
      return;
    }
    watched.runs.push_back(WaitRun{day, day, state, extra});
  }

  void PrintRuns(std::ostream& out,
                 std::uint64_t seed,
                 const char* name,
                 const Watched& watched) const {
    for (const WaitRun& run : watched.runs) {
      out << "population_curve: G1285W seed " << seed << ' ' << name << " d" << run.first << '-'
          << run.last << " (" << (run.last - run.first + 1) << " d) " << run.state << run.extra
          << '\n';
    }
    if (!watched.done) {
      out << "population_curve: G1285W seed " << seed << ' ' << name << " NOT STANDING by the "
          << "watch's end\n";
    }
  }

  /// From the first granary's standing day while the church holds grain:
  /// the church's grain, its emptying order, and where the carts were at noon.
  void CountChurch(const core::WorldState& world, std::int64_t day) {
    bool granary_stands = false;
    core::Grams church = 0;
    bool emptying = false;
    for (const core::UnitRow& unit : world.units.rows) {
      granary_stands = granary_stands || (unit.type.value == granary_ && unit.level > 0);
      if (unit.type.value != church_ || unit.level == 0) {
        continue;
      }
      emptying = emptying || unit.emptying != 0;
      for (std::size_t r = 0; r < unit.stock.size() && r < grain_.size(); ++r) {
        church += grain_[r] != 0 ? unit.stock[r] : 0;
      }
    }
    if (!granary_stands || church <= 0) {
      return;
    }
    if (church_lines_.size() >= kMaxChurchLines) {
      ++church_lines_dropped_;
      return;
    }
    std::ostringstream line;
    line << "d" << day << " church_grain_t " << church / core::kGramsPerTonne << " emptying "
         << (emptying ? 1 : 0) << " xfer_hands " << xfer_hands_ << " carts_by_task field_heap "
         << carts_by_kind_[static_cast<std::size_t>(core::LogisticsLoadKind::kFieldHeap)]
         << " stand_logs "
         << carts_by_kind_[static_cast<std::size_t>(core::LogisticsLoadKind::kStandLogs)]
         << " store_xfer "
         << carts_by_kind_[static_cast<std::size_t>(core::LogisticsLoadKind::kStoreTransfer)]
         << " site_supply "
         << carts_by_kind_[static_cast<std::size_t>(core::LogisticsLoadKind::kSiteSupply)]
         << " other " << OtherCarts();
    church_lines_.push_back(line.str());
  }

  std::uint32_t OtherCarts() const {
    std::uint32_t other = 0;
    for (std::size_t kind = 0; kind < carts_by_kind_.size(); ++kind) {
      const auto load = static_cast<core::LogisticsLoadKind>(kind);
      if (load != core::LogisticsLoadKind::kFieldHeap &&
          load != core::LogisticsLoadKind::kStandLogs &&
          load != core::LogisticsLoadKind::kStoreTransfer &&
          load != core::LogisticsLoadKind::kSiteSupply) {
        other += carts_by_kind_[kind];
      }
    }
    return other;
  }

  std::vector<std::string> type_keys_;
  std::vector<std::string> resource_keys_;
  std::vector<std::uint8_t> grain_;
  std::uint16_t granary_ = 0xFFFFU, long_store_ = 0xFFFFU, church_ = 0xFFFFU;
  std::unordered_map<std::uint32_t, Site> sites_;
  std::vector<std::uint32_t> order_;
  Watched granary_watch_;
  Watched store_watch_;
  std::unordered_map<std::uint32_t, std::uint32_t> crew_;
  std::unordered_map<std::uint32_t, std::uint32_t> carriers_;
  std::array<std::uint32_t,
             static_cast<std::size_t>(core::LogisticsLoadKind::kLogisticsLoadKindCount) + 1>
      carts_by_kind_ = {};
  std::uint32_t xfer_hands_ = 0;
  std::vector<std::string> church_lines_;
  std::uint32_t church_lines_dropped_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_SITE_WAIT_TALLY_H_
