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
/// sources — stage 5 (rest, trudodni), stage 6 (satiety, variety, needs),
/// project phase 2+ (needs inflation).

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

  Metric component_satiety = 55.0F;  ///< STUB neutral until food (stage 6).

  Metric component_common_cause = 55.0F;  ///< STUB neutral until its sources exist.

  Metric component_needs = 55.0F;  ///< STUB neutral until goods (stage 6+).

  Metric component_rest = 55.0F;  ///< STUB neutral until labor (stage 5).

  // -- food (metrics design §8) --------------------------------------------
  /// Product categories actually consumed this season, one bit per category.
  /// STUB: written by the food system (stage 6).
  std::uint16_t food_variety_mask = 0;

  // -- private plot (household design §1, life-cycle §10) ------------------
  /// Game hours per day left for the private plot after work, road and
  /// sleep. STUB: computed when labor exists (stage 5).
  float household_hours = 0.0F;

  /// Share of the family's effort drifting into the private plot, 0-100.
  /// The central health indicator of the whole economy (life-cycle §10).
  /// STUB until stage 5-6.
  Metric private_plot_share = 0.0F;

  // -- pay (labor-payment design §2) ---------------------------------------
  /// The family's trudodni account, hundredths. Accrual arrives at stage 5.
  TrudodniHundredths trudodni_account = 0;
};

/// @brief The families table type used by WorldState.
using FamilyTable = StateTable<FamilyId, FamilyRow>;

}  // namespace core

#endif  // CORE_COMMON_FAMILY_STATE_H_
