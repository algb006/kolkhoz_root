// Who carried the year (#671's instrument; core_common/hauled_book.h): one
// line a closed year - the grams hauled on foot and by cart, the share on
// foot, the hay carried by hand, and the logs carried by two with their near
// misses. Transport design §2's return condition reads the share on foot in
// year 1 («выше 20 %» - three bands of carry); econ's pair reads hay by hand
// and the log rows. A line of its own: the year lines the scripts read keep
// their shape.
#ifndef KOLKHOZ_TESTS_RUN_COMMON_HAULED_LINE_H_
#define KOLKHOZ_TESTS_RUN_COMMON_HAULED_LINE_H_

#include <cstdint>
#include <iostream>

#include "core_common/ledger_state.h"
#include "core_common/quantities.h"

namespace run {

/// @brief Prints «year N hauled: …» for the closed book `book`.
/// @param hay The hay's resource id; invalid prints the hay as «no hay row».
inline void PrintHauledLine(const core::YearLedger& book, core::ResourceId hay) {
  core::Grams on_foot = 0;
  core::Grams by_cart = 0;
  for (const core::Grams grams : book.hauled_on_foot) {
    on_foot += grams;
  }
  for (const core::Grams grams : book.hauled_by_cart) {
    by_cart += grams;
  }
  const core::Grams all = on_foot + by_cart;
  const double tonne = static_cast<double>(core::kGramsPerKilogram) * 1000.0;
  std::cout << "year " << book.year << " hauled: " << static_cast<double>(all) / tonne
            << " t, on foot " << static_cast<double>(on_foot) / tonne << " t (";
  if (all > 0) {
    std::cout << 100.0 * static_cast<double>(on_foot) / static_cast<double>(all) << " %";
  } else {
    std::cout << "NOTHING HAULED - not measured";
  }
  std::cout << "), hay by hand ";
  if (hay.value == core::kInvalidDefIdValue) {
    std::cout << "(no hay row)";
  } else {
    std::cout << static_cast<double>(core::AmountOf(book.hauled_on_foot, hay)) / tonne << " t of "
              << static_cast<double>(core::AmountOf(book.hauled_on_foot, hay) +
                                     core::AmountOf(book.hauled_by_cart, hay)) /
                     tonne
              << " t";
  }
  std::cout << "; logs by two " << book.log_pair_trips << " trips on " << book.log_pair_days
            << " days, longest " << book.log_pair_worst_leg_m << " m, near misses "
            << book.log_pair_near_miss_days << " days\n";
}

}  // namespace run

#endif  // KOLKHOZ_TESTS_RUN_COMMON_HAULED_LINE_H_
