/// @file
/// @brief TRAIL2072 (#2072, econ's gates P1 and the trail print): by year and season, the
///        trodden paths at the day's look (rows, visible rows, visible km, the strongest) and
///        the walkers' one-way road in game hours per placed walker-day - winter apart (econ:
///        a rise over 10 % in winter flags the snow rules).
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.
///
/// GROUND, printed with the rows: a walker-day is a resident whose work today has a measured
/// road (WorkAssignment::travel_hours >= 0) and who neither rides a horse nor a cart; the
/// season is the calendar's (kDaysPerSeason, winter = the first season of the year and the
/// last month); the trails are WorldState::trails at the look. NOT MEASURED here: the laid-vs-
/// trodden saving on one leg (econ's third print) - it needs a laid path over a full trail,
/// which no bot lays yet.

#ifndef TESTS_RUN_COMMON_TRAIL_TALLY_H_
#define TESTS_RUN_COMMON_TRAIL_TALLY_H_

#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <ostream>

#include "core_common/calendar.h"
#include "core_common/labor_state.h"
#include "core_common/road_rules.h"
#include "core_common/trail_state.h"
#include "core_common/work_seam.h"
#include "core_common/world_state.h"

namespace run {

class TrailTally {
 public:
  void CountDay(const core::WorldState& world) {
    const std::uint32_t day = world.calendar.day;
    const std::uint32_t year = (day / core::kDaysPerYear) + 1U;
    const std::uint32_t month = (day % core::kDaysPerYear) / core::kDaysPerMonth;  // 0..11
    // Winter: December, January, February (months 11, 0, 1).
    const bool winter = month == 11U || month <= 1U;
    Year& row = by_year_[year];
    Half& half = winter ? row.winter : row.rest;
    // econ's S1: by season (spring Mar-May, summer Jun-Aug, autumn Sep-Nov, winter), and the
    // MUD days apart (the dirt bed muddy: roads.md §3's distraint on the sowing window).
    const std::size_t season = winter ? 3U : (month <= 4U ? 0U : (month <= 7U ? 1U : 2U));
    const bool mud =
        world.weather.road_beds.condition[static_cast<std::size_t>(core::RoadBed::kDirt)] ==
        core::RoadCondition::kMud;
    Half& by_season = row.season[season];
    for (const core::ResidentRow& person : world.residents.rows) {
      const core::WorkAssignment& work = person.work;
      // A walker by the world's own predicate (static review: the brigade cart's riders rode).
      if (work.kind == core::WorkKind::kNone || work.travel_hours < 0.0F ||
          core::WorkRidesOut(world, work)) {
        continue;
      }
      const auto hours = static_cast<double>(work.travel_hours);
      half.walk_hours += hours;
      ++half.walker_days;
      by_season.walk_hours += hours;
      ++by_season.walker_days;
      if (mud) {
        row.mud.walk_hours += hours;
        ++row.mud.walker_days;
      }
    }
    std::uint32_t visible = 0;
    double visible_m = 0.0;
    float strongest = 0.0F;
    for (const core::TrailRow& trail : world.trails.rows) {
      const float strength = core::TrailStrength(trail);
      strongest = strength > strongest ? strength : strongest;
      if (strength >= world.trails.visible_strength) {
        ++visible;
        const core::Vec2 a = core::TrailEndPosition(trail.a);
        const core::Vec2 b = core::TrailEndPosition(trail.b);
        const double length =
            std::hypot(static_cast<double>(b.x - a.x), static_cast<double>(b.y - a.y));
        visible_m += length;
        // econ's rule (b): one visible leg >= 300 m per village.
        row.longest_visible_m = length > row.longest_visible_m ? length : row.longest_visible_m;
      }
    }
    Month& at = row.months[month];
    at.rows_sum += world.trails.rows.size();
    at.visible_sum += visible;
    at.visible_m_sum += visible_m;
    at.strongest = strongest > at.strongest ? strongest : at.strongest;
    ++at.days;
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    for (const auto& [year, row] : by_year_) {
      out << "population_curve: TRAIL2072 seed " << seed << " year " << year
          << " ground=walker_day:travel_hours>=0,no_horse,no_cart;winter=Dec-Feb"
          << " walk_h_per_walker_day " << PerDay(row.rest) << " winter " << PerDay(row.winter)
          << " walker_days " << row.rest.walker_days << " winter " << row.winter.walker_days
          << " seasons(spr/sum/aut/win) " << PerDay(row.season[0]) << ' ' << PerDay(row.season[1])
          << ' ' << PerDay(row.season[2]) << ' ' << PerDay(row.season[3]) << " mud_days "
          << PerDay(row.mud) << " mud_walker_days " << row.mud.walker_days << " longest_visible_m "
          << row.longest_visible_m << " | by_month rows/visible/visible_km/strongest";
      for (const Month& month : row.months) {
        const double days = month.days > 0 ? static_cast<double>(month.days) : 1.0;
        out << ' ' << static_cast<double>(month.rows_sum) / days << '/'
            << static_cast<double>(month.visible_sum) / days << '/'
            << month.visible_m_sum / days / 1000.0 << '/' << month.strongest;
      }
      out << '\n';
    }
  }

 private:
  struct Half {
    double walk_hours = 0.0;
    std::uint64_t walker_days = 0;
  };

  struct Month {
    std::uint64_t rows_sum = 0;
    std::uint64_t visible_sum = 0;
    double visible_m_sum = 0.0;
    float strongest = 0.0F;
    std::uint32_t days = 0;
  };

  struct Year {
    Half rest;
    Half winter;
    std::array<Half, 4> season{};
    Half mud;
    double longest_visible_m = 0.0;
    std::array<Month, core::kMonthsPerYear> months{};
  };

  static double PerDay(const Half& half) {
    return half.walker_days > 0 ? half.walk_hours / static_cast<double>(half.walker_days) : 0.0;
  }

  std::map<std::uint32_t, Year> by_year_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_TRAIL_TALLY_H_
