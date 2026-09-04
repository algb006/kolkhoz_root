// Internal to core_production: configuration parsed from the balance tables.
//
// CropDef mirrors tables/crops.csv (indexed by CropId = row), LivestockDef
// mirrors tables/livestock.csv, UnitTypeDef mirrors tables/unit_types.csv,
// FarmingConfig mirrors tables/farming.csv, FeedLinkDef mirrors
// tables/feed_links.csv (stage 6). A world without these tables
// (unit tests, early runs) gets empty rosters and the subsystem idles;
// present-but-malformed tables refuse the factory.

#ifndef CORE_PRODUCTION_PRODUCTION_CONFIG_H_
#define CORE_PRODUCTION_PRODUCTION_CONFIG_H_

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/ids.h"

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
};

struct UnitTypeDef {
  float storage_capacity_kg = 0.0F;  ///< 0 = stores nothing.

  /// Storage capacity per LEVEL, in kilograms, index = level - 1
  /// (unit_levels.csv storage_capacity_t). "A store keeps its old ceiling"
  /// until the day the level moves (unit rules §11), so the ceiling that
  /// binds is the one of the level the unit stands at — a granary is 150 t
  /// at level 1 and 300 t at level 2. Empty, or a zero entry, means the
  /// ladder says nothing for that level and the type's own figure is used;
  /// that is what keeps a table with no unit_levels.csv working exactly as
  /// before (task A3, manual/72-storage-and-alarms.md §2).
  std::vector<float> level_storage_capacity_kg;

  float livestock_capacity_head = 0.0F;

  /// 0/1: the capacity is the outline the PLAYER draws, so there is no
  /// number to read (manure heap, silage trench, hay stack, the log, stone
  /// and clay piles, the threshing floor). An empty capacity cell next to
  /// this flag means "by area", not "not written down yet" — the design db
  /// carries the flag for exactly that reason.
  std::uint8_t capacity_by_plot = 0;
};

struct FarmingConfig {
  float fertility_neutral = 50.0F;

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

  /// Half the diurnal swing per season, indexed by Season, from
  /// weather.csv temp_amplitude_c. Read here as well as by core_time: the
  /// weather state keeps one number a day by design, and the afternoon is
  /// the mean plus this. 0 when the table has no such column.
  std::array<float, 4> temp_amplitude_by_season = {0.0F, 0.0F, 0.0F, 0.0F};

  float stress_per_day = 0.02F;

  float stress_cap = 0.3F;

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

  float seed_light_margin_days = 4.0F;  ///< A month: seed is a fixed norm.

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

struct ProductionConfig {
  std::vector<CropDef> crops;  ///< Indexed by CropId row.

  std::vector<LivestockDef> livestock;  ///< Indexed by LivestockKindId row.

  std::vector<UnitTypeDef> unit_types;  ///< Indexed by UnitTypeId row.

  FarmingConfig farming;

  /// Dense by ResourceId: feed value in kilogram feed units per kilogram
  /// (oat = 1.0); 0 = the resource is not feed. Source: the resources.csv
  /// feed_value column (design db resource.feed_value, question 133).
  std::vector<float> feed_values;

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

  /// unit_types.csv "stable": the closed housing horse breeding requires
  /// (boss rules 2026-08-29 §2.2); other kinds breed under any roof.
  /// The kolkhoz yard, whose SECOND step is the stable. Foals come only
  /// under a roof, so a team with no built stable ages out and takes the
  /// ploughing with it — which is a deadline, not a balance
  /// (livestock design §5, boss answer 2026-08-31).
  UnitTypeId stable_type;

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

  /// The share of the year's grain harvest the district expects
  /// (campaign.csv plan_grain_share_percent, v4 anchor 28). The plan is
  /// "just a number" in phase 1 (plan §11): it accrues as grain is reaped
  /// and is handed over at the year's turn, with no district mechanics.
  float plan_grain_share = 0.0F;

  /// Which resources the plan counts, by key of the roster: the six bread
  /// grains. Named here rather than derived, for the same reason manure and
  /// hay are named here — the core has no notion of a resource category, and
  /// inventing one to hold six rows would be the expensive kind of guess.
  std::vector<ResourceId> plan_grain_resources;
};

/// @brief Parses every table core_production reads into `config`.
/// @param error Receives a human-readable message on failure.
/// @return false when a PRESENT table cannot be understood; a missing table
/// keeps the defaults and succeeds — a world without tables idles rather
/// than refusing to exist. Call once, at factory time.
bool ParseProductionConfig(const ITableSet& tables, ProductionConfig& config, std::string& error);

}  // namespace core

#endif  // CORE_PRODUCTION_PRODUCTION_CONFIG_H_
