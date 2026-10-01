/// @file
/// @brief What the exchange's dry count says of each yard — a view, computed
/// when asked, never stored (needs design §6; boss-all-barter-counter-go-
/// 2026-10-01 [26], core-boss-yards-holdings-03778-2026-10-01 [2]).
/// @threading SINGLE_THREADED
/// Plain data, filled between steps on the thread that owns the simulation.
///
/// WHY IT EXISTS. BarterWatch keeps the village's three totals — yards that
/// would give, yards that would take, the equivalent that would change hands
/// — and no instrument could say WHO gives WHAT: the print of 0.37.78 found
/// the totals unmoved by unlike yards and nothing to explain it by. The
/// lines below are the count's own working, by yard and resource.

#ifndef CORE_COMMON_BARTER_VIEW_H_
#define CORE_COMMON_BARTER_VIEW_H_

#include "core_common/ids.h"
#include "core_common/quantities.h"

namespace core {

/// @brief One yard's side of the dry count for one resource. Every amount is
/// grams of the GRAIN EQUIVALENT (grams x kcal_per_gram over the grain's
/// reference), the exchange's own measure — not grams of the resource.
struct BarterYardLine {
  FamilyId family;

  ResourceId resource;

  /// What the yard would bring of it to the counter (the rules of
  /// core_residents/barter.h, «who gives and who takes»).
  Grams offered = 0;

  /// What the yard would ask of it.
  Grams claimed = 0;

  /// What it would hand over of it once the yards have settled — never more
  /// than `offered`.
  Grams would_give = 0;

  /// What it would carry home of it — never more than `claimed`.
  Grams would_take = 0;
};

}  // namespace core

#endif  // CORE_COMMON_BARTER_VIEW_H_
