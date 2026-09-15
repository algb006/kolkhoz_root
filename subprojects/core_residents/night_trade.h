/// @file
/// @brief The quiet night trades of Epoch I — the distiller, the pair of net
/// fishers, the hunter: who keeps one, which nights they go out, where to and
/// what they bring home (crime design §7, §9; "Ночной промысел в ядре Эпохи I
/// — числами"; boss, parcel 346).
/// @threading SINGLE_THREADED
/// Runs in the residents' decisions sub-step (phase 3) on the sim thread: the
/// trades are handed out at the year's turn, the outings laid down in their
/// hour. It writes ResidentRow::night_trade, the night's outings, the family
/// pantries, the kolkhoz units' stock and the month's theft tally
/// (WorldState::night_theft), the ledger's `stolen` and the step's events, so
/// it can only live in a sequential slot.
///
/// WHAT DECIDES, in boss's numbers (assigned, not measured):
///   * who — at the year's turn the missing ones are chosen from the adult men
///     OUTSIDE the party and the komsomol (the crime metrics are 0 for all
///     yet, and membership is the sign of the clean): distillers up to 2, by
///     lot weighted by alcoholism (equal while it is 0); a pair of net
///     fishers 18–50 from two different yards; one hunter 25–60. One who
///     leaves the village is not replaced before the next turn;
///   * when — only the moonlit night, the third day of the month; the net
///     fishers only when the day's mean is +18 or warmer; every such night;
///   * the hours — out at 23, back by 3; the event is raised in the hour out;
///   * where — the distiller at his own gate (his yard); the hunter at a
///     square of the old forest 800–2500 m from his yard, by lot; the net
///     fishers at a spot of tables/night_fishing_spots.csv, by lot, both at
///     the same one;
///   * what it brings — the net fishers 3 kg of fish between them into their
///     yards' pantries; the hunter 5 kg of meat with a chance of 0.3; the
///     distiller carries 50 kg of raw material off the kolkhoz stores, less
///     60 % from a unit whose watchman is at his post — or whose parent's, as a
///     granary is kept by the food yard's — and it leaves the world
///     (boss, parcel 364; StealRawMaterial).
///
/// STUB, each with its place: the truancy and the lost rest of the night out
/// wait for the rest door; the chairman's orders against a trade (talk, take
/// the still, hand over to the constable) are a later door, and until then a
/// trade is kept for life; a hunter with no old-forest square in his reach
/// does not go out that night.

#ifndef CORE_RESIDENTS_NIGHT_TRADE_H_
#define CORE_RESIDENTS_NIGHT_TRADE_H_

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/post_shift.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace core {

/// @brief The night trades' numbers (world_params.csv) and the fishing spots
/// (tables/night_fishing_spots.csv). Defaults are boss's figures of parcel
/// 346, kept for a world with no tables; with no spots nobody nets.
struct NightTradeConfig {
  std::uint32_t distillers_max = 2;        ///< `night_distillers_max`
  float fisher_age_from_years = 18.0F;     ///< `night_fisher_age_from_years`
  float fisher_age_to_years = 50.0F;       ///< `night_fisher_age_to_years`
  float hunter_age_from_years = 25.0F;     ///< `night_hunter_age_from_years`
  float hunter_age_to_years = 60.0F;       ///< `night_hunter_age_to_years`
  std::uint8_t moon_day_in_month = 2;      ///< `night_moon_day_in_month`, 0-based
  std::uint8_t hour_out = 23;              ///< `night_trade_hour_out`
  std::uint8_t hour_back = 3;              ///< `night_trade_hour_back`
  float fishing_min_mean_celsius = 18.0F;  ///< `night_fishing_min_mean_celsius`
  float hunt_reach_min_m = 800.0F;         ///< `night_hunt_reach_min_m`
  float hunt_reach_max_m = 2500.0F;        ///< `night_hunt_reach_max_m`
  float fishing_catch_kg = 3.0F;           ///< `night_fishing_catch_kg`, both together
  float hunt_catch_kg = 5.0F;              ///< `night_hunt_catch_kg`
  float hunt_success_chance = 0.3F;        ///< `night_hunt_success_chance`

  /// Raw material a distiller carries off the kolkhoz stores on his night,
  /// kilograms (`night_distiller_raw_kg`), and the share a watchman at his
  /// post cuts from what is taken out of the unit he keeps
  /// (`night_watchman_theft_cut`). Boss, parcel 364: 50 kg, 60 %.
  float distiller_raw_kg = 50.0F;
  float watchman_theft_cut = 0.6F;

  /// The month's loss at which the village complains (`store_leak_complaint_kg`).
  float store_leak_complaint_kg = 100.0F;

  /// The raw material, in the order it is taken: grain (rye, wheat, barley,
  /// oat), then potato, then sugar — resources.csv rows; an absent one is
  /// skipped.
  std::vector<ResourceId> raw_material;

  /// The posts' shifts by ProfessionId (professions.csv `shift`): a unit is
  /// kept tonight when a holder of a night post stands at it.
  std::vector<PostShift> post_shift;

  /// The resources.csv rows the catch goes in as: `fish` and `meat`.
  ResourceId fish;
  ResourceId meat;

  /// The spots of night_fishing_spots.csv, in table order.
  std::vector<Vec2> fishing_spots;
};

/// @brief The world_params.csv keys this file reads, for the assembly's
/// declared-readers check.
std::span<const std::string_view> NightTradeWorldParamKeys();

/// @brief Reads the knobs, the fishing spots and the two resources.
/// @return false with `error` naming the table, row or key for a value out of
///         range — a moon day past the month, an hour past 23, an age band
///         upside down, a chance outside 0..1, a spot off the map's numbers.
bool ParseNightTradeConfig(const ITableSet& tables, NightTradeConfig& config, std::string& error);

/// @brief Whether `day` is the moonlit night of its month.
bool IsMoonlitNight(const NightTradeConfig& config, SimDay day);

/// @brief The year's turn: the trades still missing are handed out by the
///        rules above, by lot from the world's stream. Residents who already
///        keep one keep it.
/// @param life_speedup LifeConfig::life_speedup, for the biological age.
void AssignNightTrades(const NightTradeConfig& config, float life_speedup, WorldState& current);

/// @brief The hour out of a moonlit night: yesterday's outings are dropped;
///        every keeper whose night it is goes out — a row, a
///        kNightTradeOuting and the catch in his yard's pantry. Does nothing
///        at any other hour or on any other night.
/// @pre Called once per tick of the decisions slot.
void RunNightOutings(const NightTradeConfig& config, WorldState& current);

/// @brief A distiller's night at the stores: up to `distiller_raw_kg` of the
///        raw material, unreserved, unit by unit in row order and resource by
///        resource in the config's order; from a unit kept by a watchman at
///        his post — at the unit or at its parent — a share
///        `watchman_theft_cut` smaller. What is taken leaves
///        the world and is booked as `stolen`; the month's tally grows, and at
///        `store_leak_complaint_kg` the complaint is raised, once a campaign
///        (the constable's post is a STUB: Epoch I has none).
/// @return Grams taken.
Grams StealRawMaterial(const NightTradeConfig& config, WorldState& current);

}  // namespace core

#endif  // CORE_RESIDENTS_NIGHT_TRADE_H_
