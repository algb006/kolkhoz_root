/// @file
/// @brief FamilyRow — the per-household state.
/// @threading PARALLEL_READONLY
/// Rows live in WorldState::families under the double-buffer discipline: the
/// family is the unit of parallelism of phases 2 and 6, so a worker owns
/// whole family rows; structural changes (marriages, the last member dying
/// or leaving) happen only in the sequential demography sub-step.
///
/// Design sources: metrics design §7-§11 (satisfaction is computed PER
/// FAMILY from four components with epoch weights), §8 (food variety),
/// labor-payment §2 (the trudodni account is the family's), household
/// design §1 (time for the private plot). A single person or a visiting
/// specialist is a family of one.
///
/// The family "rating in the village" (metrics §16) is derived and never
/// stored. Grandparent status is derived from kinship, not stored.
///
/// Stage plan: stage 3 lays everything out and computes satisfaction from
/// the components; the components themselves start moving with their
/// sources — stage 5 (rest, trudodni), stage 6 (pantry, satiety, variety,
/// plot time), project phase 2+ (needs inflation).
///
/// Stage-6 write map (manual/66-food-model.md): the pantry is written from
/// two places that never overlap in time — the sequential decisions slot
/// (distribution, ration, household-herd produce, the garden harvest) and
/// the parallel needs phase (the family eating from its OWN row). Every
/// other field stage 6 moves — the satiety component, the variety mask,
/// plot hours and its season accumulators — is written only by the metrics
/// or needs phase of the worker owning the family row, under the buffer
/// law. `household_hours` changes its writer at stage 6: the labor close-out
/// stops writing it, the metrics phase computes it with the plot factors.

#ifndef CORE_COMMON_FAMILY_STATE_H_
#define CORE_COMMON_FAMILY_STATE_H_

#include <cstdint>

#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_common/state_table.h"

namespace core {

/// @brief One household. Plain data.
struct FamilyRow {
  /// The family's house. STUB: stays invalid until units exist (stage 4);
  /// the one-family-one-house law (families design §1) is enforced then.
  UnitId house;

  // -- satisfaction and its four components (metrics design §7) ------------
  /// The aggregate, 0-100: components weighted by the epoch, capped by the
  /// low-component law (any component below 20 caps the total at 2x itself).
  Metric satisfaction = 55.0F;

  /// Moves at stage 6: mean member satiety under the variety ceiling
  /// (metrics design §8; manual/66-food-model.md §4).
  Metric component_satiety = 55.0F;

  Metric component_common_cause = 55.0F;  ///< STUB neutral until its sources exist.

  Metric component_needs = 55.0F;  ///< STUB neutral until goods (project phase 2).

  Metric component_rest = 55.0F;  ///< Moves since stage 5 (mean member rest).

  // -- food (metrics design §8; stage 6) -----------------------------------
  /// What the family holds at home, dense by ResourceId, grams. Filled by
  /// distribution, the ration, the garden and household-herd produce; drained
  /// by the family eating (needs phase). Empty vector = holds nothing yet.
  ResourceAmounts pantry;

  /// Product categories actually eaten this season, one bit per
  /// FoodCategory (core_residents/food_config.h). Written by the needs
  /// phase as the family eats; cleared at season start. Its population
  /// count sets the variety ceiling of the satiety component.
  std::uint16_t food_variety_mask = 0;

  // -- private plot (household design §1, life-cycle §10) ------------------
  /// Game hours per day the household has for its plot: the working
  /// members' remainder of the day plus the additive factors of household
  /// design §1 (elders, schoolchildren, a drinker, sickness). Written daily
  /// by the metrics phase from stage 6 on (stage 5 wrote the bare remainder
  /// from the labor close-out; that writer is retired by stage 6).
  float household_hours = 0.0F;

  /// Garden-yield bookkeeping across the growing season: the sum of the
  /// daily yield ratio min(1, household_hours / full-yield hours) and the
  /// number of days summed. The autumn garden harvest scales the yard's
  /// yearly yield by the season mean, then both reset. Stored because it is
  /// history, not derivable from the current day.
  float plot_ratio_sum = 0.0F;

  std::uint16_t plot_ratio_days = 0;

  /// Share of the family's effort drifting into the private plot, 0-100.
  /// The central health indicator of the whole economy (life-cycle §10).
  /// STUB: no drift arithmetic exists in the design yet (phase 2).
  Metric private_plot_share = 0.0F;

  // -- pay (labor-payment design §2-§3) ------------------------------------
  /// The family's trudodni account, hundredths. Accrues since stage 5.
  TrudodniHundredths trudodni_account = 0;

  /// How much of the account the monthly distribution has already covered
  /// with goods, hundredths. Issue covers account - redeemed; both burn to
  /// zero at the economic year's turn (labor-payment design §3).
  TrudodniHundredths trudodni_redeemed = 0;
};

/// @brief The families table type used by WorldState.
using FamilyTable = StateTable<FamilyId, FamilyRow>;

}  // namespace core

#endif  // CORE_COMMON_FAMILY_STATE_H_
