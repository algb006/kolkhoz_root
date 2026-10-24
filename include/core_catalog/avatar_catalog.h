/// @file
/// @brief The chairmen New Game offers (tables/avatars.csv; avatars.md
///        «Пути»; #634, #635): one row an avatar, read here and nowhere
///        else — the start world and every door a path changes ask this
///        catalogue.
/// @threading SINGLE_THREADED
/// Read once by each module's config parse; the rows are immutable after.

#ifndef CORE_CATALOG_AVATAR_CATALOG_H_
#define CORE_CATALOG_AVATAR_CATALOG_H_

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_common/ids.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace core {

/// @brief One avatars.csv row. The defaults are the NEUTRAL values: a row's
///        path a key does not have changes nothing, and so does no avatar.
struct AvatarDef {
  std::string key;

  /// The Райком sign as a number (#634): applied once, at genesis.
  float start_reputation = 50.0F;

  /// DORMANT (#634): copied into ChairmanState at genesis; no reader yet.
  float crime_modifier = 0.0F;
  float electrification_threshold_factor = 1.0F;

  // THE PATHS (#635), read LIVE by the doors (boss, core-boss-635-avatar-
  // paths [2] (b)): a balance change moves them in an existing save too.

  /// The first N plan bargains of an epoch cost no reputation (the student:
  /// 3). kTradePlan, district_trip.cpp.
  std::uint8_t free_plan_trades_per_epoch = 0;

  /// The plan bargain is refused for the whole game (the old fighter:
  /// «Торга о плане у него нет»).
  bool plan_trade_closed = false;

  /// A crop swap in the bargain is refused in his first N bargain windows
  /// (the worker: 2, the January of years 2 and 3); ±10 % stays open.
  std::uint8_t crop_swap_closed_bargains = 0;

  /// A day off may not be declared working in years 1..N (the student: 2).
  /// DayOffMayBeDeclaredWorking.
  std::uint8_t day_off_working_closed_years = 0;

  /// The first N cancelled days off of a calendar year carry no price — no
  /// series rest, no satisfaction penalty (the old fighter: 6). rush.cpp.
  std::uint8_t free_cancelled_days_off_per_year = 0;

  /// The workshops' level-1 site's labour days × this (the worker: 0.5).
  float workshops_l1_labor_factor = 1.0F;
};

/// @brief Reads tables/avatars.csv into `avatars`, row order = AvatarId.
/// @return true with an empty vector when the table is absent — whoever
///         needs a row refuses then (ApplyStartAvatar); false with `error`
///         naming the row and the column on a malformed one.
bool ReadAvatars(const ITableSet& tables, std::vector<AvatarDef>& avatars, std::string& error);

/// @brief The row of `avatar`, or the neutral row when it is invalid or past
///        the table (no avatar: every run's and test's world).
const AvatarDef& AvatarOf(std::span<const AvatarDef> avatars, AvatarId avatar);

/// @brief The AvatarId of `key`, invalid when no row carries it.
AvatarId FindAvatar(std::span<const AvatarDef> avatars, std::string_view key);

/// @brief THE PATHS' REMAINDERS, one home for the door and its preview (#635;
///        econ, the contract review §3: «осталось 2 из 3 в эту эпоху»).
/// Free plan bargains this chairman has left in `epoch` — the path's count
/// less what ChairmanState says he used in that epoch.
std::uint8_t FreePlanTradesLeft(const AvatarDef& avatar,
                                const ChairmanState& chairman,
                                Epoch epoch);

/// @brief Cancelled days off without their price left in calendar `year`
///        (1-based) — «осталось 4 из 6 в этом году».
std::uint8_t FreeDaysOffLeft(const AvatarDef& avatar,
                             const ChairmanState& chairman,
                             std::uint16_t year);

}  // namespace core

#endif  // CORE_CATALOG_AVATAR_CATALOG_H_
