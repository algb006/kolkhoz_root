/// @file
/// @brief DECOMP2072 (boss 23 Nov: «which flow carries −24.6?»): by village-year, what the
///        walking time could move. Off the CLOSED year book: births, deaths, arrivals,
///        departures, the satiety's mean and minimum and the most hungry at once, the area sown,
///        harvested and lost, and the rest by source. Counted by day: the
///        worked norm-days and the hours away per worker-day, and the field works (plough,
///        harrow, sow) on the MUD days apart from the dry ones (econ's S2), the felling in the
///        winter months (S3).
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.

#ifndef TESTS_RUN_COMMON_DECOMP_TALLY_H_
#define TESTS_RUN_COMMON_DECOMP_TALLY_H_

#include <array>
#include <cstdint>
#include <map>
#include <ostream>

#include "core_common/calendar.h"
#include "core_common/horse_yard_road.h"
#include "core_common/labor_state.h"
#include "core_common/ledger_state.h"
#include "core_common/road_route.h"
#include "core_common/road_rules.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"

namespace run {

class DecompTally {
 public:
  /// @brief The sawmill's unit-type row (unit_types), for the sawyer-days by ground.
  void SetSawmill(std::uint32_t type_row) { sawmill_ = type_row; }

  void CountDay(const core::WorldState& world) {
    const std::uint32_t day = world.calendar.day;
    const std::uint32_t year = (day / core::kDaysPerYear) + 1U;
    // The closed book of last year, on the new year's first day.
    if (day > 0 && day % core::kDaysPerYear == 0 && year >= 2U) {
      by_year_[year - 1U].book = world.ledger.closed;
      by_year_[year - 1U].has_book = true;
    }
    Year& row = by_year_[year];
    const std::uint32_t month = (day % core::kDaysPerYear) / core::kDaysPerMonth;
    const bool winter = month == 11U || month <= 1U;
    const bool mud =
        world.weather.road_beds.condition[static_cast<std::size_t>(core::RoadBed::kDirt)] ==
        core::RoadCondition::kMud;
    row.mud_days += mud ? 1U : 0U;
    row.resident_days += world.residents.rows.size();
    for (const core::ResidentRow& person : world.residents.rows) {
      row.worker_days += person.work.kind == core::WorkKind::kNone ? 0U : 1U;
    }
    // econ's H-L: day-off leisure booked today, by the day's ground (dry / wet / mud / snow /
    // frozen: the dirt bed's condition), the rise of the current book since yesterday's look.
    {
      const auto leisure_at = static_cast<std::size_t>(core::RestSource::kDayOffLeisure);
      const std::uint32_t now = world.ledger.current.rest_by_source[leisure_at];
      const std::uint32_t before = day % core::kDaysPerYear == 0 ? 0U : last_leisure_;
      const auto condition = static_cast<std::size_t>(
          world.weather.road_beds.condition[static_cast<std::size_t>(core::RoadBed::kDirt)]);
      if (condition < row.leisure_by_ground.size()) {
        row.leisure_by_ground[condition] += now > before ? now - before : 0U;
        row.days_by_ground[condition] += 1U;
      }
      last_leisure_ = now;
    }
    // THE DAY'S MAN-DAYS AS THE HOURS BOOKED THEM (YearLedger::work_days_by_kind, current
    // book): today's is the rise since yesterday's look. The first version read each
    // resident's worked_norm_days_today, which the daily look sees already reset - all zeros.
    const auto& booked = world.ledger.current.work_days_by_kind;
    const bool new_year = day % core::kDaysPerYear == 0;
    const auto today = [&](core::WorkKind kind) {
      const auto at = static_cast<std::size_t>(kind);
      const float now = booked[at];
      const float before = new_year ? 0.0F : last_booked_[at];
      return static_cast<double>(now > before ? now - before : 0.0F);
    };
    double all = 0.0;
    for (std::size_t kind = 0; kind < booked.size(); ++kind) {
      all += today(static_cast<core::WorkKind>(kind));
    }
    row.norm_days += all;
    const double field = today(core::WorkKind::kPlowing) + today(core::WorkKind::kHarrowing) +
                         today(core::WorkKind::kSowing);
    (mud ? row.field_norm_mud : row.field_norm_dry) += field;
    (mud ? row.field_days_mud : row.field_days_dry) += 1U;
    if (winter) {
      row.winter_felling_norm += today(core::WorkKind::kFelling);
      ++row.winter_felling_days;
    }
    // OPTION 1's table answered (save 229): the planner's asks of yesterday's priced walks
    // and the answers, this village's thread since yesterday's look - a table that never
    // answers is a stub.
    {
      const core::PlacedWalkCount count = core::TakePlacedWalkCount();
      row.placed_asked += count.asked;
      row.placed_found += count.found;
    }
    // boss 24 Nov, B3's boards: the board chain's work by the day's ground (the dirt bed's
    // condition) - building, hauling, felling booked man-days, and the sawyers at the saw
    // (heads at the look: unit work's book does not split by unit).
    {
      const auto ground = static_cast<std::size_t>(
          world.weather.road_beds.condition[static_cast<std::size_t>(core::RoadBed::kDirt)]);
      if (ground < row.chain_by_ground.size()) {
        Chain& chain = row.chain_by_ground[ground];
        // boss 24 Nov: the hauling refused by reason and by ground (the current book's daily
        // rise), the haulers placed and their hours on the road (WorkRoadHours: for a carter
        // who starts at the horse yard, ONLY his walk to it - horse_yard_road.cpp).
        {
          const auto haul = static_cast<std::size_t>(core::WorkKind::kHauling);
          const core::YearLedger& now = world.ledger.current;
          for (std::size_t reason = 0; reason < core::kJobShortfallCount; ++reason) {
            const std::uint32_t value = now.short_job_days[haul][reason];
            const std::uint32_t before = new_year ? 0U : last_haul_short_[reason];
            chain.haul_short[reason] += value > before ? value - before : 0U;
            last_haul_short_[reason] = value;
          }
          const std::uint32_t blocked = now.road_blocked_job_days[haul];
          const std::uint32_t blocked_before = new_year ? 0U : last_haul_blocked_;
          chain.haul_blocked += blocked > blocked_before ? blocked - blocked_before : 0U;
          last_haul_blocked_ = blocked;
          for (const core::ResidentRow& person : world.residents.rows) {
            if (person.work.kind == core::WorkKind::kHauling && person.work.travel_hours >= 0.0F) {
              ++chain.haulers;
              chain.hauler_road_hours += static_cast<double>(person.work.travel_hours);
              chain.haulers_from_yard += core::DayStartsAtHorseYard(world, person.work) ? 1U : 0U;
            }
          }
        }
        chain.build += today(core::WorkKind::kConstruction);
        chain.haul += today(core::WorkKind::kHauling);
        chain.fell += today(core::WorkKind::kFelling);
        for (const core::ResidentRow& person : world.residents.rows) {
          if (person.work.kind != core::WorkKind::kUnitWork) {
            continue;
          }
          const std::uint32_t at = core::FindRow(world.units, person.work.unit);
          chain.sawyers +=
              at != core::kNoRow && world.units.rows[at].type.value == sawmill_ ? 1U : 0U;
        }
      }
    }
    last_booked_ = booked;
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    for (const auto& [year, row] : by_year_) {
      if (!row.has_book) {
        continue;  // the last, unclosed year: no book
      }
      const core::YearLedger& book = row.book;
      const auto per = [](double value, std::uint64_t days) {
        return days > 0 ? value / static_cast<double>(days) : 0.0;
      };
      out << "population_curve: DECOMP2072 seed " << seed << " year " << year
          << " ground=closed_book+daily_work;mud=dirt_bed_mud;winter=Dec-Feb births " << book.births
          << " deaths " << book.deaths << " arrivals " << book.arrivals << " departures "
          << book.departures << " satiety_mean "
          << per(static_cast<double>(book.satiety_day_mean_sum), book.satiety_days)
          << " satiety_min " << book.satiety_day_mean_min << " hungry_at_once_max "
          << book.hungry_at_once_max << " sown_ha " << book.area_sown_ha << " harvested_ha "
          << book.area_harvested_ha << " lost_ha " << book.area_lost_ha << " rest_by_source";
      for (const std::uint32_t days : book.rest_by_source) {
        out << ' ' << days;
      }
      // econ's felling refusals (B3's −73 %): the felling's job-days short by reason, and its
      // job-days the road stopped (the travel limit's «no working day left»).
      {
        const auto felling = static_cast<std::size_t>(core::WorkKind::kFelling);
        const auto& short_by = book.short_job_days[felling];
        out << " felling_short(no_horse/crew_cap/no_hands/road) "
            << short_by[static_cast<std::size_t>(core::JobShortfall::kNoHorse)] << '/'
            << short_by[static_cast<std::size_t>(core::JobShortfall::kCrewCap)] << '/'
            << short_by[static_cast<std::size_t>(core::JobShortfall::kNoHands)] << '/'
            << short_by[static_cast<std::size_t>(core::JobShortfall::kRoad)]
            << " felling_road_blocked " << book.road_blocked_job_days[felling]
            << " felling_man_days " << book.work_days_by_kind[felling];
      }
      // The births' denominator and stops (boss: «attempted vs stopped»).
      out << " births_drawn " << book.births_drawn << " stopped_health "
          << book.births_stopped_health << " stopped_away " << book.births_stopped_away
          << " stopped_satiety " << book.births_stopped_satiety;
      // Rest per resident-day (econ: a smaller village books fewer person-days by itself).
      out << " resident_days " << row.resident_days << " rest_per_resident_day";
      for (const std::uint32_t days : book.rest_by_source) {
        out << ' ' << per(static_cast<double>(days), row.resident_days);
      }
      out << " leisure_by_ground(dry/wet/mud/frozen/snow) days/booked";
      for (std::size_t ground = 0; ground < row.leisure_by_ground.size(); ++ground) {
        out << ' ' << row.days_by_ground[ground] << '/' << row.leisure_by_ground[ground];
      }
      out << " worker_days " << row.worker_days << " man_days_per_worker_day "
          << per(row.norm_days, row.worker_days) << " mud_days " << row.mud_days
          << " field_man_days_per_day_mud " << per(row.field_norm_mud, row.field_days_mud)
          << " dry " << per(row.field_norm_dry, row.field_days_dry) << " field_days_mud "
          << row.field_days_mud << " dry " << row.field_days_dry << " winter_felling_norm "
          << row.winter_felling_norm << " winter_felling_days " << row.winter_felling_days;
      static constexpr std::array<const char*, 5> kGrounds = {
          "dry", "wet", "mud", "frozen", "snow"};
      for (std::size_t ground = 0; ground < row.chain_by_ground.size(); ++ground) {
        const Chain& chain = row.chain_by_ground[ground];
        out << " chain_" << (ground < kGrounds.size() ? kGrounds[ground] : "other")
            << "(days/build/haul/fell/sawyer_heads) " << row.days_by_ground[ground] << '/'
            << chain.build << '/' << chain.haul << '/' << chain.fell << '/' << chain.sawyers;
        out << " haul_" << (ground < kGrounds.size() ? kGrounds[ground] : "other")
            << "(short_no_horse/crew_cap/no_hands/road/road_blocked/haulers/from_yard/"
               "road_h_per_hauler) "
            << chain.haul_short[0] << '/' << chain.haul_short[1] << '/' << chain.haul_short[2]
            << '/' << chain.haul_short[3] << '/' << chain.haul_blocked << '/' << chain.haulers
            << '/' << chain.haulers_from_yard << '/'
            << (chain.haulers > 0 ? chain.hauler_road_hours / static_cast<double>(chain.haulers)
                                  : 0.0);
      }
      out << " placed_walks(asked/found) " << row.placed_asked << '/' << row.placed_found
          << (row.placed_asked > 0 && row.placed_found == 0 ? " PLACED_WALKS_NEVER_FOUND" : "");
      out << '\n';
    }
  }

 private:
  struct Chain {
    double build = 0.0;
    double haul = 0.0;
    double fell = 0.0;
    std::uint64_t sawyers = 0;
    std::array<std::uint64_t, core::kJobShortfallCount> haul_short{};
    std::uint64_t haul_blocked = 0;
    std::uint64_t haulers = 0;
    std::uint64_t haulers_from_yard = 0;
    double hauler_road_hours = 0.0;
  };

  struct Year {
    core::YearLedger book;
    bool has_book = false;
    std::uint64_t worker_days = 0;
    std::uint64_t resident_days = 0;
    double norm_days = 0.0;
    std::array<std::uint32_t, static_cast<std::size_t>(core::RoadCondition::kRoadConditionCount)>
        leisure_by_ground{};
    std::array<std::uint32_t, static_cast<std::size_t>(core::RoadCondition::kRoadConditionCount)>
        days_by_ground{};
    std::uint32_t mud_days = 0;
    double field_norm_mud = 0.0;
    double field_norm_dry = 0.0;
    std::uint64_t field_days_mud = 0;
    std::uint64_t field_days_dry = 0;
    double winter_felling_norm = 0.0;
    std::uint64_t winter_felling_days = 0;
    std::array<Chain, static_cast<std::size_t>(core::RoadCondition::kRoadConditionCount)>
        chain_by_ground{};
    std::uint64_t placed_asked = 0;
    std::uint64_t placed_found = 0;
  };

  std::map<std::uint32_t, Year> by_year_;
  std::array<float, core::kWorkKindCount> last_booked_{};
  std::uint32_t last_leisure_ = 0;
  std::array<std::uint32_t, core::kJobShortfallCount> last_haul_short_{};
  std::uint32_t last_haul_blocked_ = 0;
  std::uint32_t sawmill_ = 0xFFFFFFFFU;  ///< no sawmill known: no sawyer counted
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_DECOMP_TALLY_H_
