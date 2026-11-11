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
#include <cstdint>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>

#include "core_common/calendar.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "salt_buy_rule.h"

namespace run {

class SaltTally {
 public:
  static constexpr std::uint32_t kYears = 16;
  static constexpr std::size_t kSteps = 4;  // 0..3; step 4 is #1630 (C)'s

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

  void CountDayEnd(const core::WorldState& world, const SaltBuyRule& rule) {
    const core::SimDay day = world.calendar.day;
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
        << (lodge_type_ == core::kNoTableRow ? " LODGE_NOT_IN_THE_TABLES" : "") << '\n';
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
           << " buy_on " << (rule.On() ? 1 : 0) << '\n';
    family_days_ = {};
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
  std::ostringstream lines_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_SALT_TALLY_H_
