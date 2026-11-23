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
#include <cstdint>
#include <map>
#include <memory>
#include <ostream>

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
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    static constexpr std::array<const char*, kGroups> kNames = {
        "field", "herd", "build", "felling", "unit", "other"};
    for (const auto& [year, row] : by_year_) {
      out << "population_curve: LEGS2072 seed " << seed << " year " << year
          << " ground=every_worker_walking;legs=start(home-net)/end(net-work)/straight;"
             "one_cell=snaps_to_one_cell(treads_nothing)";
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
  };

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
