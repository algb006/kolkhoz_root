/// @file
/// @brief The limit book a year (y1-y5) and the lots' order-to-arrival days -
/// the avatar paths' prints (#1417, #1421).
/// @threading SINGLE_THREADED
/// Test-side code, read from the thread that owns the simulation.
///
/// WHY IT EXISTS (econ, econ-boss-1427-avatar-gates [1]): the ex-chairman's
/// carry is gated on the book (carried points per year against the year's
/// points income; carried points spent), the dealer's grain payment on his
/// grain-paid lots per year, and the promoted's path on the lot's order-to-
/// arrival days. Each year's line is read from the CLOSED book at the turn
/// (YearLedger), so a year is whole when printed.
///
/// ORDER-TO-ARRIVAL: a district goods cart first seen on day D with arrive_day A
/// counts A - D days. A cart seen the day it was ordered is the order day; the
/// world keeps no order day on the row, so a cart ordered and seen a day late
/// would read one day short (never, while the tally reads every day).

#ifndef TESTS_RUN_COMMON_AVATAR_BOOK_TALLY_H_
#define TESTS_RUN_COMMON_AVATAR_BOOK_TALLY_H_

#include <array>
#include <cstdint>
#include <ostream>
#include <unordered_set>

#include "core_common/ledger_state.h"
#include "core_common/world_state.h"

namespace run {

class AvatarBookTally {
 public:
  static constexpr std::uint32_t kYears = 5;

  /// @brief Reads the day's new district carts. Call once a day.
  void CountDay(const core::WorldState& world) {
    for (std::uint32_t row = 0; row < world.limit_deliveries.rows.size(); ++row) {
      const std::uint32_t id = world.limit_deliveries.row_ids[row].value;
      if (!seen_.insert(id).second) {
        continue;
      }
      const auto arrive = static_cast<std::int64_t>(world.limit_deliveries.rows[row].arrive_day);
      const std::int64_t days = arrive - static_cast<std::int64_t>(world.calendar.day);
      lot_days_sum_ += days > 0 ? days : 0;
      ++lots_seen_;
    }
  }

  /// @brief Books the closed year. Call once at each year's turn, y1-y5.
  void CloseYear(const core::YearLedger& closed, std::uint32_t year) {
    if (year < 1 || year > kYears) {
      return;
    }
    Year& book = years_[year - 1];
    book.granted = closed.limit_points_granted;
    book.spent = closed.limit_points_spent;
    book.burned = closed.limit_points_burned;
    book.carried = closed.limit_points_carried;
    book.grain_lots = closed.limit_lots_paid_in_grain;
    book.grain_kg = closed.limit_grain_paid / 1000;
  }

  /// @brief The line a village.
  void Print(std::ostream& out, std::uint64_t seed) const {
    out << "population_curve: L1421 seed " << seed;
    for (std::uint32_t year = 0; year < kYears; ++year) {
      const Year& book = years_[year];
      out << " y" << (year + 1) << ' ' << book.granted << '/' << book.spent << '/' << book.burned
          << '/' << book.carried << '/' << book.grain_lots << '/' << book.grain_kg;
    }
    out << " (granted/spent/burned/carried/grain_lots/grain_kg) lots " << lots_seen_
        << " order_to_arrival_days_sum " << lot_days_sum_ << '\n';
  }

 private:
  struct Year {
    std::int32_t granted = 0, spent = 0, burned = 0, carried = 0;
    std::uint32_t grain_lots = 0;
    std::int64_t grain_kg = 0;
  };

  std::array<Year, kYears> years_ = {};
  std::unordered_set<std::uint32_t> seen_;
  std::int64_t lot_days_sum_ = 0;
  std::uint32_t lots_seen_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_AVATAR_BOOK_TALLY_H_
