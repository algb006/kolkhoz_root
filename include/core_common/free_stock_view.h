/// @file
/// @brief The village's free food over its funds, in days of its need (ISession::FreeStock;
///        boss, 22 Nov 2026): the one ready number the bots' «Стол для района» rule compares
///        against its two weeks, so the arithmetic has one home.
///
/// Off the issue's own door (IssueNorms' free_grams, the stock over the funds that the
/// distribution may share) in grain equivalent, over the village's need of one day (members x
/// food.csv adult_kg_grain_eq_per_year ÷ the year's days, children counted as adults - FAM1953's
/// ground). A substitute's stock is counted in its category too, so the free sum is an UPPER
/// bound (issue_norm_view.h).
/// @threading SINGLE_THREADED
/// A value type, built between steps on the sim thread.

#ifndef CORE_COMMON_FREE_STOCK_VIEW_H_
#define CORE_COMMON_FREE_STOCK_VIEW_H_

#include <vector>

#include "core_common/ids.h"
#include "core_common/quantities.h"

namespace core {

/// @brief One food position's free stock over the funds (IssueNormLine::free_grams) and what a
///        gram of it is worth in grain equivalent.
struct FreeStockPosition {
  ResourceId resource;
  Grams free_grams = 0;
  float grain_eq_per_gram = 0.0F;
};

/// @brief The free stock over the funds, in grams and in days of the village's need.
struct FreeStockView {
  /// false when nothing can be said: no food roster, or nobody to feed.
  bool measured = false;
  Grams free_grain_eq_grams = 0;  ///< the free stock over the funds, grain equivalent
  Grams day_need_grams = 0;       ///< the village's need of one day, grain equivalent
  float days = 0.0F;              ///< free_grain_eq_grams / day_need_grams
  /// By position, so a taker («Стол для района») takes each within its free grams and never
  /// a fund's.
  std::vector<FreeStockPosition> positions;
};

}  // namespace core

#endif  // CORE_COMMON_FREE_STOCK_VIEW_H_
