/// @file
/// @brief FamilyRow — the per-household state.
/// @threading PARALLEL_READONLY
/// Rows live in WorldState::families under the double-buffer discipline: the
/// family is the unit of parallelism of phases 2 and 5, so a worker owns
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
/// Stage-6 write map (manual/66-food-model.md). The pantry is written from
/// THREE places, and no two of them can overlap in time:
///   * the sequential decisions slot — the monthly distribution, the ration,
///     the nets, and the produce of the family's own herd;
///   * the parallel NEEDS phase, hour 23 — the family eating from its OWN
///     row;
///   * the parallel METRICS phase, hour 22 — the garden's autumn harvest,
///     again into its own row only.
/// The two parallel writers are separated from each other by an hour gate
/// and from the sequential slot by the phase barrier, and each touches only
/// the family row its worker owns. Every other field stage 6 moves — the
/// satiety component, its yearly mean, the variety mask, plot hours and the
/// season accumulators — is written by the metrics or needs phase of that
/// same owning worker, under the buffer law. `household_hours` changes its
/// writer at stage 6: the labor close-out stops writing it, the metrics
/// phase computes it with the plot factors.

#ifndef CORE_COMMON_FAMILY_STATE_H_
#define CORE_COMMON_FAMILY_STATE_H_

#include <cstdint>

#include "core_common/geometry.h"
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

  /// WHAT THE SEASON'S OVERWORK COSTS THE FAMILY, satisfaction points taken
  /// off the aggregate (unit rules §7, time §9; boss seq 103 and 107; save
  /// 65): summed over its members, `rush_satisfaction_per_step_day` × the
  /// step for each day a member worked under an avral, and
  /// `day_off_cancel_satisfaction` for each cancelled day off a member
  /// worked. Written by labor at the day's close; cleared on a season's
  /// first day — «неделя авралов запоминается», for a season (STUB).
  float overwork_penalty = 0.0F;

  // -- food (metrics design §8; stage 6) -----------------------------------
  /// What the family holds at home, dense by ResourceId, grams. Filled by
  /// distribution, the ration, the garden and household-herd produce; drained
  /// by the family eating (needs phase). Empty vector = holds nothing yet.
  ResourceAmounts pantry;

  /// The family's mean satiety over the last year, as a running average.
  ///
  /// A SLOW reading of a fast metric, and the distinction is the point.
  /// Satiety swings by design — it has to, or February could not be told
  /// from September — so a mechanic that asks "is this household hungry?"
  /// as a yes-or-no question must not ask it of today's value. Read daily,
  /// the hunger stop on births (decision 106) closed the whole village's
  /// fertility from July to November every year and halved the settlement's
  /// thirty-three-year curve; read over a year, it says what it means.
  /// Canon since 2026-08-30 (life-cycle design §4).
  ///
  /// Kept as an exponential mean with a one-year time constant rather than a
  /// ring of days: one float instead of forty-eight, and the difference
  /// between them is not a difference a household would notice.
  Metric satiety_year_mean = 70.0F;

  /// Product categories actually eaten this season, one bit per
  /// FoodCategory (core_residents/food_config.h). Written by the needs
  /// phase as the family eats; cleared at season start. Its population
  /// count sets the variety ceiling of the satiety component.
  std::uint16_t food_variety_mask = 0;

  /// 1 once the family has sat down to its first meal, 0 before. Until then
  /// the variety ceiling of the satiety component is not applied: a household
  /// founded today has eaten nothing yet — neither plainly nor variously — and
  /// the ceiling measures what has not happened (boss, parcel 231). Without
  /// this a new family's component read exactly 25.0 until its first meal
  /// (host's measurement, 19 families in 48 days), which a reader takes for
  /// hunger. Set by the needs phase at the meal and never cleared.
  std::uint8_t first_meal_eaten = 0;

  // -- a family without a roof (housing design §20) -------------------------
  /// Where the family's house stood when it was lost — written by the unit
  /// that falls (core_construction Collapse, beside clearing `house`). It is
  /// the plot a tent is pitched on and the address the accountant counts the
  /// road from while the family lives in it.
  Vec2 lost_house_position;

  /// 1 while the family lives in a tent on its old plot: no free house, no
  /// barrack (STUB, the rung is skipped), a warm season (world_params
  /// `tent_from_month`..`tent_to_month`). Cleared when a free house takes it
  /// in; when the cold comes with the family still in a tent, it comes for
  /// the certificate (§20, the fourth rung) — until 2026-09-19 it left.
  std::uint8_t in_tent = 0;

  /// 1 while the family has asked the chairman for the certificate to leave
  /// and has had no answer (housing §20 step 4; «Без подписи председателя
  /// уехать нельзя»), and `asked_day` the day it asked: silence for
  /// `leave_request_answer_days` refuses it. Cleared by the answer. Save 74.
  std::uint8_t asked_to_leave = 0;
  std::uint32_t asked_day = 0;

  /// The house the family is lodged in, refused its certificate: kin's, or
  /// the nearest neighbour's (§20 «подселение»). `house` stays invalid — the
  /// family has no roof of its own and every morning climbs the ladder again:
  /// a free house or a barrack place takes it out. Invalid when not lodged.
  /// Save 74.
  UnitId lodged_in;

  /// WHAT LODGING COSTS (housing §20 «комфорт и довольство обеих семей
  /// сильно вниз»; boss seq 199-200): satisfaction points taken off the
  /// aggregate, `lodging_satisfaction_penalty` while the family is lodged —
  /// or hosts a lodged family — and nought the day that ends. A level and not
  /// a sum over days: satisfaction is recomputed daily. On
  /// satisfaction because the core has no housing comfort and no cold yet
  /// (the design's two multipliers); when they come, the cost moves to them.
  /// Written by the housing ladder at the day's start. Save 74.
  float lodging_penalty = 0.0F;

  /// 1 while the family's `house` is a barrack (housing §9): a roof shared
  /// with other families, with no yard — no garden, no animals of its own
  /// (its herds went to the kolkhoz when it moved in; boss seq 197). Every
  /// morning such a family climbs to a free house before the wedding queue
  /// does. Read by production too (a grown head is not walked to a barrack).
  /// Save 75.
  std::uint8_t in_barrack = 0;

  /// 1 while the family's hunger alarm is lit (kFamilyGoingHungry): lit when
  /// the members' mean satiety is at or below `ration_satiety_threshold`, put
  /// out only when it has risen above the threshold by
  /// `hunger_alarm_clear_margin` (boss, core-host-l1 seq 45). Without the
  /// margin a family the ration holds at the threshold (22 ↔ 26, host seq 44)
  /// lit the alarm every other day — the alarm new each time, the quest on
  /// it hidden and shown daily. Written by the residents' decisions sub-step.
  /// Save 75.
  std::uint8_t hunger_alarm_lit = 0;

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

  /// 0/1: the chairman decided the ration for THIS yard (labor-payment §5,
  /// «для конкретной семьи»; kSetRation with a family). A granted family is
  /// given the minimum ration at the threshold whatever the village-wide
  /// checkbox says (ChairmanState::ration_auto).
  std::uint8_t ration_granted = 0;

  /// Months in a row this yard turned with no supplied distiller within
  /// reach (NearestSuppliedDistiller), stopping at 255; 0 when one was. From
  /// `sober_months_min` its men drink less (the human's word, 2026-09-18:
  /// «Если люди долго не пьют то алкоголизм медленно уменьшается»). It was
  /// the village's until the same day; the reach made it the yard's
  /// (register 207). Save 60.
  std::uint8_t dry_months = 0;

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
