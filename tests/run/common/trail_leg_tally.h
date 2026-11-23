/// @file
/// @brief LEGS2072 (#2072, boss 23 Nov: «print the legs first»): why trails do or do not form.
///        Every day, every resident with a day's work who walks (no horse, no cart), his way from
///        home to the work's place on the day's index - the same Way the morning's tread asks -
///        split into its open-ground legs, by work group and by the leg's place in the way
///        (start: home to the network, end: the network to the work, straight: the whole way
///        across). Per year: walker-days, open legs, how many snap to ONE cell (tread nothing),
///        and their mean metres; and the walkers the morning's tread does NOT count today (every
///        worker here is counted; the tread counts only the accountant's placements - the
///        difference is the STUB's size).
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.

#ifndef TESTS_RUN_COMMON_TRAIL_LEG_TALLY_H_
#define TESTS_RUN_COMMON_TRAIL_LEG_TALLY_H_

#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <ostream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/labor_state.h"
#include "core_common/road_route.h"
#include "core_common/trail_state.h"
#include "core_common/work_seam.h"
#include "core_common/world_state.h"

namespace run {

class TrailLegTally {
 public:
  void CountDay(const core::WorldState& world) {
    const std::uint32_t year = (world.calendar.day / core::kDaysPerYear) + 1U;
    Year& row = by_year_[year];
    const std::shared_ptr<const core::RoadIndex> index = core::RoadIndexOf(world);
    for (const core::ResidentRow& person : world.residents.rows) {
      const core::WorkAssignment& work = person.work;
      if (work.kind == core::WorkKind::kNone) {
        continue;
      }
      const std::size_t group = GroupOf(work.kind);
      Group& at = row.groups[group];
      if (work.rides_horse != 0 || work.rides_cart_of.value != core::kInvalidEntityIdValue) {
        ++at.riders;
        continue;
      }
      core::Vec2 home{};
      core::Vec2 place{};
      if (!core::HomePositionOf(world, person.family, home) ||
          !core::WorkPlaceOf(world, work, place)) {
        ++at.no_place;
        continue;
      }
      ++at.walker_days;
      // THE TWO DOORS (#2072 (A), boss 23 Nov): his lattice route beside the straight door's
      // answer for the same pair; and the route's CPU, the tread's own work by proxy.
      steps.clear();
      const auto started = std::chrono::steady_clock::now();
      const float routed = core::WalkRouteKm(world, home, place, &steps);
      const auto spent = std::chrono::steady_clock::now() - started;
      row.route_seconds += std::chrono::duration<double>(spent).count();
      const float straight = core::RoadKm(world, core::TravelMode::kWalk, home, place);
      if (straight > 0.0F) {
        const double ratio = static_cast<double>(routed) / static_cast<double>(straight);
        row.ratio_sum += ratio;
        ++row.pairs;
        row.ratio_lo = ratio < row.ratio_lo ? ratio : row.ratio_lo;
        row.ratio_hi = ratio > row.ratio_hi ? ratio : row.ratio_hi;
        row.straight_cheaper += straight < routed - 1e-4F ? 1U : 0U;
      }
      for (const auto& [a, b] : steps) {
        row.edge_houses[EdgeKey(a, b)].insert(person.family.value);
      }
      const core::Route way = index->Way(core::TravelMode::kWalk, home, place);
      const std::size_t legs = way.legs.size();
      for (std::size_t leg_index = 0; leg_index < legs; ++leg_index) {
        const core::RouteLeg& leg = way.legs[leg_index];
        if (leg.on_road) {
          continue;
        }
        const std::size_t where = legs == 1 ? 2U : (leg_index == 0 ? 0U : 1U);
        Leg& kind = at.legs[where];
        ++kind.count;
        kind.metres += static_cast<double>(leg.length_m);
        const core::TrailEnd a = core::SnapTrailEnd(leg.from);
        const core::TrailEnd b = core::SnapTrailEnd(leg.to);
        if (a.x == b.x && a.y == b.y) {
          ++kind.one_cell;
        }
      }
    }
    ++row.days;
    row.worst_day_seconds = row.route_seconds - row.seconds_before_today > row.worst_day_seconds
                                ? row.route_seconds - row.seconds_before_today
                                : row.worst_day_seconds;
    row.seconds_before_today = row.route_seconds;
    // THE TRUNKS at the year's last day (econ: edges walked by >= 3 houses, and their share of
    // the trodden strength).
    if (world.calendar.day % core::kDaysPerYear == core::kDaysPerYear - 1U) {
      double all = 0.0;
      double trunk = 0.0;
      std::uint32_t trunks = 0;
      for (const core::TrailRow& trail : world.trails.rows) {
        const auto strength = static_cast<double>(core::TrailStrength(trail));
        all += strength;
        const auto found = row.edge_houses.find(EdgeKey(trail.a, trail.b));
        if (found != row.edge_houses.end() && found->second.size() >= 3) {
          trunk += strength;
          ++trunks;
        }
      }
      row.trunks = trunks;
      row.trunk_share = all > 0.0 ? trunk / all : 0.0;
      row.rows_at_year_end = world.trails.rows.size();
    }
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    static constexpr std::array<const char*, kGroups> kNames = {
        "field", "herd", "build", "felling", "unit", "other"};
    for (const auto& [year, row] : by_year_) {
      out << "population_curve: LEGS2072 seed " << seed << " year " << year
          << " ground=every_worker_walking;legs=start(home-net)/end(net-work)/straight;"
             "one_cell=snaps_to_one_cell(treads_nothing)"
          << " doors routed/straight mean "
          << (row.pairs > 0 ? row.ratio_sum / static_cast<double>(row.pairs) : 0.0) << " lo "
          << row.ratio_lo << " hi " << row.ratio_hi << " straight_cheaper " << row.straight_cheaper
          << " of " << row.pairs << " route_cpu_s_per_day "
          << (row.days > 0 ? row.route_seconds / static_cast<double>(row.days) : 0.0)
          << " worst_day " << row.worst_day_seconds << " trunks_ge3_houses " << row.trunks
          << " trunk_share " << row.trunk_share << " rows_year_end " << row.rows_at_year_end;
      for (std::size_t group = 0; group < kGroups; ++group) {
        const Group& at = row.groups[group];
        out << " | " << kNames[group] << " walker_days " << at.walker_days << " riders "
            << at.riders << " no_place " << at.no_place;
        static constexpr std::array<const char*, 3> kWhere = {"start", "end", "straight"};
        for (std::size_t where = 0; where < 3; ++where) {
          const Leg& leg = at.legs[where];
          out << ' ' << kWhere[where] << ' ' << leg.count << '/' << leg.one_cell << '/'
              << (leg.count > 0 ? leg.metres / static_cast<double>(leg.count) : 0.0) << "m";
        }
      }
      out << '\n';
    }
  }

 private:
  static constexpr std::size_t kGroups = 6;

  struct Leg {
    std::uint64_t count = 0;
    std::uint64_t one_cell = 0;
    double metres = 0.0;
  };

  struct Group {
    std::uint64_t walker_days = 0;
    std::uint64_t riders = 0;
    std::uint64_t no_place = 0;
    std::array<Leg, 3> legs{};
  };

  struct Year {
    std::array<Group, kGroups> groups{};
    double ratio_sum = 0.0;
    double ratio_lo = 1e9;
    double ratio_hi = 0.0;
    std::uint64_t pairs = 0;
    std::uint64_t straight_cheaper = 0;
    double route_seconds = 0.0;
    double seconds_before_today = 0.0;
    double worst_day_seconds = 0.0;
    std::uint32_t days = 0;
    std::uint32_t trunks = 0;
    double trunk_share = 0.0;
    std::size_t rows_at_year_end = 0;
    std::unordered_map<std::uint64_t, std::unordered_set<std::uint32_t>> edge_houses;
  };

  static std::uint64_t EdgeKey(core::TrailEnd a, core::TrailEnd b) {
    const auto cell = [](core::TrailEnd end) {
      return (static_cast<std::uint64_t>(static_cast<std::uint16_t>(end.x)) << 16U) |
             static_cast<std::uint16_t>(end.y);
    };
    const std::uint64_t one = cell(a);
    const std::uint64_t two = cell(b);
    return one < two ? (one << 32U) | two : (two << 32U) | one;
  }

  std::vector<std::pair<core::TrailEnd, core::TrailEnd>> steps;

  static std::size_t GroupOf(core::WorkKind kind) {
    switch (kind) {
      case core::WorkKind::kPlowing:
      case core::WorkKind::kHarrowing:
      case core::WorkKind::kSowing:
      case core::WorkKind::kHarvest:
        return 0;
      case core::WorkKind::kHerdCare:
        return 1;
      case core::WorkKind::kConstruction:
        return 2;
      case core::WorkKind::kFelling:
        return 3;
      case core::WorkKind::kUnitWork:
        return 4;
      default:
        return 5;
    }
  }

  std::map<std::uint32_t, Year> by_year_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_TRAIL_LEG_TALLY_H_
