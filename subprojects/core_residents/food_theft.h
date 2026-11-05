/// @file
/// @brief #1568 link 2 - theft for one's own table, the book and the district's
/// store audit (crime §7 «Утечка», §11; epochs §8 «расхождение при ревизии»;
/// shadow.md «Ревизия приходит внезапно, один-два раза в год»; econ's spec
/// econ/manual/proposals/crime-links-spec-2026-11-05.md §2).
/// @threading SINGLE_THREADED
/// Runs in the residents' decisions sub-step (phase 3) on the sim thread,
/// beside the night trades (night_trade.h): it writes the kolkhoz units'
/// stock and book_gap, the ledger's `stolen`, `audits`, `audit_remarks` and
/// `written_off`, the district's reputation, WorldState::audit and the step's
/// events, so it can only live in a sequential slot.
///
/// WHAT IT DOES, behind `crime_food_theft_on` (0 = none of it; the canon):
///   * 2a night theft - on the month's moonlit night (the night trades' own),
///     every man 16+ with alcoholism >= food_theft_from_alcoholism within
///     food_theft_reach_m of a store holding grain or potato whose leak is
///     open (StoreLeakClosed false: no sober night watchman, or a drinking
///     storekeeper) takes food_theft_kg_per_thief_month, capped per store a
///     month at its recorded stock x food_theft_store_cap_share_month;
///   * 2b collusion - a storekeeper of alcoholism > collusion_keeper_alcoholism
///     lets out collusion_share_month of his store a month (the church has no
///     storekeeper, so none there). STUB: the spec's «or inclination >= 50» is
///     NOT ENTERED - the resident has no crime inclination; return when it
///     gets a writer;
///   * 2c the book - theft lowers `stock` and raises `book_gap` by the same
///     grams; nothing else writes book_gap;
///   * (a) the act of acceptance (econ's separation proposal, boss [42]) - on
///     audit_book_opens_month of year 1 every book is set to the real stock
///     (kBooksAccepted); before it the theft is «до приёмки»
///     (YearLedger::stolen_before_acceptance) and no audit is held;
///   * 2d/(b) the audit - audits_per_year a year, one in each half on a
///     counter-hash day, reads every store-kind position over its window
///     (UnitRow::book_window, audit_window_months): its losses over its
///     receipts, positions under audit_min_receipts_t of receipts not read;
///     the worst position decides: under audit_remark_share kAuditClean; at
///     or over it kAuditRemark (audit_remark_reputation); at
///     audit_trial_share or over AFTER a remark, kAuditTrialDue (once); a
///     first audit at the trial share gives the remark, never the trigger.
///   * the write-off - kWriteOffShortage clears a store's book_gap into
///     YearLedger::written_off at write_off_reputation and withdraws the remark.
///
/// STUB, named: the recount («пересчитать склад», a clerk's day showing the
/// book against the stock) is the office's display, not built here.

#ifndef CORE_RESIDENTS_FOOD_THEFT_H_
#define CORE_RESIDENTS_FOOD_THEFT_H_

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_common/ids.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace core {

struct NightTradeConfig;

/// @brief Link 2's numbers (world_params.csv; every one econ's STUB of 5 Nov
/// 2026, calibrated by #1566's arm) and the kinds it steals.
struct FoodTheftConfig {
  bool on = false;                            ///< `crime_food_theft_on`
  float thief_from_alcoholism = 40.0F;        ///< `food_theft_from_alcoholism`
  float kg_per_thief_month = 25.0F;           ///< `food_theft_kg_per_thief_month`
  float reach_m = 1000.0F;                    ///< `food_theft_reach_m`
  float store_cap_share_month = 0.02F;        ///< `food_theft_store_cap_share_month`
  float collusion_share_month = 0.02F;        ///< `collusion_share_month`
  float collusion_keeper_alcoholism = 20.0F;  ///< `collusion_keeper_alcoholism`
  std::uint32_t audits_per_year = 2;          ///< `audits_per_year`
  float audit_remark_share = 0.05F;           ///< `audit_remark_share`
  float audit_trial_share = 0.10F;            ///< `audit_trial_share`
  /// The act of acceptance's month of year 1, 0-based (`audit_book_opens_month`,
  /// human 1-based in the table; STUB 10, October).
  std::uint32_t book_opens_month = 9;
  std::uint32_t window_months = 24;       ///< `audit_window_months`
  float min_receipts_t = 2.0F;            ///< `audit_min_receipts_t`
  float audit_remark_reputation = -5.0F;  ///< `audit_remark_reputation`
  float write_off_reputation = -10.0F;    ///< `write_off_reputation`
  /// The food a thief takes, in the order he takes it: the grains and the
  /// potato (resources.csv rows); empty when the tables name none.
  std::vector<ResourceId> food;
};

/// @brief Every world_params key link 2 reads - for the assembly's audit of
/// declared readers (world.cpp), out of the same array the parser indexes.
std::span<const std::string_view> FoodTheftWorldParamKeys();

/// @brief Reads link 2's numbers. Absent rows keep the defaults (StubTables);
/// a malformed one fails with `error`.
/// @return false on a malformed cell.
bool ParseFoodTheftConfig(const ITableSet& tables, FoodTheftConfig& config, std::string& error);

/// @brief The night's theft and the month's collusion (2a, 2b, 2c). Call once
/// a step; it acts on the night trades' moonlit night only, and does nothing
/// at all with `config.on` false.
/// @param night The night trades' config: the moon day, the leak's rule
///        (StoreLeakClosed) and the storekeeper's post.
/// @param life_speedup Biological years a game year (life.csv), for the
///        thief's age.
void RunFoodTheft(const FoodTheftConfig& config,
                  const NightTradeConfig& night,
                  float life_speedup,
                  WorldState& current);

/// @brief The district's audit (2d): draws each year-half's day, and on it
/// reads every store's book and raises its verdict. Call once a step; acts
/// at the day's first step only, and not at all with `config.on` false.
void RunStoreAudit(const FoodTheftConfig& config, WorldState& current);

/// @brief The book window's receipts (#1568 (b)): every rise of a store's
///        stock of a food kind over what the book last saw
///        (UnitRow::book_seen), into this month's `received`. An issue and a
///        receipt between two looks net out - the approximation named in the
///        contract. Call once a step, after the acceptance; does nothing with
///        `config.on` false.
void RunBookReceipts(const FoodTheftConfig& config, WorldState& current);

/// @brief Answers this step's kWriteOffShortage orders (order_state.h).
void TakeWriteOffOrders(const FoodTheftConfig& config, WorldState& current);

}  // namespace core

#endif  // CORE_RESIDENTS_FOOD_THEFT_H_
