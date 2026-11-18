// The store report's theft lines (core_common/store_theft.h; #1881).

#include "core_common/store_theft.h"

#include <cstddef>
#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/world_state.h"

namespace core {

StoreTheftView StoreTheftOf(const WorldState& world) {
  StoreTheftView view;
  view.measured = true;
  const auto this_month = static_cast<std::uint32_t>(world.calendar.day / kDaysPerMonth);
  if (this_month == 0) {
    return view;  // no month has closed yet
  }
  view.closed = true;
  view.month = this_month - 1U;
  for (std::size_t row = 0; row < world.units.rows.size(); ++row) {
    const UnitRow& unit = world.units.rows[row];
    // THE RING AS food_theft.cpp KEEPS IT: book_window_month is the month it
    // was last advanced to, plus one (0 = never); the slot of a month lies at
    // month % size while it is within the window behind that month. A month
    // after the last advance had no theft and no receipt: the ring was not
    // touched.
    if (unit.book_window.empty() || unit.book_window_month == 0) {
      continue;
    }
    const std::uint32_t last = unit.book_window_month - 1U;
    if (view.month > last || last - view.month >= unit.book_window.size()) {
      continue;
    }
    const BookMonth& slot = unit.book_window[view.month % unit.book_window.size()];
    for (std::size_t resource = 0; resource < slot.lost.size(); ++resource) {
      if (slot.lost[resource] > 0) {
        view.rows.push_back(StoreTheftRow{.unit = world.units.row_ids[row],
                                          .resource = DefIdFromIndex<ResourceIdTag>(resource),
                                          .grams = slot.lost[resource]});
      }
    }
  }
  view.before_acceptance_grams = BeforeAcceptanceInMonthBefore(world.audit, this_month);
  return view;
}

}  // namespace core
