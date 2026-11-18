/// @file
/// @brief The store report's theft lines (#1881; game's #1633; crime §7
/// «Утечка»): what went missing from each store in the last CLOSED month, and
/// the former management's thefts of that month before the act of acceptance.
/// @threading PARALLEL_READONLY
/// Plain data, built between steps by a pure read (ISession::StoreTheftLastMonth).
///
/// THE SOURCES, both summed in the core from the same lines that raise each
/// kFoodStolen with the same grams (food_theft.cpp) - the layer never sums
/// events itself:
///   - after the act, every store's book window (UnitRow::book_window, its
///     month's `lost`) - any standing unit with food by the book is open to
///     link 2's theft: the church and the heap units as a granary; the heaps
///     lying on fields are not units and are not taken from;
///   - before the act, the village-wide pair AuditState::before_acceptance_*
///     («до приёмки», never in a store's book: the audit reads that).

#ifndef CORE_COMMON_STORE_THEFT_VIEW_H_
#define CORE_COMMON_STORE_THEFT_VIEW_H_

#include <cstdint>
#include <vector>

#include "core_common/ids.h"
#include "core_common/quantities.h"

namespace core {

/// @brief One store's loss of one resource in the month.
struct StoreTheftRow {
  UnitId unit;
  ResourceId resource;
  Grams grams = 0;
};

/// @brief The last closed month's theft.
struct StoreTheftView {
  /// False from a bare engine: say «not measured», never «nothing stolen».
  bool measured = false;

  /// The campaign's month the lines are of (day / 4 of its days); meaningless
  /// before the first month has closed (`closed` false).
  std::uint32_t month = 0;

  /// False on the campaign's first month: no month has closed yet.
  bool closed = false;

  /// One row per store × resource with grams > 0, by unit row then resource.
  /// Empty with `measured` true: nothing went missing from a store.
  std::vector<StoreTheftRow> rows;

  /// Grams by resource stolen before the act of acceptance in that month,
  /// village-wide («до приёмки»); empty when none.
  ResourceAmounts before_acceptance_grams;
};

}  // namespace core

#endif  // CORE_COMMON_STORE_THEFT_VIEW_H_
