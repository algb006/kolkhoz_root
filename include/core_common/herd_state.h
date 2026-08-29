/// @file
/// @brief HerdRow — the per-herd state: kind, place, headcount by age rung.
/// @threading PARALLEL_READONLY
/// Rows live in WorldState::herds under the double-buffer discipline;
/// headcount changes (births, deaths, transfers) happen only in sequential
/// phases. Phase 1 keeps herd sizes static (offspring and slaughter are a
/// stage-6/feeding concern; horse offspring is additionally blocked until a
/// stable exists — the capacity rule of the start rework).
///
/// Design sources: livestock design §6 and the mobs parcel: the age ladder
/// is 1/2/3 (wild/poultry/cattle), ONLY ADULTS produce — milk, wool, eggs,
/// draught and manure are computed from adult_count; no growth curves or
/// per-age feed norms exist by design. Disease is a bare STUB degree
/// (0 = healthy, 1 vulnerable, 2 obvious, 3 down): the mechanics are an
/// open design topic and must not be invented here.
///
/// A herd stands either at a unit (the stock-yard's cows) or at a family's
/// yard (the start keeps all 16 kolkhoz horses in private yards until the
/// kolkhoz yard is built) — exactly one of `unit`/`household` is valid.

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

  /// STUB: disease degree 0-3. The field exists so saves and interfaces are
  /// final; no mechanics reads or writes it in phase 1.
  std::uint8_t disease_stage = 0;
};

/// @brief The herds table type used by WorldState.
using HerdTable = StateTable<HerdId, HerdRow>;

}  // namespace core

#endif  // CORE_COMMON_HERD_STATE_H_
