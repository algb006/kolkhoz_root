/// @file
/// @brief HEAT2261 (#2261 stage 1, econ's base rates before the gates): the heated units' burn
///        by month - unit-days that wanted fuel, of them the days the buffer did not cover,
///        the same for dwellings, m³ burnt and m³ short. Read off HeatingState's cumulative
///        probe tallies at each month's last day; printed per year and by month of the year.
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.
#ifndef TESTS_RUN_COMMON_HEATING_PROBE_H_
#define TESTS_RUN_COMMON_HEATING_PROBE_H_

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "core_catalog/world_conventions.h"
#include "core_common/calendar.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "core_tables/tables.h"

namespace run {

class HeatingProbe {
 public:
  explicit HeatingProbe(const core::ITableSet& tables)
      : life_speedup_(core::LifeSpeedupOr(tables, 4.0F)) {
    if (const core::ITable* const types = tables.FindTable("unit_types")) {
      firewood_yard_ = types->FindRowByKey("firewood_yard");
    }
    if (const core::ITable* const professions = tables.FindTable("professions")) {
      craftsman_ = professions->FindRowByKey("farm_craftsman");
    }
  }

  /// @brief Once a day after the step: on a month's last day, the month's deltas.
  void CountDayEnd(const core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    const std::uint32_t day = world.calendar.day;
    // econ's base rate for the STUB endurance bands (heating.cpp's: hardy ≥ 120 of 200; weak
    // under 60, health < 40 or ≥ 55 biological years), each year's last day.
    if ((day + 1U) % core::kDaysPerYear == 0) {
      // heating.cpp's bands mirrored: by quantile of the adults (16+), weak = bottom 20 % or
      // health < 40 or 60+, hardy = top 15 %.
      std::vector<float> adults;
      for (const core::ResidentRow& resident : world.residents.rows) {
        if (core::BiologicalAgeYears(life_speedup_, resident.birth_day, day) >= 16.0F) {
          adults.push_back(resident.stamina + resident.sportiness);
        }
      }
      std::sort(adults.begin(), adults.end());
      const float weak_cut =
          adults.empty() ? -1.0F : adults[static_cast<std::size_t>(0.20 * adults.size())];
      const auto hardy_index = static_cast<std::size_t>(std::ceil(0.85 * adults.size()));
      const float hardy_cut =
          hardy_index < adults.size() ? adults[hardy_index] : std::numeric_limits<float>::max();
      std::array<std::uint32_t, 3> bands{};  // hardy, ordinary, weak
      for (const core::ResidentRow& resident : world.residents.rows) {
        const float toughness = resident.stamina + resident.sportiness;
        const float age = core::BiologicalAgeYears(life_speedup_, resident.birth_day, day);
        const std::size_t band = (toughness < weak_cut || resident.health < 40.0F || age >= 60.0F)
                                     ? 2U
                                     : (toughness >= hardy_cut ? 0U : 1U);
        ++bands[band];
      }
      band_lines_.push_back(bands);
    }
    if ((day + 1U) % core::kDaysPerMonth != 0) {
      return;
    }
    const core::HeatingState& heating = world.heating;
    Month month;
    month.index = day / core::kDaysPerMonth;
    month.want = heating.probe_want_unit_days - last_.probe_want_unit_days;
    month.empty = heating.probe_empty_unit_days - last_.probe_empty_unit_days;
    month.want_house = heating.probe_want_house_days - last_.probe_want_house_days;
    month.empty_house = heating.probe_empty_house_days - last_.probe_empty_house_days;
    month.burnt = heating.probe_burnt_m3 - last_.probe_burnt_m3;
    month.short_m3 = heating.probe_short_m3 - last_.probe_short_m3;
    month.refills = heating.probe_refills - last_.probe_refills;
    month.refill_short = heating.probe_refill_short - last_.probe_refill_short;
    month.refill_m3 = heating.probe_refill_m3 - last_.probe_refill_m3;
    month.split_days = heating.probe_split_days - last_.probe_split_days;
    month.split_floor_days = heating.probe_split_floor_days - last_.probe_split_floor_days;
    month.split_m3 = heating.probe_split_m3 - last_.probe_split_m3;
    month.deadwood_m3 = heating.probe_deadwood_m3 - last_.probe_deadwood_m3;
    month.cold_season = heating.probe_cold_season_days - last_.probe_cold_season_days;
    month.kholodno = heating.probe_cold_stage_days - last_.probe_cold_stage_days;
    month.zamerz = heating.probe_frozen_stage_days - last_.probe_frozen_stage_days;
    month.hits = heating.probe_health_hits - last_.probe_health_hits;
    months_.push_back(month);
    last_ = heating;
    // The buffers standing at the month's end, m³, and how many heated units hold any.
    double standing = 0.0;
    std::uint32_t holding = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.dead == 0 && unit.heat_buffer_m3 > 0.0F) {
        standing += static_cast<double>(unit.heat_buffer_m3);
        ++holding;
      }
    }
    months_.back().standing = standing;
    months_.back().holding = holding;
    // WHY THE YARD DOES NOT SPLIT (hc2261k/m: 1 day in 6 years): the firewood yard's state at
    // the month's close - level, work written / remaining, its parent's farm craftsmen - beside
    // the demand and what the village holds.
    if (firewood_yard_ != core::kNoTableRow) {
      for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
        const core::UnitRow& yard = world.units.rows[row];
        if (yard.type.value != firewood_yard_ || yard.dead != 0) {
          continue;
        }
        std::uint32_t craftsmen = 0;
        for (const core::ResidentRow& resident : world.residents.rows) {
          craftsmen += resident.post.profession.value == craftsman_ &&
                               resident.post.unit.value == yard.parent.value
                           ? 1U
                           : 0U;
        }
        std::ostringstream line;
        line << "month " << (day / core::kDaysPerMonth) + 1U << " yard level "
             << static_cast<int>(yard.level) << " phase "
             << static_cast<int>(yard.construction.phase) << " paused "
             << static_cast<int>(yard.paused) << " written " << yard.production_days_written
             << " remaining " << yard.production_days_remaining << " parent_craftsmen " << craftsmen
             << " need_2m " << world.heating.need_2m_m3 << " need_1m " << world.heating.need_1m_m3
             << " buffers_m3 " << standing;
        yard_lines_.push_back(line.str());
        break;
      }
    }
    // econ's C2 second half: the village's mean health on month 5's first day (the month that
    // closes now is month 4 - its last day is the eve).
    if (world.calendar.date.month == core::Month::kApril && !world.residents.rows.empty()) {
      double health = 0.0;
      for (const core::ResidentRow& resident : world.residents.rows) {
        health += static_cast<double>(resident.health);
      }
      months_.back().health_mean = health / static_cast<double>(world.residents.rows.size());
    }
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    if (months_.empty()) {
      out << "HEAT2261 seed " << seed << " NOT MEASURED (no month closed)\n";
      return;
    }
    for (const std::string& line : yard_lines_) {
      out << "HEAT2261 seed " << seed << " yard " << line << '\n';
    }
    for (std::size_t year = 0; year < band_lines_.size(); ++year) {
      const std::array<std::uint32_t, 3>& b = band_lines_[year];
      const std::uint32_t all = b[0] + b[1] + b[2];
      out << "HEAT2261 seed " << seed << " year " << year + 1 << " bands(hardy/ordinary/weak) "
          << b[0] << ' ' << b[1] << ' ' << b[2] << " of " << all << '\n';
    }
    constexpr std::uint32_t kMonths = core::kDaysPerYear / core::kDaysPerMonth;
    std::array<Month, kMonths> by_month{};
    // The run's output is one stream every later printer shares: its format is restored on the
    // way out (hc2261d printed every tally after this one at .1 - a reader-breaking leak).
    const std::ios_base::fmtflags flags = out.flags();
    const std::streamsize precision = out.precision();

    struct Restore {
      std::ostream& out;
      std::ios_base::fmtflags flags;
      std::streamsize precision;

      ~Restore() {
        out.flags(flags);
        out.precision(precision);
      }
    } restore{out, flags, precision};

    out << std::fixed << std::setprecision(1);
    for (std::size_t first = 0; first < months_.size(); first += kMonths) {
      Month year;
      for (std::size_t i = first; i < first + kMonths && i < months_.size(); ++i) {
        Add(year, months_[i]);
        Add(by_month[months_[i].index % kMonths], months_[i]);
      }
      out << "HEAT2261 seed " << seed << " year " << (first / kMonths) + 1 << ' ';
      Line(out, year);
      const Month& last = months_[std::min(first + kMonths, months_.size()) - 1];
      out << " standing_m3 " << last.standing << " holding " << last.holding;
      for (std::size_t i = first; i < first + kMonths && i < months_.size(); ++i) {
        if (months_[i].health_mean >= 0.0) {
          out << " health_mean_may1 " << months_[i].health_mean;  // April's last day's close
        }
      }
      out << '\n';
    }
    for (std::uint32_t m = 0; m < kMonths; ++m) {
      out << "HEAT2261 seed " << seed << " month " << m + 1 << ' ';
      Line(out, by_month[m]);
      out << '\n';
    }
  }

 private:
  struct Month {
    std::uint32_t index = 0;
    std::uint64_t want = 0;
    std::uint64_t empty = 0;
    std::uint64_t want_house = 0;
    std::uint64_t empty_house = 0;
    double burnt = 0.0;
    double short_m3 = 0.0;
    std::uint64_t refills = 0;
    std::uint64_t refill_short = 0;
    double refill_m3 = 0.0;
    std::uint64_t split_days = 0;
    std::uint64_t split_floor_days = 0;
    double split_m3 = 0.0;
    double deadwood_m3 = 0.0;
    std::uint64_t cold_season = 0;
    std::uint64_t kholodno = 0;
    std::uint64_t zamerz = 0;
    std::uint64_t hits = 0;
    double health_mean = -1.0;
    double standing = 0.0;
    std::uint32_t holding = 0;
  };

  static void Add(Month& into, const Month& month) {
    into.want += month.want;
    into.empty += month.empty;
    into.want_house += month.want_house;
    into.empty_house += month.empty_house;
    into.burnt += month.burnt;
    into.short_m3 += month.short_m3;
    into.refills += month.refills;
    into.refill_short += month.refill_short;
    into.refill_m3 += month.refill_m3;
    into.split_days += month.split_days;
    into.split_floor_days += month.split_floor_days;
    into.split_m3 += month.split_m3;
    into.deadwood_m3 += month.deadwood_m3;
    into.cold_season += month.cold_season;
    into.kholodno += month.kholodno;
    into.zamerz += month.zamerz;
    into.hits += month.hits;
  }

  static double Pct(std::uint64_t part, std::uint64_t whole) {
    return whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) / static_cast<double>(whole);
  }

  static void Line(std::ostream& out, const Month& m) {
    out << "want_unit_days " << m.want << " empty " << m.empty << " (" << Pct(m.empty, m.want)
        << "%) house_days " << m.want_house << " empty " << m.empty_house << " ("
        << Pct(m.empty_house, m.want_house) << "%) burnt_m3 " << m.burnt << " short_m3 "
        << m.short_m3 << " refills " << m.refills << " refill_short " << m.refill_short
        << " refill_m3 " << m.refill_m3 << " split_days " << m.split_days << " split_floor_days "
        << m.split_floor_days << " split_m3 " << m.split_m3 << " deadwood_m3 " << m.deadwood_m3
        << " cold_season_resident_days " << m.cold_season << " kholodno " << m.kholodno << " ("
        << Pct(m.kholodno, m.cold_season) << "%) zamerz " << m.zamerz << " ("
        << Pct(m.zamerz, m.cold_season) << "%) health_hits " << m.hits;
  }

  core::HeatingState last_;
  std::vector<Month> months_;
  float life_speedup_ = 4.0F;
  std::uint32_t firewood_yard_ = core::kNoTableRow;
  std::uint32_t craftsman_ = core::kNoTableRow;
  std::vector<std::string> yard_lines_;
  std::vector<std::array<std::uint32_t, 3>> band_lines_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_HEATING_PROBE_H_
