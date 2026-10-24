/// @file
/// @brief The run chairman's sugar (#1240; the human, 24 October 2026: «Бот
///        покупает сахар»; econ's rule, core-boss-1238 [5]): what people eat
///        and no more, bought as the district's sugar lots.
/// @threading SINGLE_THREADED — between steps, on the sim thread, as
///        LimitPolicy that asks it.
///
/// THE RULE:
///   * WHEN: on a day the sugar in the stores AND on its way (the district's
///     carts) is under 3 months' issue - residents × sugar_kg_per_resident_year
///     ÷ 12 × 3. No trip: a limit lot is ordered any day (district_trip.cpp
///     asks the trip of the trade plan alone);
///   * HOW MUCH: the next 12 months' issue - residents now plus the last 12
///     months' growth - minus that sugar, rounded UP to whole lots;
///   * WHICH: the catalogue's lot that carries sugar and nothing else
///     (`sugar_lot`, 200 kg);
///   * NO HOARDING: never while the sugar covers 3 months - the still takes
///     what is above the month's issue, so at most about one lot.
/// The year's growth stands in for the §2 forecast's eaters, which no door
/// gives: this rule counts the residents a year ago itself (none in year 1:
/// growth 0 there).
#ifndef TESTS_RUN_COMMON_SUGAR_BUY_RULE_H_
#define TESTS_RUN_COMMON_SUGAR_BUY_RULE_H_

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <iostream>
#include <optional>

#include "core_catalog/limit_catalog.h"
#include "core_common/calendar.h"
#include "core_common/quantities.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

class SugarBuyRule {
 public:
  /// @brief Reads the sugar's row, the issue's number and the lot carrying
  ///        sugar alone; any missing - the rule stays silent and says so.
  void Read(const core::ITableSet& tables, const core::LimitCatalog& catalog) {
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      sugar_ = resources->FindRowByKey("sugar");
    }
    if (const core::ITable* const world = tables.FindTable("world_params")) {
      const std::uint32_t row = world->FindRowByKey("sugar_kg_per_resident_year");
      const std::uint32_t column = world->FindColumn("value");
      if (row != core::kNoTableRow && column != core::kNoTableColumn) {
        if (const std::optional<float> kg = world->CellReal(row, column)) {
          kg_per_resident_year_ = static_cast<double>(*kg);
        }
      }
    }
    for (std::uint32_t lot = 0; lot < catalog.lots.size() && sugar_ != core::kNoTableRow; ++lot) {
      const core::ResourceAmounts& goods = catalog.lots[lot].goods;
      bool only_sugar = sugar_ < goods.size() && goods[sugar_] > 0;
      for (std::size_t resource = 0; resource < goods.size() && only_sugar; ++resource) {
        only_sugar = resource == sugar_ || goods[resource] <= 0;
      }
      if (only_sugar) {
        lot_ = lot;
        lot_grams_ = goods[sugar_];
        break;
      }
    }
  }

  /// @brief The control arm (--no-sugar-buy): the rule orders nothing.
  void Enable(bool on) { on_ = on; }

  /// @brief Counts the day's residents for the year's growth; once a day.
  void Watch(const core::WorldState& world) {
    residents_by_day_.push_back(static_cast<std::uint32_t>(world.residents.rows.size()));
    while (residents_by_day_.size() > core::kDaysPerYear + 1U) {
      residents_by_day_.pop_front();
    }
  }

  /// @brief How many sugar lots the rule asks for today; 0 when the sugar
  ///        covers 3 months, the rule is off, or a table is missing.
  std::uint32_t LotsWanted(const core::WorldState& world) const {
    if (!on_ || lot_ == core::kNoTableRow || lot_grams_ <= 0) {
      return 0;
    }
    const double residents = static_cast<double>(world.residents.rows.size());
    const double month_g = residents * kg_per_resident_year_ * 1000.0 / core::kMonthsPerYear;
    const double sugar = static_cast<double>(SugarHeldAndComing(world));
    if (month_g <= 0.0 || sugar >= 3.0 * month_g) {
      return 0;
    }
    double growth = 0.0;
    if (residents_by_day_.size() > core::kDaysPerYear) {
      growth = std::max(0.0, residents - static_cast<double>(residents_by_day_.front()));
    }
    const double year_g = (residents + growth) * kg_per_resident_year_ * 1000.0;
    const double short_g = year_g - sugar;
    return short_g > 0.0
               ? static_cast<std::uint32_t>(std::ceil(short_g / static_cast<double>(lot_grams_)))
               : 0U;
  }

  [[nodiscard]] std::uint32_t Lot() const { return lot_; }

  /// @brief Counts a lot asked for (the district's verdict comes next step).
  void Bought() { ++bought_; }

  /// @brief The run's line: what the rule bought, or why it could not.
  void Report(const char* run) const {
    if (!on_) {
      std::cout << run << ": the sugar rule is OFF (--no-sugar-buy): no sugar lot was asked\n";
      return;
    }
    if (lot_ == core::kNoTableRow) {
      std::cout << run << ": the sugar rule found NO lot carrying sugar alone - NOT MEASURED\n";
      return;
    }
    std::cout << run << ": the run's chairman asked for " << bought_
              << " sugar lots (what people eat, 3 months' issue the floor; a refused one is "
                 "asked again)\n";
  }

 private:
  core::Grams SugarHeldAndComing(const core::WorldState& world) const {
    core::Grams sugar = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      sugar += sugar_ < unit.stock.size() ? unit.stock[sugar_] : 0;
    }
    for (const core::LimitDeliveryRow& cart : world.limit_deliveries.rows) {
      sugar += sugar_ < cart.goods.size() ? cart.goods[sugar_] : 0;
    }
    return sugar;
  }

  std::uint32_t sugar_ = core::kNoTableRow;
  std::uint32_t lot_ = core::kNoTableRow;
  core::Grams lot_grams_ = 0;
  double kg_per_resident_year_ = 1.0;
  bool on_ = true;
  std::uint32_t bought_ = 0;
  std::deque<std::uint32_t> residents_by_day_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_SUGAR_BUY_RULE_H_
