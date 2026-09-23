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
///     granary is kept by the food yard's — and never out of the sealed
///     funds (boss seq 18) — and it leaves the world (boss, parcel 364;
///     StealRawMaterial).
///
/// THE WAY OUT OF A TRADE is kTakeNightTrader (2026-09-18): the man taken
/// keeps no trade from that step, and the year's turn hands it out again.
/// Until then nothing ever cleared a trade, and a trade was kept for life.
///
/// STUB, each with its place: the truancy and the lost rest of the night out
/// wait for the rest door; the chairman's other words to a trader (talk,
/// close his eyes, take him under his wing, hand him to the constable — §7)
/// are later doors, and only the taking is built; a hunter with no
/// old-forest square in his reach does not go out that night.

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

struct FoodConfig;

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
  /// kilograms (`night_distiller_raw_kg`). Boss, parcel 364: 50 kg.
  float distiller_raw_kg = 50.0F;

  /// THE LEAK OF A STORE IS CLOSED OR OPEN, NOT CUT (crime design §7,
  /// register 206, 2026-09-18): closed when a watchman stands at his post
  /// with alcoholism at most this, and every storekeeper of the store too;
  /// a drinking watchman is as good as none. `night_sober_keeper_max`, 20.
  /// It replaced `night_watchman_theft_cut` (a 60 % cut), which is known and
  /// no longer read until the design base drops the row.
  float sober_keeper_max = 20.0F;

  /// «Самогонщик достаёт на 1000 м от своего двора» (crime §6, register
  /// 207): the reach of a supplied distiller for the +2, the purchase and
  /// the sobriety. `samogon_reach_m`.
  float samogon_reach_m = 1000.0F;

  /// A distiller taken or gone is replaced this many months after, if the
  /// village's leak is open that month; never while it is closed, and not at
  /// the year's turn either (register 206). `distiller_replace_months`.
  float distiller_replace_months = 2.0F;

  /// The month's purchase in kind, kilograms, by the drinker's band of the
  /// metric (crime §6, «Самогон стоит семье»): 21–40 «выпивает», 41–60
  /// «злоупотребляет»; 0–20 buys nothing. `samogon_buy_kg_drinks`,
  /// `samogon_buy_kg_abuses`. Boss's numbers, not measured.
  float buy_kg_drinks = 3.0F;
  float buy_kg_abuses = 8.0F;

  /// Lights-out for the evening sale at the gate: a sale lands in a random
  /// hour from sunset up to this one. STUB, 23:00 (register 206).
  /// `samogon_lights_out_hour`.
  float lights_out_hour = 23.0F;

  /// professions.csv `storekeeper`: a store with one posted keeps its leak
  /// closed only if he, too, is sober. Invalid when the roster has none.
  ProfessionId storekeeper_post;

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

/// @brief The trades still missing are handed out by the rules above, by lot
///        from the world's stream. Residents who already keep one keep it.
/// @param life_speedup LifeConfig::life_speedup, for the biological age.
/// @param with_distillers True at the start only. After it a distiller is
///        replaced from the leak, month by month (TurnNightTheftMonth), and
///        never at the year's turn (register 206: «и на переломе тоже»).
void AssignNightTrades(const NightTradeConfig& config,
                       float life_speedup,
                       WorldState& current,
                       bool with_distillers);

/// @brief Is some store holding grain or potato standing with its leak open
///        (StoreLeakClosed false)?
bool VillageLeakOpen(const NightTradeConfig& config, const WorldState& current);

/// @brief The month's turn of the stores' leak (register 206), for the month
///        that closed: kStoreLeakClosedDryMonth when its leak stayed closed
///        every night and no distiller was supplied in it; a missing
///        distiller replaced `distiller_replace_months` after the vacancy
///        was seen, in a month whose leak was open, never while it stays
///        closed; the month's leak flag cleared.
/// @pre Called on the first day of a month, before TurnAlcoholismMonth.
void TurnNightTheftMonth(const NightTradeConfig& config, float life_speedup, WorldState& current);

/// @brief The hour out of a moonlit night: yesterday's outings are dropped;
///        every keeper whose night it is goes out — a row, a
///        kNightTradeOuting and the catch in his yard's pantry. Does nothing
///        at any other hour or on any other night.
/// @param food Sizes the sealed funds the distiller stays above
///        (SealedFunds), asked once a night and only when one goes out.
/// @pre Called once per tick of the decisions slot.
void RunNightOutings(const NightTradeConfig& config, const FoodConfig& food, WorldState& current);

/// @brief Where a resident's yard stands: his family's house, or where it
///        stood if it fell. False when he has no family row.
bool YardOf(const WorldState& current, const ResidentRow& person, Vec2& yard);

/// @brief The month tag a supplied distiller carries for the calendar month
///        of `day` (year × 12 + month, plus one; nought means never), the
///        same tag StealRawMaterial writes.
std::uint32_t SupplyMonthTag(SimDay day);

/// @brief The row of the NEAREST distiller supplied in the month `tag`
///        whose yard is within `samogon_reach_m` of `yard` (crime §6,
///        register 207: «самогонщик достаёт на 1000 м от своего двора»), or
///        kNoRow. «Есть самогон» is this, asked of a yard.
std::uint32_t NearestSuppliedDistiller(const NightTradeConfig& config,
                                       const WorldState& current,
                                       Vec2 yard,
                                       std::uint32_t tag);

/// @brief Is the leak of the store at `unit_row` CLOSED (crime design §7,
///        register 206)? A watchman at his post — at the unit or at its
///        parent — with alcoholism at most `sober_keeper_max`, and every
///        storekeeper posted there sober too. A drinking watchman is as good
///        as none; a store with no watchman is open.
bool StoreLeakClosed(const NightTradeConfig& config,
                     const WorldState& current,
                     std::uint32_t unit_row);

/// @brief A distiller's night at the stores: up to `distiller_raw_kg` of the
///        raw material, unreserved, unit by unit in row order and resource by
///        resource in the config's order, from every store whose leak is OPEN
///        (StoreLeakClosed) — a closed store gives nothing, and he goes on to
///        the next — and NEVER INTO THE SEALED FUNDS: of each raw material no
///        more than the village's unreserved stock above `sealed` (boss
///        seq 18; SealedFunds). What is taken leaves the world and is booked
///        as `stolen`;
///        the month's tally grows, and at `store_leak_complaint_kg` the
///        complaint is raised, once a campaign (the constable's post is a
///        STUB: Epoch I has none). When anything was taken, the distiller at
///        `distiller_row` is SUPPLIED this month
///        (ResidentRow::distiller_supplied_month).
/// @param sealed Dense by ResourceId; a resource past its end is not sealed.
/// @return Grams taken.
Grams StealRawMaterial(const NightTradeConfig& config,
                       std::span<const Grams> sealed,
                       WorldState& current,
                       std::uint32_t distiller_row);

/// @brief Settles every pending kTakeNightTrader: the man named keeps no
///        trade from now on, and his row among tonight's outings, if he is
///        out, is dropped — a poacher taken brings no catch home (crime §9,
///        «взятый браконьер перестаёт таскать рыбу»). Refused kNoSuchSubject
///        for no such resident and kNotEligible for one who keeps no trade.
/// @pre Called once per step in the decisions slot, before RunNightOutings,
///      so that an order read in the hour back is settled before the catch
///      is carried home.
void ConsumeNightTradeOrders(WorldState& current);

}  // namespace core

#endif  // CORE_RESIDENTS_NIGHT_TRADE_H_
