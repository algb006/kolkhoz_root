/// @file
/// @brief LedgerState — the accountant's yearly book: the FLOWS of the
/// economic year, counted where they happen.
/// @threading SINGLE_THREADED
/// Written only in sequential slots: each counter has exactly one writer,
/// named in the write map below, and that writer is a sequential sub-step
/// (decisions, phase 3) or the events slot (phase 7). Parallel phases never
/// touch the ledger — what they produce is folded into it afterwards, in row
/// order, by the events slot (buffer-law rule 5). Read by anyone between
/// steps; the run harness and the report writer are the intended readers.
///
/// Why this is state at all (stage 7, task F2; manual/68-run-ledger.md).
/// The reconciliation with the balance calculations speaks in flows — tonnes
/// harvested, heads lost to hunger, man-days per kind of work, what was
/// eaten and where it came from — and a flow cannot be recovered from a
/// snapshot: the harvest lands in the same sequential sub-step that feeds
/// the herds and ships the plan, a death and a departure both remove a row.
/// History that the simulation cannot rederive is stored, like
/// FamilyRow::plot_ratio_sum; the difference is that nothing in the
/// simulation ever READS the ledger. It decides nothing, it only remembers.
/// It is saved, copied with the buffers and compared by the determinism
/// check like every other block — a ledger that differs between one worker
/// and three is a violated buffer-law rule somewhere upstream.
///
/// The economic year. `current` accumulates; when the FIRST tick of a new
/// calendar year has run its phases 1-6, the events slot moves `current`
/// into `closed`, stamps the year number and zeroes `current`. The hour-0
/// bookkeeping of the turn — the trudodni burn, the plan delivery, the life
/// expectancy recompute — is closing business of the year that ended, and
/// this timing keeps it in that year's book. The price is that day 0's
/// demography is booked to the previous year: one day of forty-eight,
/// stated here so nobody hunts for it.
///
/// Units. Masses are Grams by ResourceId, exactly like a store or a pantry
/// (quantities.h): integer, so what the fields gave and what the people ate
/// balance to the gram. Areas are hectares, work is game man-days, both
/// float — they are float in the simulation that produces them. Sums over
/// rows run in row order, never in completion order.

#ifndef CORE_COMMON_LEDGER_STATE_H_
#define CORE_COMMON_LEDGER_STATE_H_

#include <array>
#include <cstdint>

#include "core_common/labor_state.h"
#include "core_common/quantities.h"

namespace core {

/// @brief One economic year of flows. Every ResourceAmounts is dense by
/// ResourceId and may be shorter than the resource table (empty = nothing
/// moved yet), exactly as a pantry is.
///
/// WRITE MAP — one writer per block, all sequential:
///   people, satiety ......... core_residents, demography sub-step
///   issued, ration, nets .... core_residents, the family exchange
///   yard_produce ............ core_production, herd day (household herds)
///   plot_harvest, eaten ..... core_world, events slot — FOLDED from the
///                             pantries: at hour 22 the only pantry writer
///                             is the plot, at hour 23 the meal (family
///                             state header, stage-6 write map), so the
///                             pantry difference between `previous` and
///                             `current` at that hour IS that flow
///   land, herds, delivered .. core_production, production decisions
///   labor ................... core_labor, the day close-out; the burn is
///                             written by the exchange at the year turn
///   year, the rotation ...... core_world, events slot
struct YearLedger {
  /// Campaign year these counters belong to, counted from 1. Written when
  /// the book is closed; 0 in `current` and in a `closed` book that was
  /// never closed (the first year of a run).
  std::uint16_t year = 0;

  // -- people ---------------------------------------------------------------
  std::uint32_t births = 0;

  std::uint32_t deaths = 0;

  std::uint32_t arrivals = 0;  ///< Migrants who came to stay.

  std::uint32_t departures = 0;  ///< The outflow: those who left.

  std::uint32_t weddings = 0;

  // -- satiety, the settlement's daily mean -----------------------------------
  /// Sum of the daily settlement mean satiety and the days summed: the
  /// year's mean is the quotient. Same fold that feeds VitalsState; kept
  /// here too so the closed book is self-contained.
  float satiety_day_mean_sum = 0.0F;

  std::uint32_t satiety_days = 0;

  /// The leanest day: the lowest daily settlement mean the year saw.
  float satiety_day_mean_min = kMetricMax;

  /// The most residents under the health-loss threshold on any one day.
  std::uint32_t hungry_at_once_max = 0;

  // -- food: what reached the pantries, and what left them (grams) ---------
  ResourceAmounts issued;  ///< Distribution against trudodni.

  ResourceAmounts ration;  ///< The safety ration handed out below the floor.

  ResourceAmounts nets;  ///< The yards' fish.

  ResourceAmounts yard_produce;  ///< Household herds' milk and eggs.

  ResourceAmounts plot_harvest;  ///< The gardens and the yards' own hay.

  ResourceAmounts eaten;  ///< What the families actually ate.

  // -- land (grams, hectares) ---------------------------------------------
  /// What came off the fields into the stores: grain, potato, flax, the
  /// meadows' hay, straw as the by-product.
  ResourceAmounts harvest;

  ResourceAmounts seed;  ///< What sowing took out of the stores.

  float area_sown_ha = 0.0F;

  float area_harvested_ha = 0.0F;

  float area_lost_ha = 0.0F;  ///< Annuals lost to snow before harvest.

  float area_manured_ha = 0.0F;

  Grams manure_plowed_in = 0;

  // -- kolkhoz herds -------------------------------------------------------
  /// Milk, eggs, wool, manure from the kolkhoz herds into the stores.
  ResourceAmounts herd_produce;

  ResourceAmounts feed;  ///< What the herds ate out of the stores.

  std::uint32_t herd_births = 0;

  std::uint32_t herd_deaths_age = 0;

  std::uint32_t herd_deaths_hunger = 0;

  /// Deliberate removals: surplus males and the autumn pigs.
  std::uint32_t herd_culled = 0;

  /// Underfed heads times days: the year's hunger, in one number.
  float herd_hungry_head_days = 0.0F;

  // -- district ------------------------------------------------------------
  ResourceAmounts delivered;  ///< Shipped against the plan.

  // -- labor ---------------------------------------------------------------
  /// Game man-days delivered, by WorkKind (index = the enum value).
  std::array<float, kWorkKindCount> work_days_by_kind = {};

  TrudodniHundredths trudodni_accrued = 0;

  /// Unspent trudodni burned at the year turn — the turn that CLOSES this
  /// book, see the file header.
  TrudodniHundredths trudodni_burned = 0;

  std::uint32_t walk_offs = 0;  ///< Fatigue walk-offs from an assignment.
};

/// @brief The two books of the world: the year being written and the last
/// one closed. The report writer reads `closed`; nothing reads `current`
/// but the writers that fill it.
struct LedgerState {
  YearLedger current;

  YearLedger closed;
};

}  // namespace core

#endif  // CORE_COMMON_LEDGER_STATE_H_
