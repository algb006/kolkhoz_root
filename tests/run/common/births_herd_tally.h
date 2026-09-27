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
/// Nothing here moves the world; the numbers are read, never used.

#ifndef TESTS_RUN_COMMON_BIRTHS_HERD_TALLY_H_
#define TESTS_RUN_COMMON_BIRTHS_HERD_TALLY_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

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
    }
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      oat_ = resources->FindRowByKey("oat");
      hay_ = resources->FindRowByKey("hay");
      rye_ = resources->FindRowByKey("rye");
    }
  }

  /// @brief One day, after the day's steps: the horses ordered that day, and
  ///        at the turn, the closed year.
  void CountDay(const core::WorldState& world) {
    CountOrderedHorses(world);
    const core::SimDay day = world.calendar.day;
    if (day != 0 && day % core::kDaysPerYear == 0 && day / core::kDaysPerYear != last_year_) {
      last_year_ = day / core::kDaysPerYear;
      CloseYear(world);
    }
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
                 "NOT MEASURED):";
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

  static double Tonnes(const core::ResourceAmounts& amounts, std::uint32_t resource) {
    return resource < amounts.size()
               ? static_cast<double>(amounts[resource]) / static_cast<double>(core::kGramsPerTonne)
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
    year.work = current_;
    current_ = Work{};
    years_.push_back(year);
  }

  float birth_satiety_stop_ = -1.0F;
  float life_speedup_ = 1.0F;
  float fertile_from_ = 0.0F;
  float fertile_to_ = 0.0F;
  std::uint32_t horse_kind_ = core::kNoTableRow;
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
