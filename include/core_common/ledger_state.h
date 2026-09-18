/// @file
/// @brief LedgerState — the accountant's yearly book: the FLOWS of the
/// economic year, counted where they happen.
/// @threading SINGLE_THREADED
/// Written only in sequential slots: every writer is named in the write map
/// below, and every one of them is a sequential sub-step (decisions, phase 3)
/// or the events slot (phase 6). Nearly every counter has exactly one; where
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
///   night_catch ............. core_residents, the night trades' return hour
///   stolen .................. core_residents, the night trades' hour out
///   lost_no_room ............ every sequential writer that delivers through
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
///   mechanisation share ..... core_production, the HERD DAY — both halves of
///                             it, numerator and denominator, in one loop at
///                             one hour. It sits under the labor banner
///                             below because it is about work, and it is
///                             named here because the banner would otherwise
///                             say core_labor and be wrong: the denominator
///                             WAS booked there for one afternoon, in a
///                             different unit on the other side of the
///                             year's close
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

  /// What the night trades brought home past the kolkhoz — the poached fish
  /// and game (crime design §9: "идёт мимо колхоза"). Its own line and not
  /// `nets`, so the open fishing and the poaching stay two quantities.
  ResourceAmounts night_catch;

  /// What the distillers carried off the kolkhoz stores — grain, potato,
  /// sugar — and out of the world (crime design §7; boss, parcel 364).
  ResourceAmounts stolen;

  /// What drinkers' families paid distillers' families for samogon, in kind
  /// (crime §6, «Самогон стоит семье»; register 205): a transfer between
  /// pantries, booked so the price of the drink to a family can be read
  /// beside its table. Save 60.
  ResourceAmounts samogon_paid;

  ResourceAmounts yard_produce;  ///< Household herds' milk and eggs.

  ResourceAmounts plot_harvest;  ///< The gardens and the yards' own hay.

  ResourceAmounts eaten;  ///< What the families actually ate.

  // -- land (grams, hectares) ---------------------------------------------
  /// What came off the fields into the stores: grain, potato, flax, the
  /// meadows' hay, straw as the by-product.
  ResourceAmounts harvest;

  /// WHAT IS GONE FOR WANT OF ROOM — and the name carries the "gone",
  /// because the name is what a reader trusts.
  ///
  /// It was called `no_room` until 2026-09-05, and that name cost a
  /// measurement: an oat balance built to close came up 3.778 t short,
  /// because "no room" reads as a state — lying somewhere, waiting — and
  /// every one of the five writers here means a LOSS. Waiting has its own
  /// column and its own word (the harvest on its field, below).
  ///
  /// THE CLASS, worth more than the rename: A COLUMN NAMED AFTER ITS CAUSE
  /// IS READ AS A DESCRIPTION OF A STATE. "No room" says what happened; the
  /// reader hears where the grain is.
  ///
  /// What found no room in the stores and is gone: the store's ceiling is
  /// a refusal at the door (manual/72-storage-and-alarms.md §2), and what
  /// a refused delivery cannot keep — a herd's produce with nowhere to go,
  /// straw, a demolished unit's leftovers, a reaped crop the snow took off
  /// the field — is booked here, so that the year's book still balances
  /// and the report can say how much the missing storage cost. A refusal
  /// that CAN be kept (the harvest waiting on its field, FieldRow::
  /// reaped_grams) is not a loss and is not booked until it becomes one.
  ResourceAmounts lost_no_room;

  /// THE STANDING CROP THE SNOW TOOK (farming design §6, the one total loss
  /// of a harvest): what the field would have given had it been reaped —
  /// the harvest's own yield, fertility, weather and late sowing in it —
  /// booked on the day the snow takes it. Save 61. Until then only the
  /// hectares were written (area_lost_ha) and the reaped heap lying there
  /// (lost_no_room), and host found 150 t of potato a seed in no column at
  /// all (econ-host-lever-pass3 seq 35): nothing vanishes without a line.
  ResourceAmounts lost_to_snow;

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

  /// WHAT THE DISTRICT ASKED, by position, written by the plan's judge at
  /// the turn before it clears the figure (econ's audit M12, Л1; boss, seq
  /// 56). Beside `delivered`, `lost_no_room`, `issued` and `ration` of the
  /// same book it answers the player's question about a failed position:
  /// «сорвали — от склада или от выдачи?». Without it the closed book knew
  /// what was shipped and what was lost, and no longer knew what had been
  /// owed: the figure was cleared at the turn that judged it.
  ResourceAmounts plan_due;

  /// What went bad in a store or a larder over the year, by resource
  /// (task A4; transport design §10). A separate column from `lost_no_room`
  /// because they are different failures with different cures: lost_no_room is
  /// a barn too small, spoiled is a barn too slow. Nothing vanishes without
  /// a line — the same rule that gave lost_no_room its column.
  ResourceAmounts spoiled;

  // -- labor ---------------------------------------------------------------
  /// Game man-days delivered, by WorkKind (index = the enum value).
  std::array<float, kWorkKindCount> work_days_by_kind = {};

  TrudodniHundredths trudodni_accrued = 0;

  /// Unspent trudodni burned at the year turn — the turn that CLOSES this
  /// book, see the file header.
  TrudodniHundredths trudodni_burned = 0;

  std::uint32_t walk_offs = 0;  ///< Fatigue walk-offs from an assignment.

  /// MECHANISATION, AND IT IS MEASURED BY TRACTION RATHER THAN BY ENGINE
  /// (boss's decision of 2026-09-12; epochs design §6). The component is
  /// "доля работ НЕ ВРУЧНУЮ", and a horse mower is not hand work: in Epoch I
  /// the traction is the horse, in Epoch II an engine joins it. Without that
  /// reading the settlement's own progression would be unmeasurable — the
  /// village starts pulling by hand and ends pulling by horse, and that IS
  /// its mechanisation.
  ///
  /// BOTH HALVES ARE ASSIGNMENT-DAYS, counted in ONE loop at ONE hour by ONE
  /// module, and the unit is in the name because the first draft got it
  /// wrong: the numerator counted whole days at hour 0 while the denominator
  /// summed `worked_norm_days_today` at hour 23 — delivered fractions, short
  /// on a winter day, shorter still after a walk-off. A quotient of a count
  /// over a sum of fractions can pass 1 and means nothing on the way there.
  /// Splitting it across two modules made that easy to miss and bought a
  /// second defect free: the two halves were booked on opposite sides of the
  /// year's close, so each book divided 48 days shifted by one against 48
  /// unshifted.
  ///
  /// A day's assignment is the right unit anyway. Mechanisation asks whether
  /// the day's work had traction behind it; how many hours the man got in
  /// before dusk is a different question with its own column above.
  ///
  /// NOT COUNTED OFF `work_days_by_kind` AND `IsHorseWork`, which would have
  /// cost nothing: that predicate is a `constexpr` over the KIND of work and
  /// calls ploughing horse work in a village with no horse at all, so the
  /// share would measure the ROTATION — standing near-still for thirty years
  /// and reading positive at a farm with an empty stable. A quantity with no
  /// subject behind it adds up, compares against a threshold, and lies.
  ///
  /// Written only by the herd day of the production decisions sub-step.
  float horse_backed_assignment_days = 0.0F;

  /// Every assignment-day of the year, the denominator of the share above.
  /// Written in the same loop as the numerator, and that is the whole point:
  /// two halves of one ratio derived in two places drift, and a ratio is the
  /// one shape where drift is invisible — the quotient still looks like a
  /// quotient.
  float total_assignment_days = 0.0F;

  // The district's limit (district design §1; limit_state.h). A point is
  // spent or burnt, never kept, so the year's book is the three flows.
  /// Points granted for this year — on the first tick for the first, at the
  /// turn that opened it for every later one.
  std::int32_t limit_points_granted = 0;
  /// Points spent on lots this year.
  std::int32_t limit_points_spent = 0;
  /// Points left unspent when this year closed and burnt with it.
  std::int32_t limit_points_burned = 0;

  // -- what the era-readiness index needs and nothing else kept ------------
  //
  // FOUR QUANTITIES THE INDEX ASKS OF THE YEAR AND THE YEAR DID NOT KEEP
  // (epochs design §6; boss, parcels 128-132). Each is booked by the module
  // that owns the rule behind it, never by the counter that reads them all:
  // a rule lives with its configuration, and a ledger column filled from
  // somewhere else is the first half of two homes for one number.
  //
  // Appended at the END, because the order of this struct is the wire format
  // of a save.

  /// Sum of every family's satisfaction over every day of the year, and the
  /// number of those readings — the mean over families AND days, which is
  /// what «среднее довольство семей села за год» asks for.
  ///
  /// TWO COLUMNS AND NOT ONE RUNNING MEAN, because the divisor moves: the
  /// village gains and loses families all year, and a mean kept in one float
  /// would weight a January of twenty-one households against a December of
  /// forty as though the two were the same measurement.
  ///
  /// Written by the metrics phase of core_residents.
  float satisfaction_sum = 0.0F;

  std::uint32_t satisfaction_samples = 0;

  /// Person-days of able-bodied life in the village over the year: one for
  /// every working-age resident on every WORKING day, whether or not the
  /// kolkhoz asked anything of them. The denominator of «доля усилий,
  /// отданных колхозу», whose numerator is `total_assignment_days` above.
  ///
  /// WORKING DAYS AND NOT EVERY DAY, or the component could never reach its
  /// own ceiling: a year is 48 days of which about 41 are worked, no
  /// assignment is ever given on a holiday, and a denominator of every day
  /// would cap the share near 85 per cent in every campaign ever played — a
  /// silent 15 per cent off the weight, with nothing to show why. A rule
  /// that cannot fire and a component that cannot fill are the same defect
  /// one storey apart.
  ///
  /// AND IT IS NOT THE DENOMINATOR THE DESIGN NAMES, which is said here
  /// rather than glossed over. Epochs §6 asks for kolkhoz man-days over
  /// kolkhoz PLUS HOUSEHOLD man-days, and this core does not model the
  /// private plot as labour at all: `FamilyRow::household_hours` is the
  /// window a household has left at home once sleep and the road are taken
  /// off, a property of the FAMILY'S DAY and not a count of anybody's work.
  /// Dividing it by twenty-four would have produced a plausible number
  /// measuring nothing — the adjacent quantity, answered convincingly.
  ///
  /// What is measured instead is the same question from the other side:
  /// how much of the workforce's year the kolkhoz actually took. The
  /// design's own gloss for the component is «обратный дрейф в ЛПХ», and a
  /// WORKING day an able-bodied villager spent on no kolkhoz assignment is
  /// that drift, whatever he did with it — while a holiday is a day off
  /// nobody may declare a working day, so it is not a yard's choice at all.
  /// Confirmed by boss (parcel 134), who is amending epochs §6 to say this
  /// rather than "kolkhoz over kolkhoz plus household".
  ///
  /// Written by the sequential day of core_residents.
  float able_bodied_days = 0.0F;

  /// The year's delivery against the plan, 0..100, as the mean over the
  /// POSITIONS the district asked for — sold ÷ owed, each position capped at
  /// 100 so a doubled oat does not buy a missing wheat.
  ///
  /// AND THE BYTE BESIDE IT, because a year of nought per cent and a year
  /// the district never spoke of are different facts that a float cannot
  /// tell apart. `plan.announced` is what says which, and it is a better
  /// door than the tonnage the verdict still keys off: a district that
  /// worked no land last year IS spoken to and asked nothing, which is not
  /// a world with no district in its tables.
  ///
  /// Written by the district plan of core_production, at the judgement.
  float plan_percent = 0.0F;

  std::uint8_t plan_percent_known = 0;

  /// The wintering, as it stood on the FIRST OF DECEMBER: how many days of
  /// food the stores and pantries hold, how many days of fodder the farm
  /// holds, and how many days there are to the first spring grass. A date
  /// and not a mean, because a winter stock is judged at the moment it stops
  /// growing.
  ///
  /// THREE DAY-COUNTS AND NOT TWO SHARES, deliberately. A share is the one
  /// shape in which drift is invisible — the quotient still looks like a
  /// quotient — and here the two numerators belong to different modules
  /// while the denominator belongs to a third rule. Each owner books the
  /// count it owns; the division happens once, at the reader, over three
  /// numbers that can each be checked against the world.
  ///
  /// Taken off the same two forecasts the office's lights are drawn from, so
  /// the index and the light the player watched all autumn cannot disagree.
  /// The byte says the snapshot was taken at all: a campaign that ends before
  /// its first December has no wintering to judge, which is not the same as
  /// a wintering that failed.
  /// The village's mean count of food categories eaten in the WORST of the
  /// year's four seasons, and how many of the four were lived through.
  ///
  /// THE WORST AND NOT THE MEAN, because the block it feeds is «во все четыре
  /// сезона, включая зиму»: a summer of six categories does not answer for a
  /// winter of two, and a year's average would let it. Sampled on the first
  /// day of each season, when the season just ended still holds its mask —
  /// the masks are cleared at the evening meal of a season's first day, so
  /// the day's turn is the one moment the finished season can be read.
  ///
  /// The count beside it is the usual second question: a campaign that has
  /// not lived four seasons has not failed the block, it has not been asked.
  float worst_season_variety = 0.0F;

  std::uint8_t variety_seasons_seen = 0;

  float food_days_dec1 = 0.0F;

  float feed_days_dec1 = 0.0F;

  std::uint16_t winter_days_dec1 = 0;

  std::uint8_t winter_cover_taken = 0;
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
  /// mean over the WORKED arable WEIGHTED BY AREA. Meadows are out because a
  /// meadow has no fertility to improve or exhaust, and ground the player
  /// has given no rotation is out because it is not being farmed
  /// (land_state.h, LandKind and HasRotation).
  ///
  /// THE SECOND HALF USED TO BE THE LAND KIND and this line said so: unworked
  /// ground carried LandKind::kDerelict and fell out unremarked. The kind was
  /// removed on 2026-09-12 and the walk kept the same SET by testing the
  /// rotation instead — the numbers the reconciliation was taken against are
  /// unchanged — but for one afternoon this contract went on naming a filter
  /// that no longer existed.
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
