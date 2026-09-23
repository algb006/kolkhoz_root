// Internal to core_production: configuration parsed from the balance tables.
//
// CropDef mirrors tables/crops.csv (indexed by CropId = row), LivestockDef
// mirrors tables/livestock.csv, UnitTypeDef mirrors tables/unit_types.csv,
// FarmingConfig mirrors tables/farming.csv, FeedLinkDef mirrors
// tables/feed_links.csv (stage 6). A world without these tables
// (unit tests, early runs) gets empty rosters and the subsystem idles;
// present-but-malformed tables refuse the factory.

/// @threading PARALLEL_READONLY
/// Filled ONCE by the factory and never written again. FieldGrowthPhase
/// (slot 4, parallel by field) holds a pointer to it and reads the crop and
/// farming rosters from every worker. Safe because nothing writes it — and
/// for no other reason, so no per-step field may be added here.

#ifndef CORE_PRODUCTION_PRODUCTION_CONFIG_H_
#define CORE_PRODUCTION_PRODUCTION_CONFIG_H_

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core_catalog/district_trip_catalog.h"
#include "core_catalog/district_visit_catalog.h"
#include "core_catalog/extraction_catalog.h"
#include "core_catalog/limit_catalog.h"
#include "core_catalog/processing_catalog.h"
#include "core_catalog/timber_catalog.h"
#include "core_common/calendar.h"
#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_common/rain_stops_work.h"

namespace core {

class ITableSet;  // Defined in core_tables.

struct CropDef {
  ResourceId resource;  ///< What the harvest and the seed are.

  bool is_winter = false;  ///< Sown in autumn, harvested next July.

  bool is_perennial = false;  ///< Sown once, harvested for years.

  std::uint8_t sow_from_month = 0;  ///< 0-based month indices, inclusive.

  std::uint8_t sow_to_month = 0;

  std::uint8_t harvest_from_month = 0;

  std::uint8_t harvest_to_month = 0;

  float sow_min_temp_c = 0.0F;

  float harvest_min_temp_c = 0.0F;  ///< "Убрать до": kept for stage-5 timing.

  float yield_kg_per_ha = 0.0F;  ///< At neutral fertility.

  float sowing_norm_kg_per_ha = 0.0F;  ///< 0 = sowing consumes no material.

  /// Labor norms of the two crop-specific working phases, in GAME man-days
  /// per hectare (the table keeps REAL man-days; parsing divides by
  /// kRealDaysPerGameDay once). They size FieldRow::work_days_remaining when
  /// production opens the phase — the seam labor then drains
  /// (manual/65-labor-model.md §2). Grain anchor, real man-days per hectare:
  /// plow 10 + harrow 3 + sow 3 + harvest 8 = 24 (49-simulations §2).
  float sow_days_per_ha = 3.0F / kRealDaysPerGameDay;

  float harvest_days_per_ha = 8.0F / kRealDaysPerGameDay;

  float fertility_delta = 0.0F;  ///< Applied at harvest.

  /// Kilograms of straw per kilogram of grain reaped (design db
  /// crop.straw_ratio: rye 1.5, wheat 1.3, oat 1.1, barley 1.0). Straw is a
  /// reserve feed — own and free, and plainly there in a winter ration.
  /// 0 = the crop leaves nothing behind.
  float straw_ratio = 0.0F;

  float drought_sensitivity = 0.0F;  ///< 0..1 scale on the stress rate.

  float wet_sensitivity = 0.0F;
};

/// @brief One row of tables/livestock.csv (design db export).
///
/// THE UNIT TRAP, and it is the whole reason these names are this long.
/// The table mixes two clocks on purpose, and the rule is: **what is spent
/// is real, what ages is game**.
///   * Feed and care are REAL rates — a cow eats nine fodder units a day of
///     a real day, and its keeper spends 32 real man-days a year on it.
///     Parsing converts them once (x 365 / 48 for feed, / 7 for care) so
///     the yearly mass and the yearly labor both come out right.
///   * Ages are GAME units with the x4 life acceleration ALREADY APPLIED by
///     the design (livestock design §6, boss parcel 2026-08-30). "A cow is
///     adult at eight months" means eight GAME months, which is 2.67 years
///     of the animal's life. Dividing them again would make her adult at
///     eight months of life and a horse at one year.
struct LivestockDef {
  /// Manure per adult head per game year, kilograms.
  float manure_kg_per_year = 0.0F;

  /// Daily need of an adult head, fodder units per GAME day (oat = 1.0).
  /// The column feed_units_per_real_day keeps the REAL reference number
  /// (horse 10, cow 9, pig 3, sheep and goat 2); parsing multiplies by
  /// 365 / kDaysPerYear once so the yearly feed mass holds. Juveniles eat
  /// FarmingConfig::juvenile_feed_factor of it, newborns at the dam eat
  /// nothing (livestock design §11).
  float feed_units_per_game_day = 0.0F;

  /// Share of the daily need summer pasture and free-ranging cover in the
  /// pasture months (the norm is about need, not mandatory store spending).
  /// Canon: sheep 0.85, goat 0.8, cow 0.65, horse 0.5, duck 0.5 (reeds),
  /// hens 0.35, pigs and mink 0.
  float pasture_coverage_summer = 0.0F;

  /// 0/1: the kind has sexes; breeding then needs an adult male present.
  /// Poultry is sexless and has no newborn rung.
  std::uint8_t sexed = 1;

  /// 0/1: the kind lives only at family yards (goats) or only at kolkhoz
  /// units (sheep). Both zero = either place.
  std::uint8_t household_only = 0;

  std::uint8_t kolkhoz_only = 0;

  /// 0/1: AT A FAMILY YARD this kind eats nothing from any store (boss
  /// answer to question Q1, 2026-08-31). Hens, ducks and the yard pig live
  /// on range, scraps and the garden; the winter handful of grain comes out
  /// of the family's own ration, which the meal already counts. Feeding
  /// eight hens a full concentrate ration cost 350 kg of grain a year to
  /// return 60 kg of eggs, which no householder ever traded for.
  /// THE KOLKHOZ HERD OF THE SAME KIND IS UNAFFECTED: a poultry farm is a
  /// production unit and eats off the store by the norm.
  std::uint8_t household_self_fed = 0;

  /// How many head of this kind ONE YARD may keep (canon: cow 1, pig 1,
  /// goats 2, hens or ducks 10; 0 = a yard does not keep this animal at
  /// all, and the zero is a decision rather than a blank). Boss answer to
  /// question Q7, 2026-08-31.
  ///
  /// THE CAP IS WHAT STOPS A YARD'S BREEDING, and nothing else may: a goat
  /// lives two and a half game years and a hen one and a half, so a yard
  /// that cannot breed stands empty by the eighth year, taking with it the
  /// tonne of milk a year that is part of the canon's 59% coverage. The core
  /// blocked yard breeding outright at stage 6 — a stopgap for the missing
  /// cap, in the wrong place.
  float household_cap_heads = 0.0F;

  /// A yard keeps ONE kind of stock and ONE kind of bird (household design
  /// §2): 0 = neither, 1 = stock (cow, pig, goats), 2 = bird (hens, ducks).
  /// Without the group the cap is leaky — it would allow a cow AND a pig AND
  /// two goats at the same yard, four times the canon's household.
  std::uint8_t household_group = 0;

  // -- ages, GAME units (see the unit trap above) --------------------------
  /// Length of the newborn rung, game months; 0 = the kind has no such rung.
  float newborn_game_months = 0.0F;

  /// Age at which a head becomes an adult, game months, counted from birth.
  /// The juvenile rung therefore lasts this minus the newborn rung.
  float adult_from_game_months = 0.0F;

  /// The band of adult lifespan, game years: the age-death hazard is zero
  /// at the lower end and certain at the upper one (livestock design §6).
  float life_game_years_min = 0.0F;

  float life_game_years_max = 0.0F;

  /// Litters per adult female per GAME year — once a year, in its season,
  /// which is the canon of the yearly cycle.
  float births_per_game_year = 0.0F;

  float litter_heads = 1.0F;  ///< Newborns per litter.

  /// Share of the adults kept as males; a sexed herd always keeps at least
  /// one sire. Males beyond the share are slaughtered — a share, not a
  /// count, because "one bull" stops being right as the herd grows.
  float males_share = 0.0F;

  // -- produce: per adult head per game year, or per head at slaughter -----
  /// Milk is in LITRES in the table and roughly a kilogram a litre in the
  /// store (quantities.h); parsing does not convert, the flow does.
  float milk_l_per_year = 0.0F;

  float egg_kg_per_year = 0.0F;

  float wool_kg_per_year = 0.0F;

  float meat_kg_per_head = 0.0F;

  float hide_pieces_per_head = 0.0F;

  float pelt_pieces_per_head = 0.0F;

  float down_kg_per_head = 0.0F;
};

/// @brief One feeding-order row (tables/feed_links.csv, question 133):
/// which kind eats which resource. Row order IS the priority — regular
/// ration before reserve, fodder grain before bread grain; the exported
/// file keeps that order, so no sorting happens in code.
struct FeedLinkDef {
  LivestockKindId kind;

  ResourceId resource;

  std::uint8_t reserve = 0;  ///< 1 = reserve ration with a lowered effect.

  /// The largest share of the day's need this feed may cover. Order says
  /// what to spend FIRST; this says how much of it the animal can actually
  /// eat, and without it the model lies: a ruminant does not live on grain
  /// however much of it there is, and the first horse eats the village's
  /// whole year of oats (design db feed_link.max_share, boss 2026-08-30 —
  /// horse: oats 0.5, hay 1.0). Defaults to the whole need, so a table set
  /// without the column behaves as the order alone would.
  float max_share = 1.0F;

  /// 0/1: fed on WORKING DAYS ONLY (boss answer to question Q2,
  /// 2026-08-31). Oats are the horse's wage, not its keep: hay carries the
  /// ration all year round and grain goes out when the team goes out. At a
  /// full ration sixteen horses need 29 t of oats a year — more than the
  /// whole starting oat field yields — and the run duly fed them the
  /// village's bread grain until both ran out.
  std::uint8_t work_only = 0;

  /// 0/1: this feed is held in the FODDER FUND, rung 3 of the ladder
  /// (feed_links.csv `fodder_fund`; resources design §6, «фуражное зерно:
  /// овёс и ячмень»; boss, boss-core-epoch1-resume seq 21). The horse's oats
  /// and barley and nothing else: bread grain never, and bought compound
  /// feed is not named. Until 0.34.17 the fund was sized off every work feed
  /// at once — a horse-day held six times over, rye and wheat among them —
  /// and thirty_years failed its milk plan in year 30 for it. 0 when the
  /// table has no such column.
  std::uint8_t fodder_fund = 0;
};

/// Everything a unit type contributes to production, and every capacity in
/// it comes from the LEVEL LADDER. The type row carries no figure of its
/// own any more: the export wrote a copy of level 1 into it (a LEFT JOIN on
/// level = 1), so the type column and the level-1 row were the same number
/// BY CONSTRUCTION, and the fallback that read the type row could never
/// fire while looking perfectly alive. An arm that cannot differ from its
/// control is not a measurement — it is either the answer or an unplugged
/// wire, and the number alone does not tell the two apart (boss,
/// 2026-09-05: host nearly concluded that capacity means nothing from a run
/// byte-identical to its control).
struct UnitTypeDef {
  /// Storage capacity per LEVEL, in kilograms, index = level - 1
  /// (unit_levels.csv storage_capacity_t). "A store keeps its old ceiling"
  /// until the day the level moves (unit rules §11), so the ceiling that
  /// binds is the one of the level the unit stands at — a granary is 150 t
  /// at level 1 and 300 t at level 2. Empty = the type stores nothing by a
  /// number, and the load refuses a ladder that names a capacity at one
  /// step and leaves another blank (production_config.cpp).
  std::vector<float> level_storage_capacity_kg;

  /// Livestock places per LEVEL, in heads, index = level - 1
  /// (unit_levels.csv livestock_capacity_head). The same ladder rule: a
  /// cattle yard holds 24 head at level 1, 72 at level 2, 144 at level 3.
  std::vector<float> level_livestock_capacity_head;

  /// 0/1: the capacity is the outline the PLAYER draws, so there is no
  /// number to read (manure heap, silage trench, hay stack, the log, stone
  /// and clay piles, the threshing floor). An empty capacity cell next to
  /// this flag means "by area", not "not written down yet" — the design db
  /// carries the flag for exactly that reason.
  std::uint8_t capacity_by_plot = 0;

  /// 0/1: the chairman may pause a unit of this type — the production and
  /// livestock classes of unit_types.csv (units rules §5, «Производственный
  /// юнит можно остановить»; its own example is a milking). A school, a
  /// house or a road is not stopped by a pause: kPauseUnit on one is refused.
  std::uint8_t pausable = 0;

  /// The resources this type is the declared home of (resource_stores.csv:
  /// the row names the type as its `unit` or as its `storage`). Asked of an
  /// outline store only — a heap under the open sky takes what the table
  /// says it keeps, empty or not (IsHomeOf, stock_ops.h).
  std::vector<ResourceId> home_of;

  /// @brief Kilograms this type stores at the level a unit STANDS at.
  /// @param level The unit's own level; 0 is a construction site, which
  ///        stores nothing for anybody (unit_state.h, task A2).
  /// @return 0 when the ladder names no capacity for that level.
  float StorageCapacityKgAt(std::uint8_t level) const {
    const std::size_t index = static_cast<std::size_t>(level) - 1;
    return level >= 1 && index < level_storage_capacity_kg.size() ? level_storage_capacity_kg[index]
                                                                  : 0.0F;
  }

  /// @brief Heads of livestock this type houses at the level a unit stands
  ///        at; 0 when the ladder names none. See StorageCapacityKgAt.
  float LivestockCapacityHeadAt(std::uint8_t level) const {
    const std::size_t index = static_cast<std::size_t>(level) - 1;
    return level >= 1 && index < level_livestock_capacity_head.size()
               ? level_livestock_capacity_head[index]
               : 0.0F;
  }
};

struct FarmingConfig {
  float fertility_neutral = 50.0F;

  /// THE MEADOW'S FLOWERING WINDOW, 0-based months: MAY THROUGH JULY.
  ///
  /// No longer this core's ASSUMPTION. Decided by boss on 2026-09-06 and
  /// written down in manual/design/world/map/terrain.md, and the argument is
  /// the haymaking, not the botany: the cut is June-July (farming design,
  /// season table), and THE WINDOW ENDS WHERE THE CHOICE ENDS. An early cut
  /// breaks off two months of nectar, a late one gives the meadow away
  /// almost whole — and August adds nothing to that choice, because by then
  /// the meadow is either mown or standing in aftermath and seed.
  ///
  /// The core counts months 0..11 and the layer's own yard-flower rows count
  /// 1..12, so MAY IS 4 HERE AND 5 THERE. That is the very case the `reader`
  /// column was added for; the yard is the layer's and flowers into
  /// September, which makes a butterfly in August a sign of a YARD.
  std::uint8_t flower_from_month = 4;

  std::uint8_t flower_to_month = 6;

  /// Plowing and harrowing norms in GAME man-days per hectare: one norm for
  /// any land and any crop (farming design §5). Same conversion as the crop
  /// norms above.
  float plow_days_per_ha = 10.0F / kRealDaysPerGameDay;

  float harrow_days_per_ha = 3.0F / kRealDaysPerGameDay;

  float manure_norm_kg_per_ha = 20000.0F;

  float manure_fertility_bonus = 6.0F;

  float fallow_recovery = 6.0F;

  /// The month a fallow field is ploughed, 0-based (farming design §7:
  /// "fallow is ploughed" — with the manure, which is the point of it).
  /// ASSUMPTION: after the spring sowings are in.
  std::uint8_t fallow_plow_month = 4;  ///< May.

  float repeat_penalty_per_year = 3.0F;

  /// The repeat penalty stops growing after this many years in a row (boss
  /// answer to question Q3, 2026-08-31): "the second year is felt, the
  /// third is hard" — and there it ends. Uncapped, the penalty took a
  /// monocropped field from 65 fertility to 2 in six years WITH manure, so
  /// a rotation mistake cost the game rather than the year.
  float repeat_penalty_max_years = 3.0F;

  /// Fertility never falls below this: an exhausted field bears little but
  /// it bears (same answer). No arrangement of crops turns ploughland into
  /// desert. ASSUMPTION on the value — the rule is canon, the number is
  /// polish.
  float fertility_floor = 20.0F;

  // -- meadows (boss answer to question Q6, 2026-08-31) --------------------
  /// Hay off a hectare of grassland for the whole season's cuts, kilograms.
  /// Canon numbers, not knobs: natural meadow 1500, floodplain 2500. A
  /// meadow has no fertility and no crop row — grass is mown where it grew
  /// (land_state.h, LandKind).
  float meadow_yield_kg_per_ha = 1500.0F;

  float meadow_floodplain_yield_kg_per_ha = 2500.0F;

  /// Mowing, GAME man-days per hectare (the table keeps the design's 8 REAL
  /// man-days, 49-simulations §2).
  float meadow_mow_days_per_ha = 8.0F / kRealDaysPerGameDay;

  /// The month the scythes go out, 0-based. One cut a year carrying the
  /// season's whole yield: the core does not model a second cut, and the
  /// yield above is the season's total precisely so that it need not.
  std::uint8_t meadow_cut_month = 5;  ///< June.

  /// Drought threshold on the DAY temperature (heat design: "above +25 is
  /// heat"). Until the diurnal swing existed this read the daily MEAN, whose
  /// summer ceiling is 24 — so in every run before 2026-08-31 the drought
  /// branch was dead and no field ever burned.
  float drought_temp_c = 25.0F;

  float stress_per_day = 0.02F;

  float stress_cap = 0.3F;

  /// THE PRICE OF A LATE SOWING, per game day past the end of the crop's
  /// sowing window, and it is a SLOPE rather than a coin (boss, 2026-09-13).
  ///
  /// The price used to be the snow — a total loss — and that was wrong not in
  /// size but in kind: nobody CHOOSES to sow late. The chairman hands out
  /// rotations; when to sow is the accountant's arithmetic, and it sows
  /// whatever it can whenever it can. A full price on a band nobody entered is
  /// a trap, not a price (measured: the first year sowed 66.5 ha and lost 28
  /// of them to snow). A diminishing return puts the decision back where one
  /// actually exists — upstream, in how much land to raise.
  float late_sowing_yield_loss_per_day = 0.06F;

  /// The floor the slope above may not go under, and it is STRICTLY ABOVE
  /// ZERO: a field sown late gives less, never nothing. Nothing is what a
  /// field gives when it cannot ripen at all, and such a field is not sown.
  float late_sowing_yield_floor = 0.35F;

  /// How many CONSECUTIVE growing days of one kind of weather it takes
  /// before the field says out loud that it is drying or soaking
  /// (FieldWeatherState). The design gives the phenomenon — "long heat
  /// without rain", "drawn-out rains" — and leaves the count to the run,
  /// which is what this row is: tables/farming.csv, weather_state_days.
  ///
  /// Chosen by sweeping the thirty-year run rather than by taste, and the
  /// rule of choice is written down: THE LARGEST VALUE AT WHICH BOTH HALVES
  /// STILL HAPPEN. A threshold at which one of them never fires turns half
  /// the state into decoration and then reports itself healthy by being
  /// silent.
  ///
  /// Re-swept once the weather gained memory, because the answer depended on
  /// the source and not on the field: over 2400 growing field-days,
  /// 3 days -> 1.17% drying / 3.08% soaking; 4 -> 0.29% / 1.38%;
  /// 5 -> 0% / 0.58%. Before memory the same sweep gave three days, with
  /// drought already dead at four. The threshold moved because the generator
  /// started producing spells, not because it was fitted to an answer.
  /// See manual/balance/69-reconciliation.md §13.10 and §13.11.
  ///
  /// AND THE RULE OF CHOICE DIED WITH THE SINGLE NUMBER, 2026-09-05. "The
  /// largest value at which both halves still happen" is a good rule for ONE
  /// threshold and only for one: it makes each half's frequency hostage to
  /// the other's. The design wants them at different frequencies — a dry
  /// summer once in five to eight years, a waterlogged one once in two or
  /// three, because drowning is the worse of the two and strikes twice, at
  /// the yield and at the calendar. One number cannot be set to two
  /// frequencies.
  ///
  /// THE ROW IS KEPT AS THE FALLBACK OF BOTH and will be removed from
  /// farming.csv when the two below have their own rows: while it is the
  /// only one present, the two spells behave exactly as they did.
  float weather_state_days = 4.0F;

  /// Consecutive HOT days before a field says it is drying. The design's
  /// drought is "затяжные +25…+30 в июне–июле" and the frequency it names is
  /// a dry summer once in five to eight years — a rarity, but one a player
  /// lives to see twice.
  ///
  /// Optional in tables/farming.csv under `drought_spell_days`; absent, it
  /// stands at `weather_state_days` and nothing changes. THE NUMBER IS NOT
  /// SET HERE: it comes from a measurement over YEARS, because a drought is
  /// a property of a summer and not of a day — one dry summer in thirty and
  /// a dozen dry weeks scattered over all of them give the same share of
  /// field-days and are two different worlds.
  float drought_spell_days = 4.0F;

  /// Consecutive RAIN days before a field says it is soaking. Same shape,
  /// different frequency: once in two or three years.
  ///
  /// Optional in tables/farming.csv under `wet_spell_days`.
  float wet_spell_days = 4.0F;

  // -- stage 6: herd-wide knobs (tables/farming.csv scalar rows) -----------
  /// Underfeeding: produce multiplier while unfed, and when deaths start.
  /// ASSUMPTION until the feeding runs.
  float unfed_produce_factor = 0.5F;

  float unfed_death_after_days = 8.0F;

  float unfed_death_percent_per_day = 5.0F;

  // -- stage 6: feeding by feed units (question 133) -----------------------
  /// Juveniles eat this share of the adult norm (canon: one half).
  float juvenile_feed_factor = 0.5F;

  /// Feed units from reserve rows (FeedLinkDef::reserve) count into the
  /// covered need with this multiplier — the design says "reserve with a
  /// lowered effect" but names no number. ASSUMPTION until the balance pass
  /// and polish question P22n, which asks whether one multiplier can stand
  /// for three different penalties at all.
  float reserve_feed_factor = 0.8F;

  /// THE STOCK LIGHTS' MARGINS, in game days: how much slack "enough with
  /// room to spare" means before a light goes green (office design §5, the
  /// stock traffic light). They are separate numbers on purpose — a bad
  /// winter and a sowing norm are not the same kind of risk, and one shared
  /// threshold would be a wrong answer to one of the two.
  ///
  /// ASSUMPTION until the balance pass. A yellow light that is yellow
  /// always is noise and stops being seen inside a week; if that happens
  /// these are what is wrong, not the player.
  float feed_light_margin_days = 12.0F;  ///< A season of slack on the fodder.

  /// The seed light measures COVERAGE, not days, so its margin is a SHARE
  /// on top of a whole covering: 0.1 means green wants a tenth in hand.
  /// Seed is spent all at once and "days of seed" does not exist (boss,
  /// 2026-09-04) — a margin in days would have been a slack on a number
  /// that is not there.
  float seed_light_margin_share = 0.1F;

  /// What the ploughing and the sowing are multiplied BY when the working
  /// stock has had none of its fodder grain, 0..1 (farming.csv
  /// traction_hungry_factor). Full fodder is 1.0 and the scale is linear
  /// between; the norm is divided by it, so 0.7 makes the work half again
  /// as long.
  ///
  /// A DESIGNER'S NUMBER AND NOT A MEASURED ONE, and boss says so in the
  /// document too: it is the price of unsealing the fodder fund, and the
  /// price has to be visible in the sowing calendar without being a wall.
  /// Zero switches the whole rule off, which is what a table set that has
  /// never heard of traction gets.
  float traction_hungry_factor = 0.7F;

  /// What share of the working stock's need counts as a FULL work ration,
  /// 0..1 (farming.csv traction_full_ration_share). The traction ration is
  /// measured against this and not against the whole need.
  ///
  /// BECAUSE A HORSE ON GRAIN ALONE IS NOT A HORSE. The agronomy bands the
  /// runs check against were measured on a model with no traction rule at
  /// all — that is, on a settlement whose horses were fed normally — so a
  /// multiplier laid over them counts the horse twice unless its 1.0 means
  /// the NORMAL farm rather than an impossible one. Livestock design §11:
  /// "больше половины нормы им не закроешь — остальное сено".
  ///
  /// Not derived from the roster's own caps, and that is worth saying: they
  /// sum to 2.2 of the need for a horse (oats .5, barley .4, compound .4,
  /// three bread grains .3 each), so any sum of them clamped by the need is
  /// just the need again and the number would go back to meaning the
  /// impossible farm. The achievable share is a DESIGN statement about what
  /// a working animal actually eats, so it lives in the balance table where
  /// a design decision can reach it.
  float traction_full_ration_share = 0.5F;

  /// The pasture season, 0-based months inclusive: outside it a head takes
  /// nothing from the grass and its whole norm comes out of the stores.
  /// ASSUMPTION — the design names summer grazing but no month band.
  std::uint8_t pasture_from_month = 4;  ///< May.

  std::uint8_t pasture_to_month = 8;  ///< Inclusive: September.

  // -- stage 6: billeting (livestock design §6, boss answer 2026-08-30) ----
  /// A head with no room under the roof is BILLETED at private yards, never
  /// slaughtered. It stays kolkhoz property — the milk is the farm's — and
  /// the farm sees one number, not an allocation per household. Billeting
  /// is paid for in leakage: this is the produce multiplier on the billeted
  /// share of the herd. ASSUMPTION.
  float billet_yield_factor = 0.6F;

  /// HOW MANY HEAD OF KOLKHOZ STOCK ONE YARD CAN BILLET (world_params.csv
  /// `billet_heads_per_yard`, source `measurement`). Until 2026-09-16 there
  /// was no ceiling at all, and a village of twenty-one households could
  /// hold five hundred horses without anything saying otherwise.
  ///
  /// THE NUMBER IS MEASURED, NOT CHOSEN. Across the three arms of idle_curve
  /// the model never billets more than 0.95 head per yard, and in a played
  /// village billeting ENDS in the second year, as soon as a roof goes up —
  /// so this is a ceiling on a start condition, not a running cost. One head
  /// per yard would have left the start's twenty-one head in twenty-one
  /// places with no room to buy a single horse: the ceiling would have closed
  /// the very way out of the deadlock it was written beside. Two doubles it.
  float billet_heads_per_yard = 2.0F;

  /// The school year, 0-based months (world_params.csv, human 1..12 there).
  /// Production reads them for ONE question: whether the children who keep
  /// the night pasture are on holiday.
  ///
  /// THE YEAR ENDS ON THE FIRST DAY OF ITS LAST MONTH, which is how every
  /// rung in this project is counted, so a year of 9..6 means school runs to
  /// the end of MAY and the holidays are June, July and August. Read the
  /// number and not its header and you get two months instead of three — and
  /// then the night pasture's window is wrong by a third.
  std::uint8_t school_year_start_month = 8;  ///< September.
  std::uint8_t school_year_end_month = 5;    ///< June — the year ends as it opens.

  /// The band of children the night pasture is kept by, and the acceleration
  /// that turns campaign days into their years (world_params.csv
  /// `age_school_senior_from_years` and `age_adult_from_years`, both declared
  /// for `both` readers; life.csv `life_speedup`).
  ///
  /// SECOND READERS, NOT SECOND HOMES. core_labor reads the same speedup row
  /// and core_residents the same bands; production asks the same rows rather
  /// than keeping numbers of its own, which is the only way the three cannot
  /// drift.
  ///
  /// AND THE BAND IS WHY THE CHECK IS NOT "IS HE ENROLLED": a village with no
  /// school has no enrolled pupils at all, so an enrolment test would smuggle
  /// in a school BUILDING as a condition of the night pasture — a condition
  /// the design does not name, and the kind of false gate this project spent
  /// two days marking.
  float senior_school_from_years = 11.0F;
  float adult_from_years = 16.0F;

  /// How much of a producing unit's output is lost at FULL wear, 0..1 — the
  /// loss grows linearly from the first per cent (unit rules §15, «производ-
  /// ственный юнит — падает производительность»; world_params.csv
  /// wear_output_loss_at_full).
  ///
  /// NO DEAD ZONE, AND THE DESIGN SAYS SO BY OMISSION. «До половины шкалы
  /// только вид» is the row about MACHINERY, which the same table lists
  /// separately and which this core does not have at all; a building's line
  /// carries no threshold, so neither does this.
  ///
  /// HALF AND NOT ALL. A worn shed is a bad shed, not a stopped one — «a ruin
  /// still works», and the only thing in the game that falls down from wear
  /// is the start's old house.
  float wear_output_loss_at_full = 0.5F;

  /// world_params.csv `gather_alarm_horizon_days` — STUB 4 (boss seq 161 А):
  /// the harvest-will-not-be-gathered alarm judges only the fields in the
  /// reaping and those whose reaping opens within this many days. «Пока
  /// игрок ещё может успеть, а не за месяц»: a field three weeks from ripe
  /// was judged by the June pace, when the village's hands are on other
  /// work, and the alarm cried for 150 t that the autumn reaped (host, seq
  /// 35). WHEN THE STUB COMES OFF (boss seq 161): when a run shows that at
  /// this horizon the alarm lights, on every seed, before the debt is beyond
  /// every hand the village has — the player can still make it.
  float gather_alarm_horizon_days = 4.0F;

  /// world_params.csv `gather_alarm_snow_day` — STUB 40, a day of the year:
  /// the harvest-will-not-be-gathered alarm counts the days to THIS snow and
  /// not to the climate's mean edge (growing_season_last_day), whichever is
  /// earlier. «Тревога Эпохи I считает до РАННЕГО снега» (econ, no-forecast
  /// proposal §3; boss, boss-core-epoch1-resume): the real snow is a coin
  /// against −1, and with no three-day forecast to warn him the chairman
  /// needs the early edge. THE NUMBER IS A MEASUREMENT: econ's P10 of the
  /// first snow in the reaping season from host's 2000-year probe — snow
  /// already down by day 40 in 17.1 % of years, never before day 38; the
  /// mean's edge, day 41, in 28.4 %. Counting to 40 the alarm is late by at
  /// most two days in a few per cent of years, instead of crying three days
  /// early in nearly all of them. WHEN THE STUB COMES OFF: when the climate
  /// table moves, this is re-read off the same probe, not guessed.
  ///
  /// THE FIRST DAY THE SNOW LIES, NOT THE LAST DAY THAT COUNTS (econ,
  /// econ-boss-snow-edge-reading, adopted by boss): the probe's P10 is of
  /// the first kSnow day, which takes a standing field on its morning, so
  /// the alarm counts up to the day BEFORE it — 39 at 40. 0.34.16 read it as
  /// the last day counted and was a day late; the value stayed, the reading
  /// moved (0.34.17). Unlike growing_season_last_day, which IS a last safe
  /// day.
  ///
  /// ONLY THE ALARM. The queue's last days before the snow keep the mean's
  /// edge: the alarm is a warning to the chairman, the queue is the
  /// accountant's order of work, and econ asked for the first.
  float gather_alarm_snow_day = 40.0F;

  /// world_params.csv `field_heap_keeping_factor` — 0.33, econ's number
  /// accepted by boss (econ-boss-field-heap-2026-09-19): a reaped heap waiting
  /// on its field for a cart keeps a third as long as the same produce in a
  /// store — spoil_days × the store's keeping_factor × this. Until 2026-09-19
  /// the heap did not rot at all, and a field was a free store with no loss
  /// (host: 139.6 t of vegetables lying out a whole autumn). Grain in the heap
  /// too: rain on a field rots it. The settled snow still takes what is left.
  float field_heap_keeping_factor = 0.33F;

  /// world_params.csv `mud_speed_factor` — 0.5, econ's number accepted by
  /// boss (seq 182; econ/manual/proposals/mud-season-links.md): on a day of
  /// РАСПУТИЦА (WeatherState::mud) a cart or a carrier hauling a load goes at
  /// this share of its speed, and a district lot ordered that day takes its
  /// base term divided by it. STUB until host's measure: with a clamp by the
  /// field the chairman must lose at most 8 % of any plan crop in the heaps,
  /// or this rises to 0.7. STUB too: no exemption on gravel — the haul is
  /// measured in a straight line and the core knows no route.
  float mud_speed_factor = 0.5F;
  float life_speedup = 4.0F;

  /// life.csv `adult_age_years`, read beside `life_speedup` from the row
  /// labor reads it from: who counts as a hand when the harvest alarm has no
  /// season's pace yet (production_alarms.cpp).
  float adult_age_years = 16.0F;

  /// The month the autumn pig slaughter falls in, 0-based. Everything but
  /// the sows and the sire goes to meat then (livestock design §6).
  std::uint8_t pig_slaughter_month = 9;  ///< October.

  /// Share of the adult pigs kept over the winter as sows. The design names
  /// the rule ("everything but the sows") but not the number, so this is the
  /// one knob the rule needs. ASSUMPTION until the balance run.
  float sow_keep_share = 0.25F;

  /// The calving season, 0-based months inclusive. Births are once a game
  /// year in their own season (canon of the yearly cycle); livestock.csv
  /// gives the yearly rate but names no month, so the band is ASSUMPTION and
  /// the rate is spread inside it — the yearly total is what the balance
  /// eats, and it holds either way.
  std::uint8_t birth_from_month = 2;  ///< March.

  std::uint8_t birth_to_month = 4;  ///< Inclusive: May.
};

/// The district's ambulance (district_car.h; boss seq 210, register 236).
/// world_params.csv keys, read with these defaults until the base carries
/// them — every one a STUB of boss's decision, except where noted.
struct DistrictCarConfig {
  /// `ambulance_health_line` — health below this is "grave" and the district
  /// sends its car. STUB 15 (boss seq 210, 1).
  float health_line = 15.0F;

  /// `hospital_days` — days in the district's hospital. STUB 8, two months
  /// of the game (boss seq 210, 3).
  float hospital_days = 8.0F;

  /// `hospital_return_health` — the health he comes home with. STUB 60.
  float return_health = 60.0F;

  /// `ambulance_arrive_hour` — the hour the car stands at the house: the
  /// morning after it was sent, the district's working morning like the
  /// trip's departure. STUB 8.
  float arrive_hour = 8.0F;

  /// `walk_home_hours` — out of the milk cart's season he walks in from the
  /// district's border, and these are the last hours of his absence, drawn
  /// on the road. STUB 4 — core's own number, not boss's: the border is
  /// ~6 km off and a convalescent walks it slowly.
  float walk_home_hours = 4.0F;
};

struct ProductionConfig {
  /// The district's ambulance (district_car.h).
  DistrictCarConfig district_car;

  /// The last day of the year a standing crop is safe from the snow — the
  /// physical end of the growing season, handed in by the assembly from
  /// ITimeSystem::GrowingSeasonLastDay because the seasonal curve is
  /// core_time's and must not be interpolated twice.
  ///
  /// NOT PARSED FROM A TABLE and deliberately absent from ParseProductionConfig:
  /// it is derived from the weather table rather than written in one, and a
  /// row of its own would be a second home that goes stale the day the climate
  /// moves. The default changes nothing.
  std::uint32_t growing_season_last_day = kDaysPerYear - 1U;

  /// The climate's share of rain days per day of the year, handed in by the
  /// assembly from ITimeSystem::ClimateRainDayShares on the season edge's
  /// terms: derived, not parsed, and all zeros by default — rain stopping
  /// nothing ahead of the clock, the alarm as it was.
  RainDayShares rain_day_shares{};

  std::vector<CropDef> crops;  ///< Indexed by CropId row.

  std::vector<LivestockDef> livestock;  ///< Indexed by LivestockKindId row.

  std::vector<UnitTypeDef> unit_types;  ///< Indexed by UnitTypeId row.

  /// 1 when resource_stores.csv was read and UnitTypeDef::home_of is the
  /// answer; 0 in a table set without it (a hand-built test world), where an
  /// outline store's home is where the resource already lies (STUB, the rule
  /// before the table reached the core).
  std::uint8_t resource_stores_read = 0;

  FarmingConfig farming;

  /// Dense by ResourceId: feed value in kilogram feed units per kilogram
  /// (oat = 1.0); 0 = the resource is not feed. Source: the resources.csv
  /// feed_value column (design db resource.feed_value, question 133).
  std::vector<float> feed_values;

  /// Dense by ResourceId: kilocalories per gram, off food.csv's
  /// kcal_per_gram column (the file core_residents parses for the table);
  /// 0 = not a food. Read here for one question only — what a tonne over the
  /// plan weighs in grain (PlanOverfulfilGrainTonnes) — and read off the same
  /// column, so the two readings cannot tell different calories.
  std::vector<float> food_kcal_per_gram;

  /// Feeding-order rows in file (= priority) order; see FeedLinkDef.
  std::vector<FeedLinkDef> feed_links;

  ResourceId manure_resource;  ///< resources.csv "manure" row.

  ResourceId hay_resource;  ///< resources.csv "hay" row.

  ResourceId straw_resource;  ///< resources.csv "straw" row: the grain's by-product.

  // -- stage 6: where herd produce lands (invalid = kind yields none) ------
  ResourceId milk_resource;  ///< resources.csv "milk" row.

  ResourceId egg_resource;  ///< resources.csv "egg" row.

  ResourceId wool_resource;  ///< resources.csv "wool" row.

  ResourceId meat_resource;  ///< resources.csv "meat" row.

  ResourceId hide_resource;  ///< resources.csv "hide" row.

  ResourceId pelt_resource;  ///< resources.csv "mink_pelt" row.

  ResourceId down_resource;  ///< resources.csv "down" row.

  UnitTypeId compost_heap_type;  ///< unit_types.csv "compost_heap".

  /// The two stores kEmptyStore empties (start §5; registers 214 and 233):
  /// unit_types.csv "church_store" and "clamp". Neither has a post, so both
  /// hold the store leak open while they hold raw material.
  UnitTypeId church_store_type;
  UnitTypeId clamp_type;

  /// resources.csv `theft`, dense by ResourceId: 2 eager, 1 some, 0 none or
  /// not yet written. The perevalka carries what is stolen more readily
  /// before the rest (start §5, «сначала то что портится и то что
  /// воруют»). WHAT IS STOLEN, not what samogon is made of: the distiller's
  /// raw material is core_residents' own list (boss seq 117).
  std::vector<std::uint8_t> theft_rank;

  /// unit_types.csv "stable": the closed housing horse breeding requires
  /// (boss rules 2026-08-29 §2.2); other kinds breed under any roof.
  /// The kolkhoz yard, whose SECOND step is the stable. Foals come only
  /// under a roof, so a team with no built stable ages out and takes the
  /// ploughing with it — which is a deadline, not a balance
  /// (livestock design §5, boss answer 2026-08-31).
  UnitTypeId stable_type;

  /// unit_types.csv "field_camp": where the district MTS's column camps
  /// (mts_column.cpp). Invalid = the tables have none, and no column arrives.
  UnitTypeId field_camp_type;

  /// The farm office (`farm_office`). Read by one rule only: electrification
  /// will not come to a farm that has no address for the district to write to
  /// (district_limit.h, RunEraEvents).
  UnitTypeId farm_office_type;

  LivestockKindId horse_kind;  ///< livestock.csv "horse": the only kind the stable gates.

  /// professions.csv "groom" — the post that ends the start's horse
  /// arrangement (livestock design §5, task A7). The herd day recognises
  /// the groom by this id and nothing else: the post itself is core_labor's
  /// business, but a profession key is DATA, and reading a table is not a
  /// dependency on the module that also reads it (manual/74-posts.md §5).
  /// Invalid when the roster is missing, and then no team is ever gathered.
  ProfessionId groom_post;

  // -- what a haul costs (task A4; transport design §1, §2; haul.h) --------
  /// The SAME five numbers labor reads, out of the SAME two tables
  /// (transport.csv, labor.csv), and read twice on purpose. Production
  /// sizes the hauling demand it writes into a field's work seam; labor
  /// drains that seam with real people. **The price of a trip has to be the
  /// same on both sides of a seam**, and the way it is kept the same is that
  /// the arithmetic lives in one place — core_common/haul.h — and only its
  /// inputs are read twice. A number is data; two modules reading one table
  /// is not a dependency (the same argument as the groom's key above).
  ///
  /// The demand is sized for a REFERENCE carrier, and that is exact where it
  /// matters: a cart takes its 750 kg whoever leads the horse. On foot the
  /// spread between a strong man and a frail one shows up in what each is
  /// PAID for the day, not in the tonnage the field expects — a named
  /// simplification, not an oversight.
  float cart_load_kg = 750.0F;

  float carry_kg_adult = 20.0F;

  float walk_speed_kmh = 5.0F;

  float harness_speed_kmh = 12.0F;

  float standard_day_hours = 10.0F;

  /// labor.csv `travel_limit_hours` and `min_usable_hours`: the accountant's
  /// road rule, read here for kFellingUnreachable (timber_felling.h) so that
  /// the alarm asks the assignment's own question (parcel 308).
  float travel_limit_hours = 4.0F;

  float min_usable_hours = 1.0F;

  // -- shelf life (task A4; transport design §10) --------------------------
  /// Game days a resource keeps, by ResourceId — resources.csv `spoil_days`,
  /// empty for what does not go bad. Read here for the units' stores;
  /// core_residents reads the same column for the larders, and the rule
  /// itself is shared (core_common/spoilage.h) so the two cannot drift.
  std::vector<float> spoil_days;

  /// Multiplier on every shelf life: a cellar, an ice house, a frost.
  /// STUB at 1.0 — none of them is in the vertical slice, and the knob is
  /// here so that the day one arrives there is a place to put it.
  float keeping_factor = 1.0F;

  LivestockKindId pig_kind;  ///< livestock.csv "pig": the only kind with an autumn slaughter.

  /// The share of a NORMAL yield off the worked arable that the district
  /// expects (campaign.csv plan_grain_share_percent, v4 anchor 28).
  ///
  /// THE SHARE IS THE SAME NUMBER IT ALWAYS WAS; what changed on 2026-09-12
  /// is what it is a share OF. It used to be a share of the year's actual
  /// reaping, accrued as the crop came in — so what was owed WAS what had
  /// been cut, every year was met by construction, and a verdict on such a
  /// plan could not fail. Now it is a share of what the land that was worked
  /// last year SHOULD give at a normal yield: the norm stands whatever the
  /// weather does, and a poor year no longer forgives itself (boss's
  /// decision of 2026-09-12; district design §9).
  ///
  /// Two consequences hold today, and one was claimed and does not: sowing
  /// less does not owe less, because the norm is off worked land and not off
  /// sown land, and the figure is knowable in spring and does not move
  /// again. What does NOT hold is "raised ground enters the plan the year
  /// after": the reading happens after the year's rotation shift, so it sees
  /// this year's slot, and the core remembers no earlier year. District
  /// design §9 asks for it; production_system.cpp AnnouncePlan carries the
  /// same note beside the code.
  float plan_grain_share = 0.0F;

  /// One position of the district's plan: which crop, and what share of the
  /// WORKED arable the district counts against it (campaign.csv
  /// plan_positions, "crop_key=percent").
  struct PlanPosition {
    CropId crop;

    /// 0..1. The shares do not add up to one and are not meant to: the rest
    /// of the land grows crops the start's plan does not ask for.
    float area_share = 0.0F;
  };

  /// THE POSITIONS THE DISTRICT ASKS BY, and the area it counts under each.
  /// District design §9 says only what is GROWN goes into a plan, and names
  /// grain by crop, potatoes and vegetables, flax, milk, meat, egg and wool;
  /// the start's plan is the short list of them. (Said in my own words rather
  /// than quoted: the document is in Russian, and a translation inside quote
  /// marks is a promise the reader cannot check against the file.)
  ///
  /// THE NORM IS OFF THE POSITION AND OFF THE AREA, NOT OFF THE SLOT, which
  /// is the whole point: a norm computed from what the chairman planted is a
  /// norm the chairman sets, and the district does not ask what he planted.
  /// Empty means no plan at all, which is how a table-less world and every
  /// unit test keep working — and PlanState::announced, not the tonnage, is
  /// what tells that apart from a year the district asked nothing of.
  std::vector<PlanPosition> plan_positions;

  /// THE FIRST PLAN IS OFF THE START STOCK, NOT OFF THE ARABLE (boss, parcel
  /// 399; district design §9, "Первый план считается от стартового запаса").
  /// The start's arable is derelict — nobody ploughs 160 ha in the first year
  /// and no winter rye stands in the first spring — so the first year's norm
  /// of a position is this share of the start stock of the position's produce,
  /// and a position with no start stock is not in the first plan. From the
  /// second year the ground worked in the year before. campaign.csv
  /// `first_plan_start_stock_percent`; STUB figure, boss's to set.
  float first_plan_start_stock_share = 0.2F;

  /// Grams of each resource in the start stock (start_stock.csv, summed over
  /// the places it lies), dense by ResourceId — read for the first plan only.
  ResourceAmounts start_stock;

  /// The share of the plan that counts as met, 0..1 (campaign.csv
  /// plan_met_share). It was 1.0, the plain reading of "сорванный план"
  /// (epochs design §8), until seed 5 failed a year six kilograms of rye
  /// short of 1.116 t; 0.99 since 2026-09-19 (boss seq 89). The +150 for a
  /// plan delivered IN FULL does not read it and still asks 100 %.
  float plan_met_share = 0.99F;

  /// THE MILK POSITION'S SHARE (district §9, «Молоко — в плане с первого
  /// года»; register 231; campaign.csv `plan_milk_share`): the share of the
  /// kolkhoz's milking herd's day, at the herd's own factor on the day of
  /// the announcement, that the district's cart takes against the position.
  /// STUB 0.5, boss's. 0 puts no milk in the plan.
  float plan_milk_share = 0.5F;

  /// How many failed years in a row make the "Под суд" condition (epochs
  /// design §8: "три сорванных плана подряд"). campaign.csv.
  std::uint8_t plan_failed_years_to_trial = 3;

  /// What a met and a failed year do to the chairman's raikom reputation,
  /// in points of its 0..100 scale (campaign.csv). District design §5 gives
  /// the DIRECTIONS — "растёт: выполнение и перевыполнение плана", "падает:
  /// срыв плана" — and no magnitudes; these are an ASSUMPTION living in the
  /// table where a balance pass can reach them.
  float plan_met_reputation = 4.0F;

  float plan_failed_reputation = -8.0F;

  /// Timber design §8a: the stands and the felling numbers (2026-09-13).
  TimberCatalog timber;

  /// Production units §8а: the shops' recipes, the barrels and the room a
  /// resource takes in a store (2026-09-19).
  ProcessingCatalog processing;

  /// Construction design §3: the plots clay, stone and sand are dug on and
  /// the digging numbers (boss, parcel 270).
  ExtractionCatalog extraction;

  /// District design §1, §4: the limit catalogue and the year's points
  /// knobs (2026-09-13).
  LimitCatalog limit;

  /// Characters design §2, "Эпоха I числами": the regular visits' months and
  /// notice (boss, parcel 324).
  DistrictVisitCatalog district_visits;

  /// The chairman's trip to the district in numbers (boss seq 206).
  DistrictTripCatalog district_trip;
};

/// @brief Parses every table core_production reads into `config`.
/// @param error Receives a human-readable message on failure.
/// @return false when a PRESENT table cannot be understood; a missing table
/// keeps the defaults and succeeds — a world without tables idles rather
/// than refusing to exist. Call once, at factory time.
bool ParseProductionConfig(const ITableSet& tables, ProductionConfig& config, std::string& error);

}  // namespace core

#endif  // CORE_PRODUCTION_PRODUCTION_CONFIG_H_
