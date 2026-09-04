/// @file
/// @brief LedgerState — the accountant's yearly book: the FLOWS of the
/// economic year, counted where they happen.
/// @threading SINGLE_THREADED
/// Written only in sequential slots: every writer is named in the write map
/// below, and every one of them is a sequential sub-step (decisions, phase 3)
/// or the events slot (phase 7). Nearly every counter has exactly one; where
/// a counter has two, the map says so and says why the sum stays exact. Parallel phases never
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
#include <vector>

#include "core_common/ids.h"
#include "core_common/labor_state.h"
#include "core_common/quantities.h"

namespace core {

/// @brief One economic year of flows. Every ResourceAmounts is dense by
/// ResourceId and may be shorter than the resource table (empty = nothing
/// moved yet), exactly as a pantry is.
///
/// WRITE MAP — all writers sequential, one per block except where said:
///   people, satiety ......... core_residents, demography sub-step
///   issued, ration, nets .... core_residents, the family exchange
///   no_room ................. every sequential writer that delivers through
///                             the store door and cannot keep the remainder:
///                             production (harvest, herds, straw), construction
///                             (demolition), genesis (a start table that overfills)
///   spoiled ................. TWO WRITERS, and the exactness argument is
///                             written here rather than assumed (task A4):
///                             core_production rots the units' stores at the
///                             day's LAST tick, core_residents rots the
///                             families' larders at the NEXT day's first —
///                             different ticks, both in the sequential
///                             decisions slot, and even sharing one tick the
///                             slot's fixed order (labor, residents,
///                             production) would decide it. The accumulation
///                             is += on an integer, so order could not
///                             change the sum anyway; the reason to write
///                             this down is that the NEXT writer will not
///                             have these three properties by luck
///   yard_produce ............ core_production, herd day (household herds)
///   plot_harvest, eaten ..... core_world, events slot — FOLDED from the
///                             pantries: at hour 22 the only pantry writer
///                             is the plot, at hour 23 the meal (family
///                             state header, stage-6 write map), so the
///                             pantry difference between `previous` and
///                             `current` at that hour IS that flow
///   land, herds, delivered .. core_production, production decisions
///   labor ................... core_labor, the day close-out. The BURN has
///                             TWO writers, and this is the one block that
///                             does: the exchange burns the unspent accounts
///                             at the year turn, and demography burns what a
///                             household took with it when its last member
///                             died with no heir (residents_system.cpp,
///                             DropFamilyIfEmpty). Both are sequential and
///                             their order within the step is fixed —
///                             demography runs before the exchange — and
///                             they touch disjoint families, so the sum is
///                             exact; but a third writer would need the same
///                             argument made again, not assumed
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

  /// What found NO ROOM in the stores and is gone: the store's ceiling is
  /// a refusal at the door (manual/72-storage-and-alarms.md §2), and what
  /// a refused delivery cannot keep — a herd's produce with nowhere to go,
  /// straw, a demolished unit's leftovers, a reaped crop the snow took off
  /// the field — is booked here, so that the year's book still balances
  /// and the report can say how much the missing storage cost. A refusal
  /// that CAN be kept (the harvest waiting on its field, FieldRow::
  /// reaped_grams) is not a loss and is not booked until it becomes one.
  ResourceAmounts no_room;

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

  /// What went bad in a store or a larder over the year, by resource
  /// (task A4; transport design §10). A separate column from `no_room`
  /// because they are different failures with different cures: no_room is
  /// a barn too small, spoiled is a barn too slow. Nothing vanishes without
  /// a line — the same rule that gave no_room its column.
  ResourceAmounts spoiled;

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
/// @brief One year on the office wall: the three curves and nothing else
/// (office design §7, boss parcel core-year-chronicle 2026-09-05).
///
/// WHY A ROW OF THREE AND NOT THE WHOLE CLOSED YEAR. The wall carries three
/// sheets on one axis, and a fourth curve is forbidden there by design — the
/// stack is ranked by eye, and a fourth breaks the comparison of the three.
/// Keeping the entire YearLedger per year would carry the resource columns
/// seventy times over into every save, and would sit there as a standing
/// invitation to draw the fourth line from data that is already handy. Three
/// numbers say what the wall says; a fourth curve, when the design asks for
/// one, arrives as a task and as a field.
struct ChronicleYear {
  /// The year that closed, 1-based, as YearLedger::year carries it.
  std::uint16_t year = 0;

  /// Residents alive at the moment the books rotated.
  std::uint32_t residents = 0;

  /// SOIL FERTILITY OF THE WHOLE FARM AS ONE NUMBER, 0-100 — and one number
  /// about many fields is an assertion, so here is which one it is: the
  /// mean over ARABLE fields WEIGHTED BY AREA, meadows excluded because a
  /// meadow has no fertility to improve or exhaust (land_state.h, LandKind).
  ///
  /// Weighted and not plain: a hundred hectares at 40 beside one hectare at
  /// 90 is not a farm at 65, and the plain mean would let a scrap of good
  /// garden hide the state of the fields. Zero when there is no arable at
  /// all, which is a fact and not a missing value.
  float fertility = 0.0F;

  /// The year's harvest in KILOCALORIES — food off the arable, not mass off
  /// the farm.
  ///
  /// Mass was the first answer and it was wrong, in a way worth keeping
  /// written down. Potatoes yield 9000 kg/ha against rye's 850 and feed
  /// about a quarter as much per kilogram, so a year that swapped rye for
  /// potatoes would have read as a bumper year: a sum of unmixable
  /// quantities is a number somebody decides by and gets wrong.
  ///
  /// Caloric density does two jobs at once. It makes the crops comparable,
  /// and it drops the meadow with no special case — hay and straw feed
  /// animals and have no density here, so they fall out. That is what pairs
  /// this sheet with the fertility one: both are then about the same land,
  /// and their divergence is the reading the wall exists for — FERTILITY
  /// FALLING WHILE THE HARVEST HOLDS MEANS THE LAND IS BEING EATEN.
  ///
  /// THE LIMIT, SAID RATHER THAN DISCOVERED: a crop grown for fibre is
  /// invisible on this curve. Flax exhausts the soil and adds no calories,
  /// so the pair would read "the land is being eaten" for a year in fact
  /// spent on linen. The sheet measures food off the arable, and that is the
  /// whole of what it measures.
  std::int64_t harvest_kcal = 0;
};

/// @brief The wall: one row per closed year, oldest first.
using Chronicle = std::vector<ChronicleYear>;

struct LedgerState {
  YearLedger current;

  YearLedger closed;

  /// Every year that has closed, in order. THE ONE PIECE OF HISTORY THE
  /// SIMULATION CANNOT REDERIVE: `current` and `closed` are two years, and
  /// the wall is fifty. A layer that accumulated it instead would lose it on
  /// load, and the office of a campaign resumed after a year away would show
  /// a single point on the thirtieth year — lying in the comfortable
  /// direction, "the farm has only just started".
  Chronicle chronicle;
};

/// @brief Adds grams under `resource` to a ledger column, growing the dense
/// vector as it goes. The one way to touch a column: a counter written with
/// a bare index would be a counter that stops matching its resource the
/// first time the table is edited.
/// @param column   Dense by ResourceId; may be shorter than the table.
/// @param resource An invalid id or a non-positive amount is a no-op, so a
///                 caller need not guard a resource its tables lack.
inline void AddLedgerAmount(ResourceAmounts& column, ResourceId resource, Grams amount) {
  if (resource.value == kInvalidDefIdValue || amount <= 0) {
    return;
  }
  if (column.size() <= resource.value) {
    column.resize(static_cast<std::size_t>(resource.value) + 1U, 0);
  }
  column[resource.value] += amount;
}

}  // namespace core

#endif  // CORE_COMMON_LEDGER_STATE_H_
