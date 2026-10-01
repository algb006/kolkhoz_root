/// @file
/// @brief The yards' exchange at the barter counter — who walks to it this
/// evening, what the day's settlement moved, and whether the village has
/// reached «жителям есть что менять» (needs design §6, «Эпоха I — место
/// обмена»; the human's «По рынку делай», 2026-10-01;
/// boss-all-barter-counter-go-2026-10-01 [1]-[7]; econ's
/// barter-counter-2026-10-01).
/// @threading PARALLEL_READONLY
/// Plain data. Written only by the residents' decisions sub-step (phase 3) on
/// the sim thread: the dry count once a day, the evening's trips in the hour
/// they go out (yesterday's are dropped then), their settlement in the
/// counter's hour. Read by anyone between steps — the graphics layer stands
/// the walkers at the counter by the trips, the host reads who exchanged.
/// SAVED (save format 121).
///
/// WHAT THE EXCHANGE IS, so the fields below read by themselves. Yards
/// exchange FOOD of their pantries (FamilyRow::pantry) for food, equal for
/// equal in the grain equivalent (kilocalories over the grain's; the
/// distribution's measure, no second home). No money, no stock at the
/// counter: one settlement an evening between the yards that came, by
/// shares, and what found no pair goes home. The kolkhoz stores take no
/// part. A yard carries off as much equivalent as it brought, so the
/// village's food does not change by a gram at the counter.
///
/// WHY ROWS AND NOT THE EVENT (host's condition, host-boss-fact-raisers
/// [22]): one kBarterDay a day for a counter names the day's exchange, and
/// «who, with how much» has no place in a SimEvent — a trip has a resident, a
/// yard, a counter and two amounts. So the event names the day and these rows
/// describe it, as NightOutingRow does for the quiet trades.

#ifndef CORE_COMMON_BARTER_STATE_H_
#define CORE_COMMON_BARTER_STATE_H_

#include <cstdint>

#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_common/state_table.h"

namespace core {

/// @brief One yard's walk to the counter this evening: one free adult of the
/// yard, out after the working day, at the counter in its hour, home before
/// sleep.
struct BarterTripRow {
  /// Who walks.
  ResidentId resident;

  /// The yard whose pantry he carries from and to.
  FamilyId family;

  /// The counter he walks to: a standing `barter_place`, the nearest within
  /// the walk's limit.
  UnitId counter;

  /// The campaign day of the evening.
  std::uint32_t day = 0;

  /// The hour he leaves the yard, the hour of the settlement at the counter
  /// and the hour he is home, 0..23, all on `day`, in this order. Between
  /// `hour_out` and `hour_at`, and between `hour_at` and `hour_back`, he is
  /// on the road; at `hour_at` he stands at the counter.
  std::uint8_t hour_out = 0;
  std::uint8_t hour_at = 0;
  std::uint8_t hour_back = 0;

  /// What the yard handed over and what it carried home at the settlement,
  /// grams of the grain equivalent; both 0 until `hour_at` has passed, and
  /// equal after it within the grams' rounding (the exchange's invariant).
  /// By resource it is the year's book, YearLedger::bartered.
  Grams given_equivalent = 0;
  Grams taken_equivalent = 0;
};

/// @brief This evening's trips, in the order they were laid down.
using BarterTripTable = StateTable<BarterTripId, BarterTripRow>;

/// @brief The village's side of the exchange: the dry count that raises
/// «жителям есть что менять», and whether it has been raised. SAVED.
///
/// THE DRY COUNT is the exchange's own calculation run once a day with no
/// counter and no road: what the yards WOULD exchange today if a counter
/// stood at every gate. It moves nothing. The fact is raised on the day the
/// three counters below have stood at or above their thresholds
/// (world_params `barter_fact_*`) for `barter_fact_days_in_row` days running,
/// once a campaign, and never lowered.
struct BarterWatch {
  /// 0/1: kBarterWorthStarting has been raised — once a campaign. A load
  /// after the fact does not raise it again; the world answers «raised?»
  /// by this byte.
  std::uint8_t worth_starting_raised = 0;

  /// Days in a row the dry count has met all three thresholds, today's
  /// included; 0 on a day it did not.
  std::uint16_t dry_days_in_row = 0;

  /// The last dry count's three counters: yards that would hand something
  /// over, yards that would take something, and the grams of the grain
  /// equivalent that would change hands. Kept so a run can print the day
  /// the fact rose, or which threshold holds it back.
  std::uint16_t dry_givers = 0;
  std::uint16_t dry_takers = 0;
  Grams dry_equivalent = 0;
};

}  // namespace core

#endif  // CORE_COMMON_BARTER_STATE_H_
