/// @file
/// @brief HerdRow — the per-herd state: kind, place, headcount by age rung.
/// @threading PARALLEL_READONLY
/// Rows live in WorldState::herds under the double-buffer discipline;
/// headcount changes (births, maturation, deaths, transfers) happen only in
/// the sequential production decisions sub-step (stage 6; horse offspring
/// is additionally blocked until a stable exists — the capacity rule of the
/// start rework). Only care_days_remaining is written elsewhere: the labor
/// sub-step of the same sequential slot.
///
/// Design sources: livestock design §6 and the mobs parcel: the age ladder
/// is 1/2/3 (wild/poultry/cattle), ONLY ADULTS produce — milk, wool, eggs,
/// draught and manure are computed from adult_count; no growth curves or
/// per-age feed norms exist by design. Disease is a bare STUB degree
/// (0 = healthy, 1 vulnerable, 2 obvious, 3 down): an Era II+ mechanic
/// (design question 99 closed 2026-08-29 — Era I animals never get sick;
/// its cold ladder is freeze -> productivity drop -> death, no disease
/// step). The field stays a STUB for all of phase 1.
///
/// A herd stands either at a unit (the stock-yard's cows) or at a family's
/// yard (the start keeps all 16 kolkhoz horses in private yards until the
/// kolkhoz yard is built) — exactly one of `unit`/`household` is valid.
///
/// Labor seam (stage 5, manual/65-labor-model.md): unit-standing herds are
/// a daily work source — the labor sub-step refills care_days_remaining
/// each morning from the kind's care norm and drains it with assigned barn
/// workers. A household-standing herd generates NO kolkhoz job: its care is
/// the owner's leak (livestock design §5), never assigned labor.

#ifndef CORE_COMMON_HERD_STATE_H_
#define CORE_COMMON_HERD_STATE_H_

#include <cstdint>

#include "core_common/ids.h"
#include "core_common/state_table.h"

namespace core {

/// @brief One herd of one kind in one place. Plain data.
struct HerdRow {
  /// Row of tables/livestock.csv: cow, horse, sheep, pig, chicken, duck.
  LivestockKindId kind;

  /// The unit housing the herd; invalid when it stands at a family yard.
  UnitId unit;

  /// The family yard housing the herd; invalid when it stands at a unit.
  FamilyId household;

  std::uint16_t newborn_count = 0;

  std::uint16_t juvenile_count = 0;

  /// The only count that produces anything (mobs canon).
  std::uint16_t adult_count = 0;

  /// Males among adult_count, for the breeding rule (boss summary 2026-08-29
  /// §2.2: offspring needs an adult male present). Always 0 for sexless
  /// kinds (poultry); genesis and transfers keep it <= adult_count.
  std::uint16_t adult_male_count = 0;

  // -- stage-6 cohort flow (manual/66-food-model.md §6) --------------------
  // The row stores counts, not per-head ages (mobs canon: no per-animal
  // modeling), so aging and births run as deterministic fractional flows:
  // each day the accumulator gains count / rung-duration (or the birth
  // rate x females), and the integer part moves whole heads. Written only
  // in the production decisions sub-step.

  /// Accumulated fractional heads maturing newborn -> juvenile.
  float newborn_progress = 0.0F;

  /// Accumulated fractional heads maturing juvenile -> adult.
  float juvenile_progress = 0.0F;

  /// Accumulated fractional births (adult females x kind birth rate).
  float birth_progress = 0.0F;

  /// Sum of the adult heads' biological ages, years. Maintained by the same
  /// flows (daily aging, +adult-entry age per maturation, -mean per death);
  /// the mean age drives the age-death draw — the "threshold with
  /// randomness" of the boss rules over a count-only cohort.
  float adult_age_years_total = 0.0F;

  /// Consecutive days the herd went underfed (stage 6): produce drops at
  /// once, deaths begin past the config threshold. Reset by a fed day.
  /// The freeze ladder proper (question 99) stays a STUB — every phase-1
  /// herd stands under a roof.
  float unfed_days = 0.0F;

  /// STUB: disease degree 0-3. The field exists so saves and interfaces are
  /// final; no mechanics reads or writes it in phase 1.
  std::uint8_t disease_stage = 0;

  /// Game man-days of barn work left today (stage 5). Refilled every morning
  /// by the labor sub-step from the kind's yearly care norm (real man-days
  /// / 7 / days per year x heads), drained by assigned kHerdCare workers.
  /// Unmet care has no consequence yet — that STUB ties into feeding
  /// (stage 6). Zero for household-standing herds (see @file).
  float care_days_remaining = 0.0F;
};

/// @brief The herds table type used by WorldState.
using HerdTable = StateTable<HerdId, HerdRow>;

}  // namespace core

#endif  // CORE_COMMON_HERD_STATE_H_
