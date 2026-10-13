/// @file
/// @brief SupplyEstimate — how long the carting of a works' materials would
///        take, said BEFORE the works are started (routing stage V2).
/// @threading PARALLEL_READONLY
/// Plain data, answered between steps off the completed state; nobody writes
/// it into the world.
///
/// Why it exists: since 0.37.204 a started site waits for its recipe to be
/// carted from the stores, and a site far from them waits long — a wait the
/// player could not foresee (econ, the C2 thread [3] H; boss [4], [6]:
/// «площадка, которая будет ждать, говорит об этом ДО старта»). The layer
/// shows it beside the start: «подвоз ≈ N суток одной подводой», and
/// «подвоз пешком ≈ N суток» while the farm has no horse to give — the two
/// must not be confusable, so the kind rides with the number.

#ifndef CORE_COMMON_SUPPLY_ESTIMATE_H_
#define CORE_COMMON_SUPPLY_ESTIMATE_H_

#include "core_common/geometry.h"
#include "core_common/quantities.h"

namespace core {

/// @brief What the works on a unit would have carted, and at what price.
struct SupplyEstimate {
  /// The days ONE cart would take to bring it all at today's road and bed —
  /// the supply seam's own unit (ConstructionState::supply_days_written). It
  /// is not a date: the groom gives a works as many carts as the day's other
  /// tasks leave, and the core does not know that number before the morning.
  /// When `on_foot`, the days of ONE CARRIER ON FOOT instead.
  float cart_days = 0.0F;

  /// No draught horse is free in the farm today: the estimate is a carrier's.
  bool on_foot = false;

  /// Where it would be carted from: whoever gives the most of it — a store,
  /// or the stand or pit it lies felled or dug at (the direct leg). The price
  /// counts ALL of it from this one place by the cart that keeps to the
  /// roads: an estimate — the works' own legs, a sender each, are written
  /// when the materials are fixed. Meaningless while `grams` is 0.
  Vec2 sender;

  /// What would be carted: the recipe's lines less what already lies on the
  /// unit, as far as the stores hold them today. 0 — nothing to cart (the
  /// recipe is on the unit, a marking class, no such unit, or no recipe line
  /// is in any store), and `cart_days` is 0 with it.
  Grams grams = 0;

  /// `grams` BY RESOURCE, dense by ResourceId (#671, 0.37.240): a carrier on
  /// foot takes one resource a trip and each by its own form's trip - a
  /// bundle of straw is half a sack, a log goes by two - so the price on foot
  /// is the sum over these (transport design §2, «Смешанное снабжение
  /// стройки»), the works' own seam's arithmetic. A cart mixes and reads only
  /// `grams`. Empty from a caller that does not know them: priced by the sack.
  ResourceAmounts by_resource;
};

}  // namespace core

#endif  // CORE_COMMON_SUPPLY_ESTIMATE_H_
