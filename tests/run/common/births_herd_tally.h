/// @file
/// @brief What held the births and what the horses did, year by year: econ's
/// two prints on the "Epoch I forever" branch (population_curve
/// --epoch-one-forever; boss-core-epoch1-queue [41], [42]).
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// BIRTHS (econ's question: why seed 1931 had no births in years 25-29 with
/// 200+ families — "the gate that actually closes is FOOD", or not): the
/// year's births, the families' mean yearly satiety against life.csv's
/// birth_satiety_stop and how many families stood under it, the couples
/// waiting for a house, and the women of the fertile ages (life.csv).
///
/// HORSES AND HARNESS (econ's audit horse-traction.md, the registry's «Лошадь
/// не становится механизацией Эпохи I»): the adult and young horses at the
/// turn, the horses ordered from the district, the herds' births and deaths (every kind — the book
/// does not split them), and the year's work by kind in person-days sampled at noon, one sample a
/// day: ploughing and harrowing, carting with a horse and on foot, mowing a meadow; the harnessed
/// job-days left short for want of a horse, and the book's own horse-backed and total assignment
/// days (the mechanisation component's numerator and denominator).
///
/// HAY (boss-core-epoch1-queue [78], for econ's clover rule): the hay reaped
/// and the meadows mown, what each kind ate and went short of (the book's by
/// kind, save 108), the horses' unfed head-days by month, and the hay left
/// at the turnout.
///
/// Nothing here moves the world; the numbers are read, never used.

#ifndef TESTS_RUN_COMMON_BIRTHS_HERD_TALLY_H_
#define TESTS_RUN_COMMON_BIRTHS_HERD_TALLY_H_

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/calendar.h"
#include "core_common/labor_state.h"
#include "core_common/land_state.h"
#include "core_common/limit_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

class BirthsHerdTally {
 public:
  explicit BirthsHerdTally(const core::ITableSet& tables) {
    if (const core::ITable* const life = tables.FindTable("life")) {
      const std::uint32_t value = life->FindColumn("value");
      const auto number = [life, value](const char* key, float fallback) {
        const std::uint32_t row = life->FindRowByKey(key);
        if (row == core::kNoTableRow) {
          return fallback;
        }
        return static_cast<float>(life->CellReal(row, value).value_or(fallback));
      };
      birth_satiety_stop_ = number("birth_satiety_stop", -1.0F);
      life_speedup_ = number("life_speedup", 1.0F);
      fertile_from_ = number("fertility_from_years", 0.0F);
      fertile_to_ = number("fertility_to_years", 0.0F);
    }
    if (const core::ITable* const kinds = tables.FindTable("livestock")) {
      horse_kind_ = kinds->FindRowByKey("horse");
      cow_kind_ = kinds->FindRowByKey("cow");
      sheep_kind_ = kinds->FindRowByKey("sheep");
    }
    // THE TURNOUT: the first day of the pasture months (farming.csv, human
    // months 1..12), the day the hay left in the stores is read (boss [78]).
    if (const core::ITable* const farming = tables.FindTable("farming")) {
      const std::uint32_t row = farming->FindRowByKey("pasture_from_month");
      if (row != core::kNoTableRow) {
        const std::optional<float> month = farming->CellReal(row, farming->FindColumn("value"));
        turnout_month_ = month.has_value() && *month >= 1.0F
                             ? static_cast<std::uint32_t>(*month) - 1U
                             : core::kNoTableRow;
      }
    }
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      oat_ = resources->FindRowByKey("oat");
      hay_ = resources->FindRowByKey("hay");
      rye_ = resources->FindRowByKey("rye");
    }
  }

  /// @brief One day, after the day's steps: the horses ordered that day; on
  ///        the turnout's first hour, the hay in every unit's stock (a
  ///        level-0 unit's too, which the herds cannot take from), read after
  ///        that day's herd walk, which runs on the day-change tick; and at
  ///        the turn, the closed year.
  void CountDay(const core::WorldState& world) {
    CountOrderedHorses(world);
    if (static_cast<std::uint32_t>(world.calendar.date.month) == turnout_month_ &&
        world.calendar.date.day_in_month == 0) {
      double hay_t = 0.0;
      for (const core::UnitRow& unit : world.units.rows) {
        hay_t += Tonnes(unit.stock, hay_);
      }
      current_.hay_at_turnout_t = hay_t;
    }
    const core::SimDay day = world.calendar.day;
    if (day != 0 && day % core::kDaysPerYear == 0 && day / core::kDaysPerYear != last_year_) {
      last_year_ = day / core::kDaysPerYear;
      CloseYear(world);
    }
  }

  /// @brief One day's alarms, read through the simulation's own door after
  ///        the day: the days the team's two alarms stood (0.37.8; boss
  ///        [84]: «печатай число суток горения по годам, чтобы не вышел
  ///        сторож, горящий всегда»).
  void CountAlarms(const std::vector<core::Alarm>& alarms) {
    bool on_hay = false;
    bool too_few = false;
    for (const core::Alarm& alarm : alarms) {
      on_hay = on_hay || alarm.kind == core::AlarmKind::kTeamOnHay;
      too_few = too_few || alarm.kind == core::AlarmKind::kTooFewHorses;
    }
    current_.team_on_hay_days += on_hay ? 1U : 0U;
    current_.too_few_horses_days += too_few ? 1U : 0U;
    ++current_.alarm_days;
  }

  /// The hour of the day the work is sampled at: noon — the day's close
  /// clears the placements (labor_system.cpp, CloseDay), so a sample taken
  /// after the day would count nobody.
  static constexpr std::uint32_t kSampleHour = 12;

  /// @brief The day's work, sampled once, at kSampleHour; and on a day the
  ///        plough or the harrow is out, the team's ration it is priced by
  ///        (WorldState::traction_ration; boss-core-epoch1-queue [54]: «паёк
  ///        упряжи весной — медиана и худший год»).
  void SampleWork(const core::WorldState& world) {
    bool ploughing = false;
    for (const core::ResidentRow& resident : world.residents.rows) {
      const core::WorkAssignment& work = resident.work;
      if (core::IsHorseWork(work.kind)) {
        ++current_.plough_harrow;
        ploughing = true;
      } else if (work.kind == core::WorkKind::kHauling) {
        ++(work.rides_horse != 0 ? current_.carting_with_horse : current_.carting_on_foot);
      } else if (work.kind == core::WorkKind::kHarvest && OnMeadow(world, work.field)) {
        ++current_.mowing;
      }
    }
    if (ploughing) {
      current_.ploughing_days += 1;
      current_.ploughing_ration_sum += world.traction_ration;
      current_.ploughing_ration_min =
          std::min(current_.ploughing_ration_min, world.traction_ration);
    }
    // THE HORSES' PRICE OF THE PEOPLE'S BARLEY (0.37.5; boss [73]): the
    // team's ration in the autumn and the winter (months 8-11, 0-1), when the
    // oats are held and the barley is the people's; and the horse head-days
    // unfed — short of hay too, not only of oats.
    const auto month = static_cast<std::uint32_t>(world.calendar.date.month);
    if (month >= 8 || month <= 1) {
      current_.cold_days += 1;
      current_.cold_ration_sum += world.traction_ration;
    }
    for (const core::HerdRow& herd : world.herds.rows) {
      if (herd.kind.value == horse_kind_ && herd.unfed_days > 0.0F) {
        const std::uint32_t heads =
            static_cast<std::uint32_t>(herd.adult_count) + herd.juvenile_count + herd.newborn_count;
        current_.horse_unfed_head_days += heads;
        // By month too (boss [78]): which months the horses go hungry in.
        if (month < current_.horse_unfed_by_month.size()) {
          current_.horse_unfed_by_month[month] += heads;
        }
      }
    }
  }

  /// @brief The village's lines, headed by its seed.
  void Print(const char* run_name, std::uint64_t seed) const {
    std::cout << run_name << ": seed " << seed
              << " births by closed year — births; families' mean yearly satiety [under "
                 "birth_satiety_stop "
              << birth_satiety_stop_ << "]; couples waiting for a house; women aged "
              << fertile_from_ << ".." << fertile_to_
              << "; the rye reaped, t (a reaped year before an unreaped one is a year next "
                 "year's hold keeps rye from the issue — boss-core-epoch1-queue [61]):";
    for (const Year& year : years_) {
      std::cout << "\n    year " << year.number << ": " << year.births << " births; satiety "
                << year.mean_satiety << " [" << year.families_under_stop << " of " << year.families
                << " under]; " << year.couples_waiting << " waiting; " << year.fertile_women
                << " women; rye reaped " << year.rye_reaped_t << " t";
    }
    std::cout << '\n'
              << run_name << ": seed " << seed
              << " horses and harness by closed year — adult / young horses at the turn; "
                 "horses ordered from the district; herd births, deaths by age / hunger "
                 "(every kind); person-days sampled at "
                 "noon: plough+harrow, carting with a horse / on foot, meadow "
                 "mowing; harnessed job-days short of a horse; horse-backed / harnessed / "
                 "total assignment days (the book's; the share is the first over the second "
                 "since 0.37.2); oats / hay the kolkhoz herds ate, t (the "
                 "book's `feed`, every kind); the team's ration on the days the plough or the "
                 "harrow was out, sampled at noon — least / mean over N days (no ploughing: "
                 "NOT MEASURED); the team's mean ration in the autumn and winter (months "
                 "8-11, 0-1); horse head-days unfed (short of hay too), sampled at noon:";
    for (const Year& year : years_) {
      std::cout << "\n    year " << year.number << ": " << year.adult_horses << " / "
                << year.young_horses << " horses; " << year.horses_bought << " bought; "
                << year.herd_births << " born, " << year.herd_deaths_age << " / "
                << year.herd_deaths_hunger << " died; " << year.work.plough_harrow
                << " plough+harrow, " << year.work.carting_with_horse << " / "
                << year.work.carting_on_foot << " carting, " << year.work.mowing << " mowing; "
                << year.short_of_horse << " short of a horse; " << year.horse_backed_days << " / "
                << year.harnessed_days << " / " << year.total_assignment_days << "; fed "
                << year.oat_fed_t << " / " << year.hay_fed_t << " t; ration ";
      if (year.work.ploughing_days == 0) {
        std::cout << "NOT MEASURED";
      } else {
        std::cout << year.work.ploughing_ration_min << " / "
                  << year.work.ploughing_ration_sum / static_cast<float>(year.work.ploughing_days)
                  << " over " << year.work.ploughing_days << " days";
      }
      std::cout << "; cold ration ";
      if (year.work.cold_days == 0) {
        std::cout << "NOT MEASURED";
      } else {
        std::cout << year.work.cold_ration_sum / static_cast<float>(year.work.cold_days);
      }
      std::cout << "; unfed " << year.work.horse_unfed_head_days << " head-days";
    }
    std::cout << '\n'
              << run_name << ": seed " << seed
              << " hay by closed year (boss-core-epoch1-queue [78]) — hay reaped, t (the book's "
                 "harvest: meadows and grass crops); meadow ha mown this year of standing; hay "
                 "the kolkhoz herds ate, t, horse / cow / sheep (the book's by kind, save 108); "
                 "their need left uncovered, in t of hay, horse / cow / sheep / every kind; "
                 "horse head-days unfed by month 1..12, sampled at noon; hay in the stores at the "
                 "turnout (first day of pasture_from_month; NOT MEASURED before it came):";
    // A key the tables do not have would print a silent nought in its place.
    if (hay_ == core::kNoTableRow || horse_kind_ == core::kNoTableRow ||
        cow_kind_ == core::kNoTableRow || sheep_kind_ == core::kNoTableRow ||
        turnout_month_ == core::kNoTableRow) {
      std::cout << "\n    NOT MEASURED: the tables lack hay, horse, cow, sheep or "
                   "pasture_from_month — their columns below read nought";
    }
    for (const Year& year : years_) {
      std::cout << "\n    year " << year.number << ": reaped (meadows and grass crops) "
                << year.hay.mown_t << " t; meadows mown " << year.hay.meadows_mown_ha << " of "
                << year.hay.meadows_ha << " ha; eaten " << year.hay.eaten_horse_t << " / "
                << year.hay.eaten_cow_t << " / " << year.hay.eaten_sheep_t << " t; short "
                << year.hay.short_horse_t << " / " << year.hay.short_cow_t << " / "
                << year.hay.short_sheep_t << " / " << year.hay.short_all_t
                << " t; horse unfed by month";
      for (const std::uint32_t heads : year.work.horse_unfed_by_month) {
        std::cout << ' ' << heads;
      }
      std::cout << "; at turnout ";
      if (year.work.hay_at_turnout_t < 0.0) {
        std::cout << "NOT MEASURED";
      } else {
        std::cout << year.work.hay_at_turnout_t << " t";
      }
    }
    std::cout << '\n'
              << run_name << ": seed " << seed
              << " the team's alarms by closed year (0.37.8; boss-core-epoch1-queue [84]) — days "
                 "kTeamOnHay stood (idle days it keeps standing included) / days kTooFewHorses "
                 "stood, of the days read (the "
                 "simulation's CollectAlarms after each day; 0 days read is NOT MEASURED); the "
                 "team's mean ration in the autumn and winter; the jobs stopped for want of a "
                 "horse (JobShortfall::kNoHorse, job-days — beside, not in, kTooFewHorses):";
    for (const Year& year : years_) {
      std::cout << "\n    year " << year.number << ": ";
      if (year.work.alarm_days == 0) {
        std::cout << "NOT MEASURED";
      } else {
        std::cout << "on hay " << year.work.team_on_hay_days << " / too few horses "
                  << year.work.too_few_horses_days << " of " << year.work.alarm_days << " days";
      }
      std::cout << "; cold ration ";
      if (year.work.cold_days == 0) {
        std::cout << "NOT MEASURED";
      } else {
        std::cout << year.work.cold_ration_sum / static_cast<float>(year.work.cold_days);
      }
      std::cout << "; short of a horse " << year.short_of_horse << " job-days";
    }
    std::cout << '\n';
  }

 private:
  struct Work {
    std::uint32_t plough_harrow = 0;
    std::uint32_t carting_with_horse = 0;
    std::uint32_t carting_on_foot = 0;
    std::uint32_t mowing = 0;
    std::uint32_t ploughing_days = 0;
    float ploughing_ration_sum = 0.0F;
    float ploughing_ration_min = 1.0F;
    std::uint32_t cold_days = 0;
    float cold_ration_sum = 0.0F;
    std::uint32_t horse_unfed_head_days = 0;
    std::array<std::uint32_t, core::kMonthsPerYear> horse_unfed_by_month = {};
    double hay_at_turnout_t = -1.0;  ///< Negative: the turnout day not reached.
    std::uint32_t team_on_hay_days = 0;
    std::uint32_t too_few_horses_days = 0;
    std::uint32_t alarm_days = 0;  ///< Days CountAlarms was called: 0 is NOT MEASURED.
  };

  /// The hay's year (boss [78]): what the meadows gave and on how much of
  /// them, what each kind ate, and what each went short of, in hay.
  struct Hay {
    double mown_t = 0.0;
    float meadows_mown_ha = 0.0F;
    float meadows_ha = 0.0F;
    double eaten_horse_t = 0.0;
    double eaten_cow_t = 0.0;
    double eaten_sheep_t = 0.0;
    double short_horse_t = 0.0;
    double short_cow_t = 0.0;
    double short_sheep_t = 0.0;
    double short_all_t = 0.0;
  };

  struct Year {
    std::uint32_t number = 0;
    std::uint32_t births = 0;
    float mean_satiety = 0.0F;
    std::uint32_t families = 0;
    std::uint32_t families_under_stop = 0;
    std::uint32_t couples_waiting = 0;
    std::uint32_t fertile_women = 0;
    std::uint32_t adult_horses = 0;
    std::uint32_t young_horses = 0;
    std::uint32_t horses_bought = 0;
    std::uint32_t herd_births = 0;
    std::uint32_t herd_deaths_age = 0;
    std::uint32_t herd_deaths_hunger = 0;
    std::uint32_t short_of_horse = 0;
    float horse_backed_days = 0.0F;
    float harnessed_days = 0.0F;
    float total_assignment_days = 0.0F;
    double oat_fed_t = 0.0;
    double hay_fed_t = 0.0;
    double rye_reaped_t = 0.0;
    Hay hay;
    Work work;
  };

  /// THE HORSES BOUGHT, read off the district's livestock arrivals: the book
  /// keeps no count of them. A row appears at the order and goes when the
  /// head lands, at least limit_delivery_days (2) later, so a row not seen
  /// the evening before is that day's order and is counted once, then.
  void CountOrderedHorses(const core::WorldState& world) {
    std::vector<std::uint32_t> today;
    const core::LivestockArrivalTable& arrivals = world.livestock_arrivals;
    for (std::size_t row = 0; row < arrivals.rows.size(); ++row) {
      const std::uint32_t id = arrivals.row_ids[row].value;
      today.push_back(id);
      const bool seen =
          std::find(arrivals_seen_.begin(), arrivals_seen_.end(), id) != arrivals_seen_.end();
      if (!seen && arrivals.rows[row].kind.value == horse_kind_) {
        current_bought_ += arrivals.rows[row].head_count;
      }
    }
    arrivals_seen_ = std::move(today);
  }

  /// Grams at `index` of a dense column — by ResourceId, or by
  /// LivestockKindId for the book's by-kind columns — in tonnes.
  static double Tonnes(const core::ResourceAmounts& amounts, std::uint32_t index) {
    return index < amounts.size()
               ? static_cast<double>(amounts[index]) / static_cast<double>(core::kGramsPerTonne)
               : 0.0;
  }

  static bool OnMeadow(const core::WorldState& world, core::FieldId field) {
    const std::uint32_t row = core::FindRow(world.fields, field);
    // Both meadows, as WorkRidesOut reads them (static review of 0.37.2).
    return row != core::kNoRow &&
           (world.fields.rows[row].kind == core::LandKind::kMeadow ||
            world.fields.rows[row].kind == core::LandKind::kFloodplainMeadow);
  }

  /// The turn has rotated the books (world.cpp): `closed` is the year just
  /// ended; the families and the herds are read as the turn left them.
  void CloseYear(const core::WorldState& world) {
    const core::YearLedger& book = world.ledger.closed;
    Year year;
    year.number = last_year_;
    year.births = book.births;
    float satiety_sum = 0.0F;
    for (const core::FamilyRow& family : world.families.rows) {
      satiety_sum += family.satiety_year_mean;
      year.families_under_stop += family.satiety_year_mean < birth_satiety_stop_ ? 1U : 0U;
    }
    year.families = static_cast<std::uint32_t>(world.families.rows.size());
    year.mean_satiety = year.families == 0 ? 0.0F : satiety_sum / static_cast<float>(year.families);
    year.couples_waiting = static_cast<std::uint32_t>(world.wedding_waits.rows.size());
    for (const core::ResidentRow& resident : world.residents.rows) {
      const float age =
          core::BiologicalAgeYears(life_speedup_, resident.birth_day, world.calendar.day);
      year.fertile_women +=
          resident.sex == core::Sex::kFemale && age >= fertile_from_ && age <= fertile_to_ ? 1U
                                                                                           : 0U;
    }
    for (const core::HerdRow& herd : world.herds.rows) {
      if (herd.kind.value == horse_kind_) {
        year.adult_horses += herd.adult_count;
        year.young_horses += herd.juvenile_count;
      }
    }
    year.horses_bought = current_bought_;
    current_bought_ = 0;
    year.herd_births = book.herd_births;
    year.herd_deaths_age = book.herd_deaths_age;
    year.herd_deaths_hunger = book.herd_deaths_hunger;
    for (const auto& by_cause : book.short_job_days) {
      year.short_of_horse += by_cause[static_cast<std::size_t>(core::JobShortfall::kNoHorse)];
    }
    year.horse_backed_days = book.horse_backed_assignment_days;
    year.harnessed_days = book.harnessed_assignment_days;
    year.total_assignment_days = book.total_assignment_days;
    year.oat_fed_t = Tonnes(book.feed, oat_);
    year.hay_fed_t = Tonnes(book.feed, hay_);
    year.rye_reaped_t = Tonnes(book.harvest, rye_);
    year.hay.mown_t = Tonnes(book.harvest, hay_);
    const core::SimDay closed_year = last_year_ - 1U;
    for (const core::FieldRow& field : world.fields.rows) {
      if (field.kind != core::LandKind::kMeadow &&
          field.kind != core::LandKind::kFloodplainMeadow) {
        continue;
      }
      year.hay.meadows_ha += field.area_ga;
      if (field.last_mown_day != core::kNeverMownDay &&
          field.last_mown_day / core::kDaysPerYear == closed_year) {
        year.hay.meadows_mown_ha += field.area_ga;
      }
    }
    year.hay.eaten_horse_t = Tonnes(book.herd_hay_eaten, horse_kind_);
    year.hay.eaten_cow_t = Tonnes(book.herd_hay_eaten, cow_kind_);
    year.hay.eaten_sheep_t = Tonnes(book.herd_hay_eaten, sheep_kind_);
    year.hay.short_horse_t = Tonnes(book.herd_feed_short, horse_kind_);
    year.hay.short_cow_t = Tonnes(book.herd_feed_short, cow_kind_);
    year.hay.short_sheep_t = Tonnes(book.herd_feed_short, sheep_kind_);
    for (std::uint32_t kind = 0; kind < book.herd_feed_short.size(); ++kind) {
      year.hay.short_all_t += Tonnes(book.herd_feed_short, kind);
    }
    year.work = current_;
    current_ = Work{};
    years_.push_back(year);
  }

  float birth_satiety_stop_ = -1.0F;
  float life_speedup_ = 1.0F;
  float fertile_from_ = 0.0F;
  float fertile_to_ = 0.0F;
  std::uint32_t horse_kind_ = core::kNoTableRow;
  std::uint32_t cow_kind_ = core::kNoTableRow;
  std::uint32_t sheep_kind_ = core::kNoTableRow;
  std::uint32_t turnout_month_ = core::kNoTableRow;
  std::uint32_t oat_ = core::kNoTableRow;
  std::uint32_t hay_ = core::kNoTableRow;
  std::uint32_t rye_ = core::kNoTableRow;
  std::uint32_t last_year_ = 0;
  Work current_;
  std::uint32_t current_bought_ = 0;
  std::vector<std::uint32_t> arrivals_seen_;
  std::vector<Year> years_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_BIRTHS_HERD_TALLY_H_
