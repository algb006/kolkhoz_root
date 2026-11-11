/// @file
/// @brief The run chairman's salt (#1630 (B); econ's rule, salt-ending-b-
///        predictions-2026-11-11 §2): the district's salt lot when the
///        village's salt falls under three months of its table.
/// @threading SINGLE_THREADED — between steps, on the sim thread, as
///        LimitPolicy that asks it.
///
/// THE RULE (STUB econ):
///   * WHEN: the village's salt - the stores, the yards' pantries and the
///     district's carts on the way - is under salt_table_reserve_months of
///     its table (residents × salt_norm_g_per_eater_day × the month's days);
///   * HOW MUCH: one lot a month at most (`salt_lot`, 1 000 kg), the month of
///     the purchase remembered - econ's «on the first day of a month», moved
///     to the first day the policy may order in that month, so the LimitPolicy's
///     cooldown cannot skip the month;
///   * POINTS SHORT: nothing ordered, the month's «would borrow» counted
///     (the salt loan is #1630 (C)'s).
/// SILENT when the need is off (salt_need_on 0, every world before (B)) or
/// --no-salt-buy (the arm B2, the instrument's known direction).
#ifndef TESTS_RUN_COMMON_SALT_BUY_RULE_H_
#define TESTS_RUN_COMMON_SALT_BUY_RULE_H_

#include <cstdint>
#include <iostream>
#include <map>
#include <optional>

#include "core_catalog/limit_catalog.h"
#include "core_common/calendar.h"
#include "core_common/quantities.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

class SaltBuyRule {
 public:
  void Read(const core::ITableSet& tables, const core::LimitCatalog& catalog) {
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      salt_ = resources->FindRowByKey("salt");
    }
    if (const core::ITable* const world = tables.FindTable("world_params")) {
      const std::uint32_t column = world->FindColumn("value");
      const auto read = [world, column](const char* key, double& into) {
        const std::uint32_t row = world->FindRowByKey(key);
        if (row != core::kNoTableRow && column != core::kNoTableColumn) {
          if (const std::optional<float> value = world->CellReal(row, column)) {
            into = static_cast<double>(*value);
          }
        }
      };
      double on = 0.0;
      read("salt_need_on", on);
      need_on_ = on >= 0.5;
      read("salt_norm_g_per_eater_day", norm_g_);
      read("salt_table_reserve_months", reserve_months_);
    }
    for (std::uint32_t lot = 0; lot < catalog.lots.size() && salt_ != core::kNoTableRow; ++lot) {
      const core::ResourceAmounts& goods = catalog.lots[lot].goods;
      bool only_salt = salt_ < goods.size() && goods[salt_] > 0;
      for (std::size_t resource = 0; resource < goods.size() && only_salt; ++resource) {
        only_salt = resource == salt_ || goods[resource] <= 0;
      }
      if (only_salt) {
        lot_ = lot;
        points_ = catalog.lots[lot].points;
        break;
      }
    }
  }

  /// @brief The arm B2 (--no-salt-buy): the rule orders nothing.
  void Enable(bool on) { on_ = on; }

  /// @brief Whether a salt lot is asked today (and books it, or the «would
  ///        borrow» when the points do not cover it).
  bool WantsLot(const core::WorldState& world) {
    if (!on_ || !need_on_ || lot_ == core::kNoTableRow || points_ <= 0) {
      return false;
    }
    const std::uint32_t month = world.calendar.day / core::kDaysPerMonth;
    if (last_month_ && *last_month_ == month) {
      return false;
    }
    const double table_month = norm_g_ * static_cast<double>(world.residents.rows.size()) *
                               static_cast<double>(core::kDaysPerMonth);
    if (static_cast<double>(SaltHeldAndComing(world)) >= reserve_months_ * table_month) {
      return false;
    }
    last_month_ = month;
    const std::uint32_t year = (world.calendar.day / core::kDaysPerYear) + 1U;
    if (world.limit.points < points_) {
      ++would_borrow_[year];
      return false;
    }
    ++lots_[year];
    return true;
  }

  [[nodiscard]] std::uint32_t Lot() const { return lot_; }

  [[nodiscard]] std::int32_t Points() const { return points_; }

  [[nodiscard]] bool NeedOn() const { return need_on_; }

  [[nodiscard]] bool On() const { return on_; }

  /// @brief Lots asked in game year `year` (1-based), and months that wanted
  ///        one the points did not cover.
  [[nodiscard]] std::uint32_t LotsIn(std::uint32_t year) const {
    const auto it = lots_.find(year);
    return it == lots_.end() ? 0U : it->second;
  }

  [[nodiscard]] std::uint32_t WouldBorrowIn(std::uint32_t year) const {
    const auto it = would_borrow_.find(year);
    return it == would_borrow_.end() ? 0U : it->second;
  }

 private:
  core::Grams SaltHeldAndComing(const core::WorldState& world) const {
    core::Grams salt = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      salt += salt_ < unit.stock.size() ? unit.stock[salt_] : 0;
    }
    for (const core::FamilyRow& family : world.families.rows) {
      salt += salt_ < family.pantry.size() ? family.pantry[salt_] : 0;
    }
    for (const core::LimitDeliveryRow& cart : world.limit_deliveries.rows) {
      salt += salt_ < cart.goods.size() ? cart.goods[salt_] : 0;
    }
    return salt;
  }

  std::uint32_t salt_ = core::kNoTableRow;
  std::uint32_t lot_ = core::kNoTableRow;
  std::int32_t points_ = -1;
  double norm_g_ = 125.0;
  double reserve_months_ = 3.0;
  bool need_on_ = false;
  bool on_ = true;
  std::optional<std::uint32_t> last_month_;
  std::map<std::uint32_t, std::uint32_t> lots_;
  std::map<std::uint32_t, std::uint32_t> would_borrow_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_SALT_BUY_RULE_H_
