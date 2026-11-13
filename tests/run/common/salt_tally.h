/// @file
/// @brief #1630 (B)'s instrument (econ's gates S0-S7, boss's three prints,
/// 12 Nov 2026): the salt a village-year - the table's and the barrels', the
/// families' steps, the bot's lots against the grant - and when the count's
/// lodge's salt first moved.
/// @threading SINGLE_THREADED
/// Test-side code, read once a day from the thread that owns the simulation.
///
/// A year's line is taken from the CLOSED book at the turn (day 0's hour-0
/// writes - the month's first salt issue among them - book into the closing
/// year, #1580), so its book fields are the year's whole. Per year:
///   * eaters: residents at the turn; families;
///   * table_salt_kg: the issue to the yards (ledger `issued` of salt) - the
///     table's salt; barrel_salt_kg: the shops' (ledger `processed` of salt);
///     salted made / meat and fish spoiled (S5);
///   * family-days on each step 0..3, counted at each day's end (S0, S1);
///   * lots: the bot's salt lots asked, the months it would have borrowed,
///     points on salt (lots × the lot's price) against the year's grant (S2).
/// Per village once: the first day any family reached steps 1, 2, 3 (S0);
/// the count's lodge's salt at the start and the first day it fell
/// («lodge_salt_drawn_day»): the lodge is a start unit nobody seals, so its
/// salt is the village's from day 0 unless that day says otherwise.

#ifndef TESTS_RUN_COMMON_SALT_TALLY_H_
#define TESTS_RUN_COMMON_SALT_TALLY_H_

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>

#include "core_common/calendar.h"
#include "core_common/salt_view.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "core_tables/tables.h"
#include "salt_buy_rule.h"

namespace run {

class SaltTally {
 public:
  static constexpr std::uint32_t kYears = 16;
  static constexpr std::size_t kSteps = 5;  // 0..4 (step 4 «край», #1630 (C))

  SaltTally(const core::ITableSet& tables, std::uint64_t seed) : seed_(seed) {
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      salt_ = resources->FindRowByKey("salt");
      salted_meat_ = resources->FindRowByKey("salted_meat");
      salted_fish_ = resources->FindRowByKey("salted_fish");
      meat_ = resources->FindRowByKey("meat");
      fish_ = resources->FindRowByKey("fish");
    }
    if (const core::ITable* const types = tables.FindTable("unit_types")) {
      lodge_type_ = types->FindRowByKey("count_lodge");
    }
  }

  void CountDayEnd(const core::ISimulation& simulation, const SaltBuyRule& rule) {
    const core::WorldState& world = simulation.CompletedState();
    const core::SimDay day = world.calendar.day;
    // THE YEAR'S FUND AFTER THE TURN (econ's E3b): the points at the end of a
    // year's first day - the grant, the carried points, less the salt loans'
    // debt taken at the limit's turn. Printed on that year's line.
    if (day % core::kDaysPerYear == 0) {
      fund_start_ = world.limit.points;
      if (day == 0) {
        year_fund_start_ = fund_start_;  // year 1 has no closing line before it
      }
    }
    // #1630 (C): the ending, the letters, the «Село кончилось» candidate, the
    // lamp's colour by the same door the layer reads (SaltToday).
    if (world.salt.ending_raised != 0 && !ending_day_) {
      ending_day_ = day;
    }
    if (world.salt.letter_year != last_letter_year_) {
      last_letter_year_ = world.salt.letter_year;
      ++letters_year_;
      if (!first_letter_day_) {
        first_letter_day_ = day;
      }
    }
    if (world.residents.rows.size() < kVillageEndedCandidate && !below40_day_) {
      below40_day_ = day;
    }
    const core::SaltView view = simulation.SaltToday();
    if (view.measured) {
      bool on_step = false;
      for (std::size_t step = 1; step < view.families_by_step.size(); ++step) {
        on_step = on_step || view.families_by_step[step] > 0;
      }
      // The lamp's own rule (salt_loan.cpp): days to the first «пресно» =
      // floor(N) + 4, 0 on a step; lit under 12, red at 8 and under.
      const double to_complaint =
          on_step ? 0.0 : std::floor(static_cast<double>(view.table_days_left)) + kStep1Days;
      if (to_complaint <= 2.0 * kStep1Days) {
        ++red_days_;
        if (!first_red_day_) {
          first_red_day_ = day;
        }
      } else if (to_complaint < 3.0 * kStep1Days) {
        ++yellow_days_;
        if (!first_yellow_day_) {
          first_yellow_day_ = day;
        }
      }
    }
    const std::uint32_t year = (day / core::kDaysPerYear) + 1U;
    if (day % core::kDaysPerYear == 0 && year >= 2 && year - 1U <= kYears) {
      CloseYear(world, rule, year - 1U);
    }
    if (year > kYears) {
      return;
    }
    for (const core::FamilyRow& family : world.families.rows) {
      const std::size_t step = family.salt_step < kSteps ? family.salt_step : kSteps - 1U;
      ++family_days_[step];
      for (std::size_t reached = 1; reached <= step; ++reached) {
        if (!first_step_day_[reached]) {
          first_step_day_[reached] = day;
        }
      }
    }
    const core::Grams lodge = LodgeSalt(world);
    if (!lodge_start_) {
      lodge_start_ = lodge;
    } else if (!lodge_drawn_day_ && lodge < last_lodge_) {
      lodge_drawn_day_ = day;
    }
    last_lodge_ = lodge;
  }

  void Print(std::ostream& out) const {
    out << lines_.str();
    out << "population_curve: SALT1630 seed " << seed_ << " first_step1_day " << Day(1)
        << " first_step2_day " << Day(2) << " first_step3_day " << Day(3) << " lodge_salt_start_kg "
        << (lodge_start_ ? *lodge_start_ / 1000 : -1) << " lodge_salt_drawn_day "
        << (lodge_drawn_day_ ? std::to_string(*lodge_drawn_day_) : std::string("never"))
        << (lodge_type_ == core::kNoTableRow ? " LODGE_NOT_IN_THE_TABLES" : "")
        << " first_step4_day " << Day(4) << " ending_day " << Opt(ending_day_) << " below40_day "
        << Opt(below40_day_) << " first_yellow_day " << Opt(first_yellow_day_) << " first_red_day "
        << Opt(first_red_day_) << " first_letter_day " << Opt(first_letter_day_) << '\n';
  }

 private:
  static core::Grams Of(const core::ResourceAmounts& amounts, std::uint32_t resource) {
    return resource < amounts.size() ? amounts[resource] : 0;
  }

  core::Grams LodgeSalt(const core::WorldState& world) const {
    core::Grams salt = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.type.value == lodge_type_) {
        salt += Of(unit.stock, salt_);
      }
    }
    return salt;
  }

  static std::string Opt(const std::optional<core::SimDay>& day) {
    return day ? std::to_string(*day) : std::string("never");
  }

  std::string Day(std::size_t step) const {
    return first_step_day_[step] ? std::to_string(*first_step_day_[step]) : std::string("never");
  }

  void CloseYear(const core::WorldState& world, const SaltBuyRule& rule, std::uint32_t year) {
    const core::YearLedger& book = world.ledger.closed;
    lines_ << "population_curve: SALT1630 seed " << seed_ << " year " << year << " eaters "
           << world.residents.rows.size() << " families " << world.families.rows.size()
           << " table_salt_kg " << Of(book.issued, salt_) / 1000 << " barrel_salt_kg "
           << Of(book.processed, salt_) / 1000 << " salted_made_kg "
           << (Of(book.made, salted_meat_) + Of(book.made, salted_fish_)) / 1000
           << " meat_fish_spoiled_kg " << (Of(book.spoiled, meat_) + Of(book.spoiled, fish_)) / 1000
           << " family_days_step";
    for (std::size_t step = 0; step < kSteps; ++step) {
      lines_ << ' ' << step << ':' << family_days_[step];
    }
    lines_ << " lots " << rule.LotsIn(year) << " would_borrow_months " << rule.WouldBorrowIn(year)
           << " salt_points " << static_cast<std::int64_t>(rule.LotsIn(year)) * rule.Points()
           << " grant " << book.limit_points_granted << " need_on " << (rule.NeedOn() ? 1 : 0)
           << " buy_on "
           << (rule.On() ? 1 : 0)
           // THE YEAR BOOK'S PEOPLE (econ's F1, boss 12 Nov): births and the
           // births a stop cancelled, by stop; weddings; deaths - the core's
           // only resident death cause is the age bands (RunDeaths, #1302);
           // departures; arrivals.
           << " births " << book.births << " births_drawn " << book.births_drawn
           << " stopped_health " << book.births_stopped_health << " stopped_away "
           << book.births_stopped_away << " stopped_satiety " << book.births_stopped_satiety
           << " weddings " << book.weddings << " deaths_age " << book.deaths - book.deaths_salt
           << " departures " << book.departures << " arrivals "
           << book.arrivals
           // #1630 (C): the salt's doors, its loans and the fund they cost.
           << " deaths_salt " << book.deaths_salt << " departures_salt " << book.departures_salt
           << " salt_loans " << book.salt_loans << " loans_asked " << rule.LoansIn(year)
           << " lamp_yellow_days " << yellow_days_ << " lamp_red_days " << red_days_ << " letters "
           << letters_year_ << " fund_start " << year_fund_start_ << '\n';
    family_days_ = {};
    yellow_days_ = 0;
    red_days_ = 0;
    letters_year_ = 0;
    year_fund_start_ = fund_start_;  // the year now opening: its line prints it
  }

  std::uint64_t seed_ = 0;
  std::uint32_t salt_ = core::kNoTableRow;
  std::uint32_t salted_meat_ = core::kNoTableRow;
  std::uint32_t salted_fish_ = core::kNoTableRow;
  std::uint32_t meat_ = core::kNoTableRow;
  std::uint32_t fish_ = core::kNoTableRow;
  std::uint32_t lodge_type_ = core::kNoTableRow;
  std::array<std::uint64_t, kSteps> family_days_{};
  std::array<std::optional<core::SimDay>, kSteps> first_step_day_{};
  std::optional<core::Grams> lodge_start_;
  core::Grams last_lodge_ = 0;
  std::optional<core::SimDay> lodge_drawn_day_;
  /// #1569's lower candidate for «Село кончилось» (the core has no trigger).
  static constexpr std::size_t kVillageEndedCandidate = 40;
  /// salt_step1_days' default (the lamp's days to the first «пресно»).
  static constexpr double kStep1Days = 4.0;
  std::optional<core::SimDay> ending_day_;
  std::optional<core::SimDay> below40_day_;
  std::optional<core::SimDay> first_yellow_day_;
  std::optional<core::SimDay> first_red_day_;
  std::optional<core::SimDay> first_letter_day_;
  std::uint16_t last_letter_year_ = 0;
  std::uint32_t letters_year_ = 0;
  std::uint32_t yellow_days_ = 0;
  std::uint32_t red_days_ = 0;
  std::int32_t fund_start_ = 0;       ///< points at the end of the latest year's first day
  std::int32_t year_fund_start_ = 0;  ///< the closing line's year's own
  std::ostringstream lines_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_SALT_TALLY_H_
