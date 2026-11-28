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
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      firewood_ = resources->FindRowByKey("firewood");
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
          adults.empty()
              ? -1.0F
              : adults[static_cast<std::size_t>(0.20 * static_cast<double>(adults.size()))];
      const auto hardy_index =
          static_cast<std::size_t>(std::ceil(0.85 * static_cast<double>(adults.size())));
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
    // econ's C3 denominator: resident-days at work on a cold day (outdoor under +10, the burn's
    // threshold) - every resident with a work assignment that day.
    if (world.weather.air_temperature_celsius < 10.0F) {
      for (const core::ResidentRow& resident : world.residents.rows) {
        cold_work_days_ += resident.work.kind != core::WorkKind::kNone ? 1U : 0U;
      }
    }
    // THE YARD'S DAY (hp2261f split ~1/10 of two splitters' capacity; hd2261a's month-close
    // read was blind - demand is rewritten before it): today's want and its two sides, the
    // holders at the yard, the m³ split today, and the heated units standing empty.
    if (firewood_yard_ != core::kNoTableRow && firewood_ != core::kNoTableRow) {
      std::uint32_t yard_row = core::kNoRow;
      for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
        if (world.units.rows[row].type.value == firewood_yard_ && world.units.rows[row].level > 0 &&
            world.units.rows[row].dead == 0) {
          yard_row = row;
          break;
        }
      }
      if (yard_row != core::kNoRow) {
        const core::UnitId yard_id = world.units.row_ids[yard_row];
        std::uint32_t at_yard = 0;
        std::uint32_t post_cold = 0;
        std::uint32_t post_frozen = 0;
        const core::UnitRow& yard_unit = world.units.rows[yard_row];
        for (const core::ResidentRow& resident : world.residents.rows) {
          const bool here = resident.work.kind == core::WorkKind::kUnitWork &&
                            resident.work.unit.value == yard_id.value;
          at_yard += here ? 1U : 0U;
          if (here) {
            // The holder's own efficiency terms (labor_day.cpp ResidentEfficiency's inputs).
            yard_month_.holder_days += 1.0;
            yard_month_.holder_health += static_cast<double>(resident.health);
            yard_month_.holder_rest += static_cast<double>(resident.rest);
            yard_month_.holder_satiety += static_cast<double>(resident.satiety);
            yard_month_.holder_travel_h += static_cast<double>(resident.work.travel_hours);
          }
          // econ for boss's map: the walk to the work, one way, hours - the firewood yard's
          // holders, and every craftsman of the parent yard at any of its modules.
          if (here && resident.work.travel_hours >= 0.0F) {
            year_walk_yard_.push_back(resident.work.travel_hours);
          }
          if (resident.post.profession.value == craftsman_ &&
              resident.post.unit.value == yard_unit.parent.value &&
              resident.work.kind == core::WorkKind::kUnitWork &&
              resident.work.travel_hours >= 0.0F) {
            year_walk_craft_.push_back(resident.work.travel_hours);
          }
          // econ's (B): the yard's would-be splitters, the parent's craftsmen, by cold stage.
          if (resident.post.profession.value == craftsman_ &&
              resident.post.unit.value == yard_unit.parent.value) {
            post_cold += resident.cold_stage == 1 ? 1U : 0U;
            post_frozen += resident.cold_stage == 2 ? 1U : 0U;
          }
        }
        yard_month_.post_cold += post_cold;
        yard_month_.post_frozen += post_frozen;
        double stores = 0.0;
        double buffers = 0.0;
        std::uint32_t empty = 0;
        std::uint32_t stoves = 0;
        for (const core::UnitRow& unit : world.units.rows) {
          if (unit.dead != 0) {
            continue;
          }
          stoves +=
              unit.heat_buffer_m3 > 0.0F || unit.household.value != core::kInvalidEntityIdValue
                  ? 1U
                  : 0U;
          if (firewood_ < unit.stock.size()) {
            stores += static_cast<double>(unit.stock[firewood_]) / 500'000.0;
          }
        }
        // econ's variant (2): each stove counts only up to ITS share of the coming 2 months'
        // burn - APPROXIMATED as need_2m ÷ the stoves (the probe has no per-unit burn), named.
        const double need_2m = static_cast<double>(world.heating.need_2m_m3);
        const double per_stove = stoves > 0 ? need_2m / static_cast<double>(stoves) : 0.0;
        double capped = 0.0;
        for (const core::UnitRow& unit : world.units.rows) {
          if (unit.dead == 0) {
            buffers += static_cast<double>(unit.heat_buffer_m3);
            capped += std::min(static_cast<double>(unit.heat_buffer_m3), per_stove);
          }
        }
        for (const core::FamilyRow& family : world.families.rows) {
          const std::uint32_t house = core::FindRow(world.units, family.house);
          empty +=
              house != core::kNoRow && !(world.units.rows[house].heat_buffer_m3 > 0.0F) ? 1U : 0U;
        }
        const double split_today = world.heating.probe_split_m3 - split_mark_;
        split_mark_ = world.heating.probe_split_m3;
        yard_month_.written += world.heating.probe_split_written_days - written_mark_;
        yard_month_.labor += world.heating.probe_split_labor_days - labor_mark_;
        yard_month_.drained += world.heating.probe_split_drained_days - drained_mark_;
        written_mark_ = world.heating.probe_split_written_days;
        labor_mark_ = world.heating.probe_split_labor_days;
        drained_mark_ = world.heating.probe_split_drained_days;
        YardDay& d = yard_month_;
        ++d.days;
        // The core's want (SplittingWant: stores + every buffer; fuel logs NOT counted) and
        // econ's (1) with the unsplit fuel logs counted (as the bot does) and (2) capped stoves.
        const double fuel_m3 = static_cast<double>(world.heating.fuel_log_grams) / 200'000.0 * 0.33;
        d.want_days += (need_2m > stores + buffers) ? 1U : 0U;
        d.want_with_logs_days += (need_2m > stores + buffers + fuel_m3) ? 1U : 0U;
        d.want_capped_days += (need_2m > stores + capped) ? 1U : 0U;
        d.capped += capped;
        d.holders_days += at_yard > 0 ? 1U : 0U;
        d.split_m3 += split_today;
        d.need_2m += need_2m;
        d.stores += stores;
        d.buffers += buffers;
        d.fuel_logs += static_cast<double>(world.heating.fuel_log_grams) / 1000.0;
        d.empty_houses += empty;
      }
    }
    if ((day + 1U) % core::kDaysPerMonth == 0 && yard_month_.days > 0) {
      const YardDay& d = yard_month_;
      const double n = d.days;
      std::ostringstream line;
      line << std::fixed << std::setprecision(1) << "month " << (day / core::kDaysPerMonth) + 1U
           << " days " << d.days << " want_days " << d.want_days << " want_with_logs_days "
           << d.want_with_logs_days << " want_capped_days " << d.want_capped_days << " capped_m3 "
           << d.capped / static_cast<double>(d.days) << " holders_days " << d.holders_days
           << " split_m3 " << d.split_m3 << " mean need_2m " << d.need_2m / n << " stores_m3 "
           << d.stores / n << " buffers_m3 " << d.buffers / n << " fuel_logs_kg " << d.fuel_logs / n
           << " empty_houses " << d.empty_houses / n << " craftsmen_kholodno " << d.post_cold / n
           << " craftsmen_zamerz " << d.post_frozen / n << " written_md " << d.written
           << " labor_md " << d.labor << " drained_md " << d.drained << " holder_days "
           << d.holder_days;
      if (d.holder_days > 0.0) {
        line << " holder_health " << d.holder_health / d.holder_days << " rest "
             << d.holder_rest / d.holder_days << " satiety " << d.holder_satiety / d.holder_days
             << " travel_h " << d.holder_travel_h / d.holder_days;
      }
      yard_day_lines_.push_back(line.str());
      yard_month_ = YardDay{};
    }
    if ((day + 1U) % core::kDaysPerYear == 0) {
      // The year's walks: median and p90 of the samples (one a holder-day).
      const auto quantiles = [](std::vector<float>& v) {
        if (v.empty()) {
          return std::string("none");
        }
        std::sort(v.begin(), v.end());
        std::ostringstream s;
        s << std::fixed << std::setprecision(2) << v[v.size() / 2] << '/'
          << v[std::min(v.size() - 1, (v.size() * 9) / 10)] << " n " << v.size();
        return s.str();
      };
      walk_lines_.push_back("year " + std::to_string((day / core::kDaysPerYear) + 1U) +
                            " walk_h(median/p90) firewood_yard_holders " +
                            quantiles(year_walk_yard_) + " utility_yard_craftsmen " +
                            quantiles(year_walk_craft_));
      year_walk_yard_.clear();
      year_walk_craft_.clear();
      year_work_days_.push_back(cold_work_days_);
      year_fuel_felled_.push_back(world.heating.probe_fuel_felled_m3 - fuel_felled_mark_);
      fuel_felled_mark_ = world.heating.probe_fuel_felled_m3;
      cold_work_days_ = 0;
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
    // C3's parts (econ): the cold-season resident-days at work, and the m³ felled for fuel; the
    // reader turns them into man-days (felling_days_per_m3, the splitting's labor_days).
    for (std::size_t year = 0; year < year_work_days_.size(); ++year) {
      out << "HEAT2261 seed " << seed << " year " << year + 1 << " c3 cold_work_resident_days "
          << year_work_days_[year] << " fuel_felled_m3 " << year_fuel_felled_[year] << '\n';
    }
    for (const std::string& line : yard_lines_) {
      out << "HEAT2261 seed " << seed << " yard " << line << '\n';
    }
    for (const std::string& line : walk_lines_) {
      out << "HEAT2261 seed " << seed << " walk " << line << '\n';
    }
    for (const std::string& line : yard_day_lines_) {
      out << "HEAT2261 seed " << seed << " yardday " << line << '\n';
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

  struct YardDay {
    std::uint32_t days = 0;
    std::uint32_t want_days = 0;
    std::uint32_t want_with_logs_days = 0;
    std::uint32_t want_capped_days = 0;
    double capped = 0.0;
    std::uint32_t holders_days = 0;
    double split_m3 = 0.0;
    double need_2m = 0.0;
    double stores = 0.0;
    double buffers = 0.0;
    double fuel_logs = 0.0;
    double empty_houses = 0.0;
    double post_cold = 0.0;
    double post_frozen = 0.0;
    double written = 0.0;
    double labor = 0.0;
    double drained = 0.0;
    double holder_days = 0.0;
    double holder_health = 0.0;
    double holder_rest = 0.0;
    double holder_satiety = 0.0;
    double holder_travel_h = 0.0;
  };

  std::vector<float> year_walk_yard_;
  std::vector<float> year_walk_craft_;
  std::vector<std::string> walk_lines_;
  double written_mark_ = 0.0;
  double labor_mark_ = 0.0;
  double drained_mark_ = 0.0;

  YardDay yard_month_;
  double split_mark_ = 0.0;
  std::uint32_t firewood_ = core::kNoTableRow;
  std::vector<std::string> yard_day_lines_;
  std::uint64_t cold_work_days_ = 0;
  double fuel_felled_mark_ = 0.0;
  std::vector<std::uint64_t> year_work_days_;
  std::vector<double> year_fuel_felled_;
  std::uint32_t firewood_yard_ = core::kNoTableRow;
  std::uint32_t craftsman_ = core::kNoTableRow;
  std::vector<std::string> yard_lines_;
  std::vector<std::array<std::uint32_t, 3>> band_lines_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_HEATING_PROBE_H_
