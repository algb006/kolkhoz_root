/// @file
/// @brief Reads the store report's theft lines off a world (#1881;
///        store_theft_view.h).
/// @threading PARALLEL_READONLY
/// A pure function of the world.

#ifndef CORE_COMMON_STORE_THEFT_H_
#define CORE_COMMON_STORE_THEFT_H_

#include "core_common/store_theft_view.h"

namespace core {

struct WorldState;

/// @brief The last closed month's theft on `world`: each store's book window
///        slot of that month (UnitRow::book_window, as food_theft.cpp keeps it:
///        slot `month % size`, valid while the month lies within the window
///        behind `book_window_month` - 1) and the pre-acceptance pair
///        (AuditState::before_acceptance_*).
/// @return `measured` true; `closed` false on the campaign's first month.
StoreTheftView StoreTheftOf(const WorldState& world);

}  // namespace core

#endif  // CORE_COMMON_STORE_THEFT_H_
