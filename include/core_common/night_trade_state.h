/// @file
/// @brief The night's outings of the quiet trades — who went out, doing what,
/// where and in which hours (crime design §7, §9; "Ночной промысел в ядре
/// Эпохи I — числами"; boss, parcel 346).
/// @threading PARALLEL_READONLY
/// Plain data. Written only by the residents' decisions sub-step (phase 3) on
/// the sim thread: the night's rows are laid down in the hour the trades go
/// out, and yesterday's are dropped then. Read by anyone between steps — the
/// host reads the place and the hours of an outing through this table, the
/// event saying only that it happened. SAVED (save format 39).
///
/// WHY A ROW AND NOT THE EVENT. The host's scene needs who, what, where and
/// when (host 93, №19–21). The event carries who and what; a position has no
/// place in SimEvent, and widening every event of the game by a point for one
/// kind of it would cost every consumer. So the event names the outing and
/// this row describes it, for the moonlit night it belongs to.

#ifndef CORE_COMMON_NIGHT_TRADE_STATE_H_
#define CORE_COMMON_NIGHT_TRADE_STATE_H_

#include <array>
#include <cstdint>
#include <utility>

#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_common/resident_state.h"
#include "core_common/state_table.h"

namespace core {

/// @brief One resident out on his trade tonight.
struct NightOutingRow {
  ResidentId resident;

  NightTrade trade = NightTrade::kNone;

  /// The campaign day the night began on — the moonlit day of the month.
  std::uint32_t day = 0;

  /// Where he is: his own gate for a distiller, a spot of
  /// tables/night_fishing_spots.csv for a net fisher, a square of the old
  /// forest for a hunter. Metres from the map's south-west corner.
  Vec2 position;

  /// The hour he goes out, 0..23, on `day`; and the hour he is back, on the
  /// next day when it is smaller than `hour_out`.
  std::uint8_t hour_out = 0;
  std::uint8_t hour_back = 0;
};

/// @brief The night's outings, in the order they were laid down.
using NightOutingTable = StateTable<NightOutingId, NightOutingRow>;

/// @brief The village's side of the distillers: what they have carried off
/// the kolkhoz stores this calendar month, whether the village has come to
/// complain yet (crime design §7, "Утечка сырья… числами"; boss, parcel
/// 364), and how long it has gone without one. SAVED.
struct NightTheftTally {
  /// Grams of raw material taken since the first day of `month_index`.
  Grams stolen_this_month = 0;

  /// The month the tally counts: year × 12 + month, from the campaign's
  /// calendar. A day of another month starts the tally again.
  std::uint32_t month_index = 0;

  /// 0/1: kStoreLeakComplaint has been raised — once a campaign.
  std::uint8_t complaint_raised = 0;

  // `dry_months` stood here from save 55 to save 59: the months the VILLAGE
  // turned without a distiller. Since 2026-09-18 dryness is a YARD's — «в
  // 1000 м от двора есть самогонщик с сырьём» (register 207) — and lives in
  // FamilyRow::dry_months. A village-wide count beside it would be the same
  // rule with two answers.

  /// 0/1: on some night of the month counted by `month_index` a whole still
  /// load lay unguarded by the recipe, sugar included (VillageLeakOpen; until
  /// 0.37.334 any grain or potato in an unwatched store set it). The
  /// month's turn reads it — «утечка села закрыта» only if it stayed 0 all
  /// month — and clears it. Save 60.
  std::uint8_t leak_open_this_month = 0;

  /// The supply-month tag (SupplyMonthTag) from which the village has had
  /// fewer distillers than it keeps; 0 while it has its number. A vacancy is
  /// filled `distiller_replace_months` after it if the leak is open that
  /// month, never while it is closed (register 206). Save 60.
  std::uint32_t distiller_short_since = 0;

  /// The settlement's alcoholism: the mean of its men of 16 and over, taken
  /// after each month's turn (crime §6, «Алкоголизм села»; register 207).
  /// The report prints it; crossing 20 or 40 either way is an event. Save 60.
  float settlement_alcoholism = 0.0F;
};

/// @brief The district's store audit (#1568 link 2d; crime §7, §11; shadow.md
/// «Ревизия приходит внезапно, один-два раза в год»; econ's spec of 5 Nov
/// 2026): when it comes next, what the last one found, and whether the
/// mandatory remark has been given before a trigger may be. SAVED (save 207).
/// Written only while `crime_food_theft_on`; all zero with the link off.
struct AuditState {
  /// The absolute day of each year-half's audit, drawn at the half's start;
  /// 0 = not drawn yet. One a half (`audits_per_year` 2).
  std::array<std::uint32_t, 2> next_day = {0, 0};

  /// The last audit's shortage, per mille of the recorded stock it read.
  std::uint16_t shortage_permille = 0;

  /// 0/1: a «Замечание ревизии» has been given in this campaign since the
  /// last write-off - the condition for kAuditTrialDue (a first audit at
  /// 20 % or more gives the remark, never the trigger: spec §6's red flag).
  std::uint8_t remarked = 0;

  /// 0/1: kAuditTrialDue has been raised (once; the condition of «Под суд»).
  std::uint8_t trial_due = 0;

  /// 0/1: the act of acceptance has been signed (#1568 (a); kBooksAccepted):
  /// until then link 2's theft is the former management's, booked «до
  /// приёмки» (YearLedger::stolen_before_acceptance) and never audited.
  std::uint8_t books_accepted = 0;

  /// The absolute day the act was signed (#1568 round 2, R3: the trigger
  /// waits audit_trial_after_act_months from it); 0 before the act.
  std::uint32_t accepted_day = 0;

  /// THE FORMER MANAGEMENT'S THEFT BY MONTH (#1881; game's #1633, the store
  /// report's «до приёмки» line; crime §7). YearLedger::stolen_before_acceptance
  /// is the year's sum and has no month; the report reads the last closed
  /// month. The pair: grams by resource stolen before the act in the month
  /// `before_acceptance_month` - 1 names (the campaign's month: day / 4; 0 =
  /// none booked yet), and in the month before it. NEVER IN A STORE'S
  /// book_window: the district audit reads that, and «до приёмки» is never
  /// audited. Written by food_theft.cpp beside the ledger's line. SAVED (save
  /// 217).
  std::uint32_t before_acceptance_month = 0;
  ResourceAmounts before_acceptance_this_month;
  ResourceAmounts before_acceptance_last_month;
};

/// @brief Brings the pre-acceptance pair up to `month` (the campaign's month,
///        day / kDaysPerMonth): this month's grams become last month's when the
///        month moves on by one, and both clear when it moves on by more.
inline void RollBeforeAcceptance(AuditState& audit, std::uint32_t month) {
  if (audit.before_acceptance_month == month + 1U) {
    return;
  }
  if (audit.before_acceptance_month != 0 && audit.before_acceptance_month == month) {
    audit.before_acceptance_last_month = std::move(audit.before_acceptance_this_month);
  } else {
    audit.before_acceptance_last_month.clear();
  }
  audit.before_acceptance_this_month.clear();
  audit.before_acceptance_month = month + 1U;
}

/// @brief The grams stolen before the act in the month BEFORE `month`, as the
///        pair holds them - read without moving it: empty when nothing was
///        booked in that month.
inline const ResourceAmounts& BeforeAcceptanceInMonthBefore(const AuditState& audit,
                                                            std::uint32_t month) {
  static const ResourceAmounts kNone;
  if (month == 0 || audit.before_acceptance_month == 0) {
    return kNone;
  }
  const std::uint32_t booked = audit.before_acceptance_month - 1U;  // the "this" month
  if (booked == month - 1U) {
    return audit.before_acceptance_this_month;
  }
  if (booked == month) {
    return audit.before_acceptance_last_month;
  }
  return kNone;
}

}  // namespace core

#endif  // CORE_COMMON_NIGHT_TRADE_STATE_H_
