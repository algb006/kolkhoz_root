/// @file
/// @brief UnitRow — the per-unit state: type, place, stock, and the site it
/// is while it is being built, raised a level or taken down.
/// @threading PARALLEL_READONLY
/// Rows live in WorldState::units under the double-buffer discipline. EVERY
/// write to a unit row is sequential: stores move in the production
/// decisions sub-step (slot 3), houses appear in the demography sub-step,
/// the construction sub-step of the same slot moves the `construction`
/// block, the level, `wear` and `dead`, `paused` is set by the production sub-step
/// reading kPauseUnit and only read by construction and the boundary after
/// it (task A8), and the parallel slots touch no unit at all — slot
/// 4 is split by FIELD (land_state.h), slot 5 by FAMILY (metrics). The
/// instant-delivery stub that used to sit between them was the logistics
/// slot, removed with the seventh phase on 2026-09-05. When logistics becomes
/// real and takes units as its unit of parallelism, that is a threading
/// change and this block changes with it. Structural
/// changes — a row appended for a marked site, a row removed after
/// demolition — stay sequential regardless (buffer-law rule 6).
///
/// Design sources: unit rules design (one generic Unit, types in a table —
/// never a class per building; §9 the plot, §11 levels, §14 demolition),
/// construction design (§6 marking and the manual start, §7 the phases, §8
/// labour days and progress, §12 demolition), production units design §10
/// (storage), the start canon (start.md §10: what already stands).
///
/// A SITE IS A UNIT ROW, not a table of its own (project phase 2, task A2;
/// manual/71-construction.md §2). The boundary contract promised that
/// construction progress arrives "as fields of the same rows" (70-boundary
/// §3), an upgrade is a site ON a unit that already exists and keeps its
/// id, and the site's materials are delivered into the same `stock` any
/// unit has — so the logistics that will replace the instant stub (task A4)
/// needs no second kind of destination. A unit that is not yet built is a
/// row at LEVEL 0: the one rule every consumer needs.
///
/// WEAR IS A NUMBER ON THE ROW (project phase 2, task A5;
/// manual/73-wear-and-repair.md). A building ages: 0..100, faster while
/// lived in or worked in, by the amortization years of the build class of
/// the level it stands at; at 100 it is a ruin that still works — nothing
/// vanishes from wear except the start's old houses, which the canon lets
/// collapse (start design §4). Repair is a SITE on the unit, like an
/// upgrade: kDelivering, then kRepairing, and the number returns to zero.
/// An upgrade repairs on its way (unit rules §11).
///
/// WHAT WEAR DOES, AND WHAT IT STILL DOES NOT (2026-09-17). Output: it now
/// falls, linearly from the first per cent, to `wear_output_loss_at_full` of
/// itself at the top of the scale — unit rules §15, applied where the core
/// turns work into goods, which today is the sawmill and nothing else
/// (core_production/unit_production.h). Measured with it: a worn saw makes
/// the building chain wait 13-16% longer over thirty years.
///
/// STILL NOT THE CORE'S, and both are absent rather than neglected: a
/// dwelling's COMFORT, which this core has no notion of at all, and
/// MACHINERY's speed and breakdowns past half the scale, which needs
/// machinery. The same table of the design carries all three rows; this line
/// used to cover them together and now covers only what is left.
///
/// What is deliberately NOT here yet: staff assignments, upgrade modules.
/// Fields for them are added when their systems arrive — appending is the
/// cheap extension.

#ifndef CORE_COMMON_UNIT_STATE_H_
#define CORE_COMMON_UNIT_STATE_H_

#include <cstdint>
#include <optional>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_common/state_table.h"

namespace core {

/// @brief Where a unit stands as a construction site. The order of the
/// values is the order of the phases (construction design §7); kNone is
/// "not a site", which is what every unit is most of its life.
enum class ConstructionPhase : std::uint8_t {
  kNone = 0,  ///< Built and standing (or a start unit). Nothing in progress.

  /// Marked out with pegs and string (construction design §6): the plot is
  /// taken, nothing has been spent, nothing happens until kStartBuild. May
  /// lie for years; removed at once and for free by kDemolishUnit.
  kMarked,

  /// Started; materials are being brought to the site — into `stock`.
  /// Site preparation (phase 0 of the design: clearing, levelling) is
  /// folded in here as a STUB of zero cost: the core has no trees and no
  /// relief to clear.
  kDelivering,

  /// The full recipe is on site; labour is being invested. This is the
  /// phase the labor sub-step drains (construction.labor_days_remaining).
  kBuilding,

  /// Being taken down (construction design §12): labour being invested in
  /// the dismantling. The unit is at level 0 from the moment this begins.
  ///
  /// ITS STOCK WAITS ON THE SITE (since 0.34.10; boss, host-econ-shops seq
  /// 23: «разбор не теряет запас в момент приказа»): what the unit held goes
  /// to the stores through the stores' door each evening, as room allows,
  /// and what has no room yet lies here — nobody takes from a level-0 site,
  /// and kDemolitionStockWaiting says what lies and why. The row is removed
  /// once the labour reaches zero AND the stock is gone. Until 0.34.9 the
  /// stock went out at the order through a door of construction's own, and
  /// what did not fit was booked lost that instant.
  kDemolishing,

  /// Being repaired (task A5; construction design §11, unit rules §15):
  /// the spare parts are on site, labour is being invested, and the unit
  /// WORKS meanwhile at its own level — like an upgrade, a repair is a
  /// site on a standing unit. target_level equals the current level, so
  /// that every reader of the site block sees "nothing moves". At zero the
  /// parts are consumed and UnitRow::wear returns to 0. Appended after
  /// kDemolishing so that no stored value changes meaning.
  kRepairing,

  /// Being insulated with straw (unit rules §16; kInsulateUnit). ONE PHASE
  /// FOR BOTH HALVES: while the straw is short the stores are drawn on and
  /// labor_days_remaining stays 0 — so the labor sub-step, which drains only
  /// a positive seam, sends nobody; with the straw all on site the frozen
  /// man-days open. The straw it needs is frozen in `reserved` at the order.
  /// target_level equals the current level, and the unit works meanwhile. At
  /// zero the straw is spent and UnitRow::insulated set. Appended after
  /// kRepairing so that no stored value changes meaning.
  kInsulating,

  /// NOT A VALUE, and never written to a save or read from one: the
  /// codecs range-check 0..kConstructionPhaseCount-1 and this is what they check against.
  /// Values are appended BEFORE it — that is the whole rule, and it is a
  /// fact here rather than an instruction somewhere else. A length
  /// written out by hand beside an enum drifts, and four of them already
  /// had (journal_codec.cpp).
  kConstructionPhaseCount,
};

/// @brief The site block of a unit: what is being built here and how far
/// it has come. Plain data; all zeros when the unit is simply standing.
///
/// THE LABOUR SEAM. Like a field's work_days_remaining (land_state.h) and a
/// herd's care_days_remaining, `labor_days_remaining` is the contract
/// between two modules that never call each other: construction sets it
/// when a site enters kBuilding or kDemolishing, the labor sub-step drains
/// it with the crew's hourly output (WorkKind::kConstruction), construction
/// completes the site when it reaches zero. `labor_days_total` is the norm
/// it started from, FROZEN at start: "invested 40 of 120" (construction
/// design §8) must read the same after a balance edit of the class norm
/// mid-build, and a site is priced once, when it starts.
/// @brief ONE SENDER OF A WORKS' SUPPLY (routing stage V2, the direct leg; save
/// 144): a place where materials fixed for the works lie until the carts
/// bring them — a store that gave them, or the stand or the pit they were
/// felled or dug at («прямое плечо»: what lies there is taken first and goes
/// straight to the site, never through the pile). Plain data.
struct SupplyLeg {
  /// Where it lies: the store's position, the stand's or the pit's loading
  /// point.
  Vec2 from;

  /// 1: logs straight from a stand — the log cart's way, across the felling's
  /// ground (TravelMode::kLogCart); 0: the cart that keeps to the roads.
  std::uint8_t log_way = 0;

  /// What of the works' `awaiting` lies here, dense by ResourceId, grams.
  ResourceAmounts grams;

  /// Of them, the grams production has not yet priced into
  /// ConstructionState::supply_days_first. Raised by the fixing, cleared by
  /// production's settlement of the same hour.
  Grams unpriced = 0;
};

struct ConstructionState {
  ConstructionPhase phase = ConstructionPhase::kNone;

  /// THE AVRAL ON THIS STEP (kDeclareRush on a unit; unit rules §7; save
  /// 65): 0 none, 1..kMaxRushStep steps of `rush_step_percent`. It lives in
  /// the site block so that it goes out with the step: every end of a step
  /// resets the block whole (construction_system.cpp, insulation.cpp).
  std::uint8_t rush_step = 0;

  /// The level being built: 1 for a new unit, N + 1 for an upgrade of a
  /// unit standing at N (unit rules §11). 0 while demolishing and when
  /// nothing is in progress. While an upgrade is in progress the unit
  /// WORKS at N — and its capacities stay N's until the day the level
  /// moves: the stable keeps its horses under a roof, the house keeps its
  /// family, and nothing grows early (unit rules §11, the rule of
  /// 2026-08-31).
  std::uint8_t target_level = 0;

  /// Game man-days the site started with — the build class's norm of the
  /// target level, or the demolition share of it. Frozen at start.
  float labor_days_total = 0.0F;

  /// Game man-days still to invest. > 0 only in kBuilding and kDemolishing.
  float labor_days_remaining = 0.0F;

  /// At most this many builders on the site at once — the build class's
  /// ceiling, copied from the level's row (unit_levels.csv max_crew) when
  /// the site starts, so that the labor sub-step reads the cap where it
  /// reads the days left and never opens a table. 0 = uncapped (a
  /// demolition, or a class that names none). What keeps "a barn is a
  /// couple of weeks for a brigade" from becoming three days for the whole
  /// village (construction design §8).
  std::uint8_t max_crew = 0;

  /// WHETHER THE SITE WORKS IN WINTER (unit_levels.csv `winter_works`, save
  /// 80), copied from the level's row with `max_crew` and for its reason:
  /// the labor sub-step reads it where it reads the days left. 1 — log,
  /// plank, shelter and gear keep going; 0 — earthworks, pads, brick and
  /// concrete stand in the core's winter season and lose nothing, like a
  /// pause (construction design §8; core_common/rain_stops_work.h,
  /// WinterStopsSite). 1 when nothing is being built.
  ///
  /// "LIKE A PAUSE" MEANS THE WORK ONLY (boss, boss-core-epoch1-resume seq
  /// 23): the recipe is still carried in all winter — «зима — время
  /// подготовки: лес, доски, подвоз материалов» (§8) — while the player's
  /// pause stops the delivery too (§6). The two never shared a flag: this
  /// byte gates the crew, `UnitRow::paused` the crew and the delivery.
  std::uint8_t winter_works = 1;

  /// DAYS IN A ROW THE SITE STOOD IN kBuilding WITH NOBODY ON IT at the
  /// day's noon (save 115; boss-core-epoch1-queue-2026-09-29 [36]): the
  /// memory kSiteWithoutCrew's lamp reads. A day off and a day the winter
  /// stops the site by rule (WinterStopsSite) neither count nor reset it; a
  /// day with a crew resets it. Emptied with the block when the works end.
  std::uint16_t crewless_days = 0;

  /// THE WORKS' OWN SHARE OF A STANDING UNIT'S STOCK, dense by ResourceId:
  /// grams of `UnitRow::stock` held for the level being raised (or the
  /// spare parts of a repair) and for nothing else. A level-0 site needs no
  /// such line — a site stores nothing for anybody, and every taker skips
  /// it whole. A STANDING unit keeps working while its next level goes up
  /// (unit rules §11), and its stock is also a store: without this line the
  /// recipe the start checked and carried in (construction design §6) sat
  /// where the production sub-step's takers — the saw's logs, the plan's
  /// deliveries, a herd's feed — could draw it out a day later, and the
  /// check guaranteed nothing (boss, parcel 294).
  ///
  /// Written only by the construction sub-step: raised as the recipe is
  /// carried in, never above the recipe line nor above the stock; emptied
  /// with the rest of this block when the works end. Every taker of a
  /// unit's stock for any other purpose takes only `stock - reserved`, never
  /// below zero. Empty vector = nothing held back.
  ResourceAmounts reserved;

  /// THE MATERIALS ON THEIR WAY TO THE SITE (routing stage V2, save 147;
  /// transport design §12, «Снабжение … материалы на стройку»), dense by
  /// ResourceId, grams: taken out of the stores' stock for this works and not
  /// yet on the site. The start's fixing is unchanged — «с запуска они
  /// принадлежат площадке: пила, другой юнит и выдача их не берут»
  /// (construction design §6) — and only the PLACE lags: what awaits lies at
  /// the sender until the carts bring it, `UnitRow::stock` is what is on the
  /// site, and the labour opens on the stock alone. Until 0.37.204 the whole
  /// recipe was on the site the tick it started, the stores' distance ignored.
  ///
  /// Written by the construction sub-step (raised as it takes from the
  /// stores) and by production's hourly settlement of the supply seam
  /// (lowered as the carted share lands in `stock`). Nobody else reads it as
  /// a store: it is in no unit's stock. Emptied with the block.
  ResourceAmounts awaiting;

  /// WHERE WHAT AWAITS LIES, A LEG A SENDER (save 147), in the order it is
  /// carted: the stands and the pits first, then the stores, each group the
  /// nearest to the site first. The sum of the legs' grams is `awaiting`, to
  /// the gram and by resource — two homes bound by that invariant, kept by the
  /// two writers of both (site_supply.cpp's fixing, field_haul.cpp's
  /// settlement). ONE LEG IS CARTED AT A TIME, the front one: a works has one
  /// supply seam, and the carter's place is the front leg's sender. Until the
  /// direct leg a works had ONE sender — the store that gave the most — and
  /// the way of everything was measured from it, whichever stores gave.
  std::vector<SupplyLeg> legs;

  /// The front leg's sender and way — what the seam below is measured from
  /// and where the carter loads (work_seam.cpp, labor_system.cpp). Mirrors of
  /// `legs.front()`, written with it; meaningless while nothing awaits.
  Vec2 supply_from;
  std::uint8_t supply_log_way = 0;

  /// «ПОДНЯТО В ОЧЕРЕДИ» (kRaiseSite; society design §1; #595, save 154):
  /// 1 - this works goes first among its kind in both its doors: its
  /// construction job first inside its tier (AssignmentJob::raised) and its
  /// supply at the head of the sites' queue, at level 0 (core_common/
  /// site_supply_queue.h; LogisticsTaskRow::level). Written by the
  /// order's consumer (core_construction) and emptied with the block when
  /// the works end; a lowered site (enable 0) is plain again. Sits in the
  /// padding after `supply_log_way`, so sizeof(ConstructionState) did not
  /// move - the wire did, by one byte, and VERSION_SAVE counts that.
  std::uint8_t raised = 0;

  /// THE WORKS' WHOLE LOAD AS FIRST PRICED, cart-days (save 147): every leg's
  /// grams by its own way, summed by production in the hour they were fixed
  /// (field_haul.h, SettleSiteSupply). The supply's term is counted from it
  /// (labor_system.cpp: a works of many cart-days is not held to the term of
  /// a works of two). Nought while nothing awaits.
  float supply_days_first = 0.0F;

  /// WHAT IS STILL TO CART ON EVERY LEG, cart-days (save 147): the legs'
  /// grams by their own ways, summed by production whenever it writes the
  /// seam. The labour sub-step sizes the supply's crew by it — the seam
  /// below is the front leg's alone, and a crew sized by one leg of five
  /// carted a works' load a leg a morning (the direct leg's first run: a
  /// works fed from stores alone waited 5 days at the median where one seam
  /// for the whole load had it at 3).
  float supply_days_all = 0.0F;

  /// THE SUPPLY SEAM, in cart-days as every load's (field_haul.h, HaulDaysFor):
  /// written by production in the evening from what awaits on the FRONT leg,
  /// its way and the bed; drained by the labour hour (work_seam.h, WorkSeamOf — a hauling
  /// work that names this unit); settled every hour as stage V1 settles a
  /// load, the drained share of `awaiting` landing in the site's stock.
  /// `written` is what the seam stood at when last settled.
  float supply_days_remaining = 0.0F;
  float supply_days_written = 0.0F;

  /// THE DAY THE WAIT BEGAN (save 147): the game day materials were first
  /// fixed for these works while none awaited — the start's, or the morning
  /// a site that had all it could get was given more. The supply is due by a
  /// date counted from it (transport §12, level 1 «срок»; labor_system.cpp,
  /// the supply job's window): a site that has waited long outranks the
  /// windows a fresh one yields to. Meaningless while nothing awaits.
  SimDay supply_since_day = 0;

  /// WORKING DAYS IN A ROW SOMETHING AWAITED AND NOT ONE GRAM CAME (#734, save
  /// 158; transport design «Пол подвоза»): the memory the supply's floor and
  /// kSiteSupplyWaits read (core_common/site_supply_floor.h). Written by
  /// production's settlement of the supply seam (supply_haul.cpp), the module
  /// that knows a delivery: +1 the evening of a day nothing landed, nought the
  /// evening of a day anything did - UNLESS the works is on the floor already
  /// (the count at the floor's days): then a delivery leaves it where it is,
  /// and the works stays on the floor till its recipe is on the site. A day
  /// off and a day the works stood paused neither count nor reset it, as
  /// `crewless_days`. Nought while nothing awaits; emptied with the block.
  std::uint16_t supply_idle_days = 0;

  /// 1 - a gram of the supply landed on the site since the last evening (save
  /// 158): the day's memory `supply_idle_days` is settled from, cleared by the
  /// evening that reads it.
  std::uint8_t supply_moved_today = 0;

  /// THE DAYS OF `supply_idle_days` A MOWING HELD (save 165; stage В6 part 3,
  /// boss, the В6 thread [10]-[12]; transport design «Подвоз к стройке»: «дни,
  /// когда покос забрал возчиков, не идут в срок стройки»): of the idle
  /// evenings counted, those on which a meadow's cut was open in its window
  /// (supply_haul.cpp, MowingHoldsTheCarters). THE LAMP's reading only: the
  /// floor and its carters read `supply_idle_days` whole, so scheduling is
  /// 0.37.276's; kSiteSupplyWaits lights on the difference
  /// (site_supply_floor.h, SupplyLampDays). Never above `supply_idle_days`,
  /// reset with it.
  std::uint16_t supply_mowing_days = 0;
};

/// @brief A RICK'S SUPPLY FROM ANOTHER RICK (#1056 C1.2; econ's stage C
/// contract 574c366; logistics_state.h, kRickSupply; save 171): written on
/// the RECEIVING rick by production's evening (core_production rick_supply.h)
/// when its herds' hay falls below its threshold, drained by the hauling on
/// that unit, settled hourly by moving hay from `from` into this rick. All
/// nought and `from` invalid when no supply stands.
struct RickSupplySeam {
  /// The rick the hay is carted from.
  UnitId from;
  /// The feed carted (hay: the one feed a rick is short of, feed_home.cpp).
  ResourceId resource;
  /// Game man-days of carting left today, and what the last settle wrote
  /// (the perevalka's two, UnitRow::haul_days_remaining / written).
  float days_remaining = 0.0F;
  float days_written = 0.0F;
  /// The hay still to cart, grams: the evening's price less what was moved.
  Grams grams = 0;
  /// The threshold it was priced against, days of its herds' hay: the urgent
  /// check's horizon + farming.csv `rick_threshold_add_days` (STUB 7).
  std::uint8_t threshold_days = 0;
};

/// @brief Whether any material of the works still awaits its carting
/// (ConstructionState::awaiting).
inline bool AnythingAwaits(const ConstructionState& works) {
  for (const Grams grams : works.awaiting) {
    if (grams > 0) {
      return true;
    }
  }
  return false;
}

/// @brief One unit. Plain data; behavior comes from the type's table row.
struct UnitRow {
  /// Row of tables/unit_types.csv: barn, warehouse, well, house, heap...
  UnitTypeId type;

  Vec2 position;

  /// Unit level, 1-based on the type's own ladder (unit rules design §11:
  /// the first level IS the built unit). LEVEL 0 MEANS "NOT BUILT": a
  /// marked or unfinished site, or a unit being demolished. A level-0 unit
  /// produces nothing, stores nothing for anyone, houses nobody and holds
  /// no herd — every consumer that reads a unit's level treats 0 as absent
  /// (manual/71-construction.md §2), and that one rule replaces a flag in
  /// every table.
  std::uint8_t level = 1;

  /// The family living here, for house-kind units; invalid otherwise.
  /// One family - one house (families design §1).
  FamilyId household;

  /// What the unit holds, dense by ResourceId: a warehouse's stores, a
  /// stock-yard's feed buffer, a heap's logs or manure — and, while the
  /// unit is a site, the materials delivered for the level being built
  /// (consumed on completion, recipe-exact). Empty vector = holds nothing
  /// yet (sized on first delivery).
  ResourceAmounts stock;

  /// The site block (see ConstructionState). All zeros for a unit that is
  /// simply standing, which is most units most of the time.
  ConstructionState construction;

  /// A works put aside for the winter, whole (#715; livestock design «Не
  /// успел»: the straw coat «ставится ВМЕСТО глиняного… глиняное открывается
  /// весной»). Set when kInsulateUnit lands on a livestock unit whose open
  /// works is its clay warm rung: that works moves here, with everything on
  /// its way and everything it holds back, and the coat takes `construction`.
  /// It comes back into `construction` when the coat is done and the season
  /// is not winter. While it is here nothing reads it but the store's
  /// holding-back (UnreservedOf) and the doors that refuse a second works.
  /// Save 157.
  std::optional<ConstructionState> suspended_works;

  /// Wear of the building, 0..100 (unit rules §15: 0-25 as new, 26-50
  /// worn but sound, 51-75 visibly decayed, 76-99 falling apart, 100 a
  /// ruin that still works and never vanishes). Grows once a day in the
  /// construction sub-step by the amortization of the STANDING level's
  /// build class — a full scale in `wear_years_idle` years of standing
  /// empty, in `wear_years_in_use` years while a household lives here or
  /// somebody works here today (task A5, manual/73-wear-and-repair.md §2).
  ///
  /// AND SINCE 2026-09-17 IT ALSO JUMPS: a fire adds a third of the scale in
  /// one morning (construction_system.cpp, RunFires), so "grows once a day by
  /// the amortization" was no longer the whole of how this number moves. It
  /// still only ever RISES — the fire's own ceiling is taken so that it can
  /// never walk the value back, which it did for one commit.
  /// Stays 0 for a type with no building (has_wear = 0: a heap, a stack,
  /// a trench) and for a site at level 0. Reset to 0 by a finished repair
  /// or upgrade. The start's old houses begin part worn — each drawn from
  /// the band in construction.csv, 45..60 as shipped, so that they do not
  /// all fall on one night — and are the one type that collapses at 100
  /// (start design §4).
  ///
  /// Written only by the construction sub-step and by genesis; read by the
  /// boundary's UnitSignals::wear and, later, by the systems whose numbers
  /// it will move. Float on purpose: the daily share is well under one
  /// percent, and an integer would truncate it to nothing — the lesson of
  /// the herds and the clothing scales, learned three times already.
  Metric wear = 0.0F;

  /// Production here is STOPPED by the chairman's order (unit rules §5;
  /// task A8). Not demolished and not mothballed — paused: the order says
  /// when, and the unit waits where it stands.
  ///
  /// What it means in the slice, said plainly because it is less than the
  /// design describes: **the building does not wear while it is stopped**
  /// (unit rules §15 — "a standing unit does not wear out"), and the layer
  /// can show the state. The rest of §5 — production halted, workers
  /// released to the pool, supply stopped while carrying continues — needs
  /// unit work CYCLES, and the core has none yet: what a unit does today is
  /// hold a herd, hold goods and wear out. Pausing must not stop the herd
  /// being fed, so it does not touch herd care.
  ///
  /// "Stops when the running cycle ends" (§5) is likewise nothing to
  /// implement yet and everything to remember: when cycles arrive, this flag
  /// is what they will consult, and the ORDER is what will wait.
  std::uint8_t paused = 0;

  /// «ОСВОБОДИТЬ СКЛАД» (kEmptyStore; start §5; registers 214 and 233; save
  /// 67): 0 none; 1 while the chairman's order stands — the church store or a
  /// clamp accepts no delivery, and what lies in it is the load of the
  /// «перевалка» (core_production field_haul.h, SettleStoreEmptying); 2 the
  /// same with the carrying paused by kPauseUnit. The pause lives HERE and
  /// not in `paused`, so the unit keeps wearing (boss seq 119).
  std::uint8_t emptying = 0;

  /// THE PEREVALKA'S SEAM (save 67), game man-days: labor drains
  /// `haul_days_remaining` as it drains a field's haul, and the day's
  /// settlement moves the share done (`written` − `remaining`) / `written`
  /// of the load and writes tomorrow's demand into both. 0 when nothing is
  /// left to carry or no order stands.
  float haul_days_remaining = 0.0F;
  float haul_days_written = 0.0F;

  /// THE PEREVALKA'S RECEIVER (stage В, F3 (A); save 153): the store the
  /// emptied goods go to, as the seam above is priced (field_haul.cpp, the
  /// store-emptying settlement), written with it by production. A works'
  /// supply needs none: its receiver is the unit itself.
  Vec2 haul_to;

  /// THE UNIT STANDS AND DOES NOT WORK AT ALL until it is restored — the
  /// wrecked water mill of the first morning (start design; boss's decision
  /// of 2026-09-12). Set by genesis from start_layout.csv and by nothing
  /// else; cleared by a finished repair OR UPGRADE, like `wear` — an upgrade
  /// rebuilds, and a rebuild revives as surely as a repair does.
  ///
  /// The second writer is named here on purpose. In this module the
  /// per-field writer sentence IS the threading contract: it is what a later
  /// reader checks a proposed new writer against before deciding which slot
  /// may touch a unit row, so a list short by one is the shape in which a
  /// parallel writer slips in unchallenged.
  ///
  /// A STATE OF ITS OWN AND NOT A WEAR, because wear never stops anything:
  /// unit rules §15 ends the scale at "a ruin that still works". A hundred
  /// per cent said "worn to the limit and still grinding", which is the
  /// opposite of the picture the canon opens on, and there was no other
  /// number left to say it with.
  ///
  /// And not `paused` either: that is an ORDER the chairman gave and can
  /// take back, and the two would be told apart by nothing once both were
  /// bytes in a row. A dead mill is a fact about the mill.
  std::uint8_t dead = 0;

  /// 0/1: A HOUSE HELD FOR A SPECIALIST (kReserveHouse; social units, the
  /// reading hut; boss seq 191, 197): no couple, migrant or roofless family
  /// moves in — except a roofless family in the cold — and the specialist the
  /// district sends takes it first; the mark comes off when he is in. Written
  /// by core_residents' decisions sub-step and the save's load. Save 74.
  std::uint8_t reserved_for_specialist = 0;

  /// 0/1: THE UNIT IS "WARM" — insulated (unit rules §16). Written only by
  /// the construction sub-step of the decisions slot (phase 3) and by the
  /// save's load. Set by a finished kInsulateUnit; cleared by a finished
  /// upgrade TO the third level ("the
  /// third level rebuilds the walls and the roof", construction.csv
  /// insulation_reset_level) and by nothing else — an epoch's change keeps
  /// it, and an upgrade above the third does not reset it. STUB: what it does
  /// (firewood, the herd's winter, comfort) waits for heating and cold, which
  /// the core does not have yet; and the "built insulated" package.
  std::uint8_t insulated = 0;

  /// HOW FAR THE STINK OF THIS SOURCE REACHES TODAY, in metres from the unit
  /// (water design §4). 0 for everything that does not smell, and for a
  /// source that has not started yet.
  ///
  /// METRES AND NOT A SHARE, because the decay is a SPEED and not a term —
  /// boss's rule of 2026-09-06, and the whole reason the design needs only
  /// one decay rule instead of two. The zone shrinks by so many metres a
  /// day, so a big one takes longer to go out than a small one WITHOUT
  /// anybody saying so: a forge that worked a morning reached twenty metres
  /// and is clean by evening; a tannery that ran a season stands at its full
  /// two hundred and still stinks a week into its idleness. Give the decay a
  /// LENGTH instead and you must then explain why the forge's is different.
  ///
  /// Grows while the source emits, up to the full radius of its strength;
  /// falls when it does not. Written once a day by the construction
  /// sub-step, read by the two seam queries. A unit whose type does not
  /// smell keeps this at zero for ever.
  float stink_radius_m = 0.0F;

  /// THE UNIT THIS ONE IS A MODULE OF — invalid for a free-standing unit
  /// (unit rules §11, "Модули"; boss's decisions of 2026-09-13, parcel 198).
  /// A type is a module when unit_types.csv names a `parent` for it. The row
  /// names the parent UNIT, not only its type, because two yards of one type
  /// may stand and a module belongs to the one whose plot it was put on. Set
  /// when the module is marked (kBuildUnit) and never moved. A module stands
  /// on its parent's plot, is built only while the parent stands sound, and
  /// does not work while it does not (ModuleParentSound, module_rules.h).
  UnitId parent;

  /// THE UNIT'S OWN WORK SEAM, game man-days — what a producing unit asks of
  /// its workers today (unit rules §4: "the unit turns out its item while its
  /// workers stand at it, there is raw material and it is not paused").
  /// Written by production at the day's last tick from the raw material it
  /// holds, drained by labor (WorkKind::kUnitWork), settled by production the
  /// next last tick into the product. The same contract as a field's carting
  /// seam (land_state.h, haul_days_remaining). Zero for every unit that turns
  /// out nothing — which is every unit but the sawmill on 2026-09-13.
  float production_days_remaining = 0.0F;

  /// What production last wrote into the seam above, so that it can tell
  /// what the workers did from what the raw material allowed. Same contract
  /// as FieldRow::haul_days_written.
  float production_days_written = 0.0F;

  /// A rick's supply from another rick (#1056 C1.2; save 171): see the type.
  RickSupplySeam rick_supply;
};

/// @brief Whether `unit` is supplied from another rick NOW (#1056 C1.2): a
/// supply stands, and the unit is neither emptied nor a works' receiver -
/// one hauling work names one unit, and those two are asked first
/// (work_seam.cpp).
inline bool RickSupplyStands(const UnitRow& unit) {
  return unit.emptying == 0 && unit.rick_supply.from.value != kInvalidEntityIdValue &&
         !AnythingAwaits(unit.construction);
}

/// @brief The units table type used by WorldState.
using UnitTable = StateTable<UnitId, UnitRow>;

/// @brief Whether the works open on `unit` is one the straw coat PUTS ASIDE
/// for the winter (#715; livestock design «Не успел»): the unit's own next
/// rung, carting or under labour, of a class the winter stops, with nothing
/// put aside already. ONE HOME since 0.37.240 for the door that moves the
/// works (core_construction, StartInsulation), the lamp that advises the coat
/// (core_production, herd_cold.cpp) and the run's chairman - until then the
/// rule stood in the door and in the lamp apart, held together only by the
/// advice guard.
/// @note Says nothing of the unit being a livestock one or already coated:
///       those are the caller's own terms.
inline bool CoatPutsAsideOpenWorks(const UnitRow& unit) {
  const ConstructionState& open = unit.construction;
  return !unit.suspended_works.has_value() &&
         (open.phase == ConstructionPhase::kDelivering ||
          open.phase == ConstructionPhase::kBuilding) &&
         open.target_level == unit.level + 1 && open.winter_works == 0;
}

/// @brief What of `resource` in a unit's stock any taker OTHER than the
/// unit's own works may have: `stock - construction.reserved`, never below
/// zero. One accessor on purpose — every module that takes from a unit reads
/// it, and a second copy of the subtraction is how one of them forgets it.
/// It does not skip a level-0 site; the caller's own rule does.
inline Grams UnreservedOf(const UnitRow& unit, ResourceId resource) {
  const Grams held = AmountOf(unit.stock, resource);
  // A works put aside for the winter (#715) still holds back what it carried
  // in: its clay and logs are the spring's, not the store's to give.
  const Grams suspended =
      unit.suspended_works.has_value() ? AmountOf(unit.suspended_works->reserved, resource) : 0;
  const Grams free = held - AmountOf(unit.construction.reserved, resource) - suspended;
  return free > 0 ? free : 0;
}

}  // namespace core

#endif  // CORE_COMMON_UNIT_STATE_H_
