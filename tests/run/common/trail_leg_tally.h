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
///
/// THE CPU GATE: a trailed run may cost up to +15 % user CPU over B0. The +22 % of 24 Nov
/// (boss, decision (b), after six exact cuts took the first lattice's 2.9x to +21.1 %) was
/// TEMPORARY by Alex's rule - an algorithm that scales with the world is fixed, not gated - and
/// is withdrawn on the clean pair under it: the second Fable review's five exact cuts (#2390,
/// identity 0 lines of 20 419) and the revert of the trail-aware A* floor gave B0 11 256 s,
/// B2 12 703 s user = +12.85 % (offset 0, xeon17, one node and hour, 3fb1a252; the tree with
/// the floor, 311ed419, +14.20 %). PARKED LEVER: the query door's one sample per cell (about
/// -5 points, answer-changing) - back only if the game's tick budget is shown to hurt, then as
/// a paired arm with identity printed.
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.

#ifndef TESTS_RUN_COMMON_TRAIL_LEG_TALLY_H_
#define TESTS_RUN_COMMON_TRAIL_LEG_TALLY_H_

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <tuple>
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
      // Who rides, by the world's own predicate (the brigade cart's riders included).
      if (core::WorkRidesOut(world, work)) {
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
        // boss 24 Nov: THE STRAIGHT-CHEAPER SAMPLE - two a village-year, every leg of the way
        // with both doors' answers and the edges each door crosses.
        if (straight < routed - 1e-4F && row.samples.size() < 2U) {
          row.samples.push_back(SampleOf(world, *index, home, place, straight, routed));
        }
        // boss 24 Nov: WHERE the doors part - by the open ground's state today, the pair's
        // line length and whether the way is one open leg (across) or rides the network.
        {
          const core::RoadCondition dirt =
              world.weather.road_beds.condition[static_cast<std::size_t>(core::RoadBed::kDirt)];
          const std::size_t weather = dirt == core::RoadCondition::kMud      ? 1U
                                      : dirt == core::RoadCondition::kSnow   ? 2U
                                      : dirt == core::RoadCondition::kFrozen ? 3U
                                                                             : 0U;
          const float metres = std::hypot(place.x - home.x, place.y - home.y);
          const std::size_t band =
              metres < 50.0F ? 0U : (metres < 200.0F ? 1U : (metres < 500.0F ? 2U : 3U));
          const std::size_t across =
              index->Way(core::TravelMode::kWalk, home, place).legs.size() == 1U ? 1U : 0U;
          Case& at_case = row.cases[(((weather * 4U) + band) * 2U) + across];
          ++at_case.pairs;
          at_case.cheaper += straight < routed - 1e-4F ? 1U : 0U;
          at_case.over_110 += routed > 0.0F && straight > routed * 1.10F ? 1U : 0U;
        }
        // econ: the over-estimate straight ÷ route, its p50 and p90.
        if (routed > 0.0F) {
          row.over.push_back(static_cast<double>(straight) / static_cast<double>(routed));
        }
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
      {
        std::vector<double> over = row.over;
        std::sort(over.begin(), over.end());
        const auto at = [&over](double share) {
          return over.empty()
                     ? 0.0
                     : over[static_cast<std::size_t>(share * static_cast<double>(over.size() - 1))];
        };
        out << " over_straight_per_route p50 " << at(0.5) << " p90 " << at(0.9)
            << (at(0.9) > 1.10 ? " P90_OVER_1_10" : "");
        out << " cases(weather.band.across=pairs/cheaper/over110)";
        for (std::size_t at_case = 0; at_case < row.cases.size(); ++at_case) {
          const Case& one = row.cases[at_case];
          out << ' ' << (at_case / 8U) << '.' << ((at_case / 2U) % 4U) << '.' << (at_case % 2U)
              << '=' << one.pairs << '/' << one.cheaper << '/' << one.over_110;
        }
      }
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
      for (const std::string& sample : row.samples) {
        out << "population_curve: CHEAP2072 seed " << seed << " year " << year << ' ' << sample
            << '\n';
      }
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

  struct Case {
    std::uint64_t pairs = 0;
    std::uint64_t cheaper = 0;
    std::uint64_t over_110 = 0;
  };

  struct Year {
    std::array<Group, kGroups> groups{};
    double ratio_sum = 0.0;
    std::vector<double> over;          ///< straight ÷ route, every placed walker-day
    std::vector<std::string> samples;  ///< straight-cheaper pairs, legs and edges
    /// weather (dry-wet/mud/snow/frozen) x line band (<50/<200/<500/>=500 m) x across.
    std::array<Case, 32> cases{};
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

  /// One straight-cheaper pair: its two answers, then every leg - road legs by km, open legs
  /// by length, the straight door's km, the route's km (WalkRouteKm on the leg), and for each
  /// door its edges (count / trodden / diagonal / mean strength of the trodden). The straight
  /// door's edges are its own half-cell sampling replayed here (trail_lattice.cpp).
  static std::string SampleOf(const core::WorldState& world,
                              const core::RoadIndex& index,
                              core::Vec2 home,
                              core::Vec2 place,
                              float straight,
                              float routed) {
    std::ostringstream text;
    text << "straight " << straight << " routed " << routed << " legs";
    const core::Route way = index.Way(core::TravelMode::kWalk, home, place);
    for (const core::RouteLeg& leg : way.legs) {
      if (leg.on_road) {
        text << " | road " << leg.effective_km;
        continue;
      }
      std::vector<std::pair<core::TrailEnd, core::TrailEnd>> steps;
      const float leg_route = core::WalkRouteKm(world, leg.from, leg.to, &steps);
      text << " | open " << leg.length_m << "m straight " << leg.effective_km << " route "
           << leg_route << " route_edges " << EdgesOf(world, steps);
      std::vector<std::pair<core::TrailEnd, core::TrailEnd>> crossed;
      core::Vec2 from = leg.from;
      core::Vec2 to = leg.to;
      if (std::tie(to.x, to.y) < std::tie(from.x, from.y)) {
        std::swap(from, to);
      }
      const auto pieces =
          static_cast<std::uint32_t>(std::ceil(leg.length_m / (core::kTrailSnapMetres / 2.0F)));
      core::TrailEnd here = core::SnapTrailEnd(from);
      for (std::uint32_t piece = 1; piece <= pieces; ++piece) {
        const float share = static_cast<float>(piece) / static_cast<float>(pieces);
        const core::TrailEnd next = core::SnapTrailEnd(
            {.x = from.x + ((to.x - from.x) * share), .y = from.y + ((to.y - from.y) * share)});
        if (next.x != here.x || next.y != here.y) {
          crossed.emplace_back(here, next);
          here = next;
        }
      }
      text << " line_edges " << EdgesOf(world, crossed);
    }
    return text.str();
  }

  /// count/trodden/diagonal/mean strength of the trodden, of a list of lattice edges.
  static std::string EdgesOf(const core::WorldState& world,
                             const std::vector<std::pair<core::TrailEnd, core::TrailEnd>>& edges) {
    std::size_t trodden = 0;
    std::size_t diagonal = 0;
    double strength = 0.0;
    for (const auto& [a, b] : edges) {
      diagonal += a.x != b.x && a.y != b.y ? 1U : 0U;
      if (const core::TrailRow* const row = core::FindTrailEdge(world.trails, a, b)) {
        ++trodden;
        strength += static_cast<double>(core::TrailStrength(*row));
      }
    }
    std::ostringstream text;
    text << edges.size() << '/' << trodden << '/' << diagonal << '/'
         << (trodden > 0 ? strength / static_cast<double>(trodden) : 0.0);
    return text.str();
  }

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
