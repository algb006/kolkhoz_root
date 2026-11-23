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
#include "core_common/labor_state.h"
#include "core_common/ledger_state.h"
#include "core_common/road_rules.h"
#include "core_common/world_state.h"

namespace run {

class DecompTally {
 public:
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
    for (const core::ResidentRow& person : world.residents.rows) {
      const core::WorkAssignment& work = person.work;
      if (work.kind == core::WorkKind::kNone) {
        continue;
      }
      ++row.worker_days;
      row.norm_days += static_cast<double>(work.worked_norm_days_today);
      row.hours_away += static_cast<double>(work.hours_away_today);
      const bool field = work.kind == core::WorkKind::kPlowing ||
                         work.kind == core::WorkKind::kHarrowing ||
                         work.kind == core::WorkKind::kSowing;
      if (field) {
        (mud ? row.field_norm_mud : row.field_norm_dry) +=
            static_cast<double>(work.worked_norm_days_today);
        (mud ? row.field_days_mud : row.field_days_dry) += 1U;
      }
      if (work.kind == core::WorkKind::kFelling && winter) {
        row.winter_felling_norm += static_cast<double>(work.worked_norm_days_today);
        ++row.winter_felling_days;
      }
    }
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
      out << " worker_days " << row.worker_days << " norm_per_worker_day "
          << per(row.norm_days, row.worker_days) << " hours_away_per_worker_day "
          << per(row.hours_away, row.worker_days) << " mud_days " << row.mud_days
          << " field_norm_per_day_mud " << per(row.field_norm_mud, row.field_days_mud) << " dry "
          << per(row.field_norm_dry, row.field_days_dry) << " field_days_mud " << row.field_days_mud
          << " dry " << row.field_days_dry << " winter_felling_norm " << row.winter_felling_norm
          << " winter_felling_days " << row.winter_felling_days << '\n';
    }
  }

 private:
  struct Year {
    core::YearLedger book;
    bool has_book = false;
    std::uint64_t worker_days = 0;
    double norm_days = 0.0;
    double hours_away = 0.0;
    std::uint32_t mud_days = 0;
    double field_norm_mud = 0.0;
    double field_norm_dry = 0.0;
    std::uint64_t field_days_mud = 0;
    std::uint64_t field_days_dry = 0;
    double winter_felling_norm = 0.0;
    std::uint64_t winter_felling_days = 0;
  };

  std::map<std::uint32_t, Year> by_year_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_DECOMP_TALLY_H_
