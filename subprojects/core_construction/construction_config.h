// Internal to core_construction: the parsed build data.
//
// Five tables become four arrays and one scalar, all indexed the way the
// core indexes everything — by DefId, which is the row number of the table
// the id came from (core_common/ids.h). Parsed once at factory time; the
// simulation never opens a table again.
//
// The policy is every factory's: a MISSING table or key keeps the canonical
// default — a unit test's world has no tables at all, and then nothing can
// be built, which is the honest answer — while a PRESENT row that cannot be
// read or contradicts another table refuses the whole subsystem.

/// @threading PARALLEL_READONLY
/// Filled ONCE by the factory and never written again. It also CONTAINS the
/// catalogue (core_catalog/definitions.h), which hands out a std::span into
/// itself through Plots() — so this object's immutability is the lifetime
/// and the thread-safety of that span both.

#ifndef CORE_CONSTRUCTION_CONSTRUCTION_CONFIG_H_
#define CORE_CONSTRUCTION_CONSTRUCTION_CONFIG_H_

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core_catalog/definitions.h"
#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_common/stink.h"
#include "core_common/world_state.h"

namespace core {

class ITableSet;  // Defined in core_tables.

/// What opens a unit type (unlocks design §1). One gate per type, never two.
enum class UnitGate : std::uint8_t {
  kEra = 0,  ///< Its era has come. The default, and 97 of 106 types.
  kStart,    ///< Stands from day one; nothing to build, ever.
  kEvent,    ///< A way-of-life event. Closed in the core until phase III.
  kQuest,    ///< A quest. Closed until phase III.
  kUnit,     ///< A predecessor on the map. Closed until phase III.
};

/// One material line of one level's recipe, already in grams.
struct BuildMaterial {
  ResourceId resource;

  /// The recipe's amount converted once, at parse time: amount x
  /// kg_per_unit x 1000 (resources.csv). The core counts every store in
  /// grams (state model §5), the tables state each resource in its own
  /// measure, and this is the single place the two meet.
  Grams grams = 0;
};

/// One step of a unit type's ladder (unit_levels.csv, unit_level_cost.csv).
struct BuildLevel {
  /// Game man-days of the build class of THIS level, already divided by
  /// kRealDaysPerGameDay: the tables carry the agronomy books' real
  /// man-days, every consumer divides once at parse time (calendar.h).
  float labor_days = 0.0F;

  /// The build class's brigade ceiling; 0 = none named.
  std::uint8_t max_crew = 0;

  /// The era this level belongs to (1..3): an upgrade waits for its era
  /// even when the unit itself is older.
  std::uint8_t era = 1;

  /// 0/1: the class is "marking" — the player draws the outline and there
  /// is neither work nor material (build_class `plot`). Such a level is
  /// finished the moment it starts.
  std::uint8_t is_marking = 0;

  std::vector<BuildMaterial> recipe;

  /// Storage capacity of a unit STANDING at this level, in grams
  /// (unit_levels.csv storage_capacity_t). Read here and not asked of
  /// core_production because two subject-tier modules never call each other
  /// (CLAUDE.md §7): demolition has to know what a receiving store can hold
  /// (task A3), and this module already parses the very table that says so.
  /// 0 = the level stores nothing by number.
  Grams storage_capacity_grams = 0;

  /// Amortization of a unit STANDING at this level, in game years to a
  /// full wear scale (unit rules §15: "the time an object takes to reach
  /// 100 % if never repaired"): standing empty and unworked, and while a
  /// household lives in it or somebody works in it today. Read from
  /// unit_levels.csv `wear_years_idle` / `wear_years_in_use` — the build
  /// class's figures, exported per level like labor_days and max_crew, so
  /// a brick level outlasts a timber one by data and not by a rule here.
  /// 0 = the ladder names none for this level, and the unit does not wear
  /// (task A5, manual/73-wear-and-repair.md §2).
  ///
  /// IN USE IS THE SHORTER TERM. "Works — wears faster; stands — hardly
  /// ages at all", and pausing what is unused is called doubly profitable
  /// (unit rules §15). The tables carry both figures and the code only
  /// picks one; the direction is stated here because it is the half a
  /// reader gets wrong (boss corrected his own delivery criterion on it,
  /// 2026-09-03).
  float wear_years_idle = 0.0F;

  float wear_years_in_use = 0.0F;

  /// What THIS STEP does to its build class's term, on top of the type's
  /// own pace (unit_levels.csv `wear_factor`). 1.0 = the step adds
  /// nothing. Today it carries one fact for the whole roster: a timber
  /// frame on wooden stools lives shorter than the same frame on stone —
  /// 1.1 on twenty-seven first steps, and every one of them Epoch I.
  ///
  /// A SECOND FACT, NOT A SECOND NAME FOR THE FIRST. `unit.wear_factor`
  /// says what the nature of the unit does to it (a byre is damp, a mill
  /// shakes); this says what it stands on. They multiply because both
  /// answer "how much faster", and a unit that is both damp and badly
  /// founded is worse than one that is either (boss, 2026-09-05). The
  /// unit whose foundation is absent BY DESIGN takes no penalty here —
  /// its class already pays for it in the term, and one fact counted
  /// twice looks exactly like two facts.
  float wear_factor = 1.0F;
};

/// Everything the subsystem knows about one unit type. NO CAPACITY LIVES
/// HERE: the ladder holds it, step by step. The type row used to carry a
/// figure of its own, but the export wrote a copy of level 1 into it, so the
/// fallback reading it could never differ from the step it fell back from —
/// a live-looking wire that was never plugged in (boss, 2026-09-05).
struct BuildType {
  UnitGate gate = UnitGate::kEra;

  /// 0/1 from unit_types.csv: whether the player may raise it at all.
  std::uint8_t player_built = 0;

  std::uint8_t era = 1;

  /// Levels 1..N, dense: index 0 is level 1. Empty for a type with no
  /// ladder at all, which is what "cannot be built" looks like in data.
  std::vector<BuildLevel> levels;

  /// 0/1: the capacity is the outline the player draws, so there is no
  /// number and the store is never full (a heap, a stack, a trench).
  std::uint8_t capacity_by_plot = 0;

  /// The type's own pace against its class's, from unit_types.csv
  /// `wear_factor`: a stock yard, a byre and the kolkhoz yard 1.6 (damp,
  /// ammonia, animals), a water mill 1.5 (water and vibration), a sawmill
  /// 1.4. 1.0 = the class's own pace. The class gives the base, the
  /// nature of the unit corrects it (boss, 2026-09-03).
  float wear_factor = 1.0F;

  /// 0/1 from unit_types.csv `has_wear` (the design db derives it: a unit
  /// without a building has nothing to wear — unit rules §15). 0 keeps
  /// UnitRow::wear at zero for ever and refuses kRepairUnit. Missing
  /// column = 0 for every type: no data, no wear, said out loud rather
  /// than guessed from the capacity flag.
  std::uint8_t has_wear = 0;

  /// How badly this type smells at its core, from unit_types.csv `stink`
  /// (water design §4). kNone for every type the column does not name, and
  /// for every type at all when the column is absent.
  StinkStrength stink = StinkStrength::kNone;

  /// Whether the contents smell or the work does, from `stink_when`.
  ///
  /// A SECOND COLUMN AND NOT A DERIVATION. This module proposed deriving it
  /// from the unit's class, and boss answered with a measurement rather than
  /// an opinion: `manure_pile` and `silage_trench` are class `production`
  /// and smell ALWAYS — the contents smell, not the work — so the class
  /// answers wrongly for two of the thirteen sources. A derivation would
  /// have become a list of names in this file, compiling green and drifting
  /// from the table on the day a fourteenth source is added, with nothing
  /// to compare it against.
  ///
  /// Meaningless while `stink` is kNone, and the table leaves it empty
  /// exactly there.
  StinkWhen stink_when = StinkWhen::kAlways;
};

/// The subsystem's own knobs (construction.csv) and the parsed tables.
struct ConstructionConfig {
  /// Demolition costs this share of the level's build norm in labour days
  /// (construction design §12: "noticeably less than building"). ASSUMPTION.
  float demolition_labor_share = 0.25F;

  /// THE FULL RADIUS OF A STINK ZONE IN METRES, by strength — index by
  /// StinkStrength, so index 0 (kNone) is a zero radius and never used.
  ///
  /// ASSUMPTION, ALL THREE, AND THE WORD IS DELIBERATE: the design defers
  /// the radii to polish item P30be and gives no number anywhere. They live
  /// here rather than in the walk so that the day rows arrive, nothing but
  /// this struct changes.
  ///
  /// BUT THEY ARE NOT FREE, AND THE SHIPPED START MEASURES THE CEILING.
  /// The scene the game begins with holds three sources — the manure heap
  /// (strong), the cattle yard and the silage trench (medium) — and its
  /// nearest house stands 268 m from the heap. A house may not stand in a
  /// stink zone (water design §4), so a strong radius of 268 m or more
  /// makes the designed start illegal by its own rule on day one. 200 m is
  /// chosen with that 68 m of daylight, and a unit test walks the shipped
  /// tables to keep the two facts from drifting apart in silence.
  ///
  /// The medium and weak radii have no such measured ceiling today, and
  /// they are ordered rather than reasoned: a strong source reaches
  /// further than a medium one, which reaches further than a weak one.
  std::array<float, static_cast<std::size_t>(StinkStrength::kStinkStrengthCount)> stink_radius_m = {
      0.0F, 40.0F, 120.0F, 200.0F};

  /// By UnitTypeId value. Sized to the unit_types table; a type the tables
  /// do not have is simply out of range, and every lookup checks.
  std::vector<BuildType> types;

  /// Game hours of one-way travel per kilometre on foot (transport.csv
  /// `pedestrian`, divided by the clock scale). Read here because the site
  /// nobody can reach is measured in HOURS of road, and the hours need the
  /// pace; the arithmetic itself is core_common's (geometry.h) so that this
  /// module holds a number and not a second opinion.
  float walk_hours_per_km = 2.4F;

  /// The catalogue, read at parse time: the plot radii and the map side
  /// come from there and not from this module's own read of unit_types.csv,
  /// because those columns have a second reader (core_residents' wedding
  /// stub) and therefore an owner (core_catalog/definitions.h).
  Definitions definitions;

  // -- wear and repair (task A5, construction.csv) ------------------------------

  /// A repair at FULL wear costs this share of the level's build norm in
  /// labour days; scaled linearly by wear / 100 at the moment of the order
  /// (construction design §11: "less and cheaper", and "a neglected repair
  /// is dearer"). Boss's figure of 2026-09-03: a capital repair is about a
  /// third of the build — less and repair is free, more and demolishing to
  /// rebuild wins, which is not the fork the design wants.
  float repair_labor_share = 0.30F;

  /// Spare parts a repair consumes per game man-day of its labour — the
  /// only material a building's repair takes (construction design §2:
  /// "repair of buildings: spare parts only"). In pieces of resources.csv
  /// `spare_part`; the mass is that row's kg_per_unit, as everywhere.
  /// Boss's figure of 2026-09-03: a repair must run into HANDS, not into
  /// the district's quota — at 0.30 a timber barn's repair costs nine
  /// parts, which is felt and does not block.
  float repair_spare_parts_per_labor_day = 0.30F;

  /// resources.csv "spare_part": what a repair is delivered and what it
  /// consumes. Invalid = the table set has none, and every repair order is
  /// refused rather than silently free.
  ResourceId spare_part_resource;

  /// Grams in one spare part (resources.csv kg_per_unit of that row).
  /// Parts are counted in PIECES by the design and in grams by every store;
  /// this is the one number that converts, resolved once at parse time like
  /// every recipe amount.
  Grams spare_part_grams = 0;

  /// unit_types.csv "old_house": the start's houses, which begin part worn
  /// (genesis draws each in the band of construction.csv, 45..60 by the
  /// shipped figures), cannot be repaired and are the ONE type that
  /// collapses at 100 (start design §4, housing design §10). Invalid = the
  /// table set has none, and nothing collapses.
  UnitTypeId old_house_type;

  /// Game years an old house takes from its starting wear to 100 — the
  /// canon's "race of the first years: dismantle them before they fall"
  /// (start design §4). Its own figure, not the ladder's: the ladder gives
  /// a timber house decades, and these are the houses that do not get
  /// them. Boss's figure of 2026-09-03: six years would drop all twenty-one
  /// yards inside the first five, when the player has nothing to build
  /// with; twelve puts the first collapse at year eight to ten — built in
  /// time, nothing lost.
  ///
  /// The BAND those houses start in is not here: it is a start value, and
  /// genesis reads it straight from the same table. Two knobs parsed by a
  /// subsystem that has no rule for them are two knobs nobody reads, which
  /// is how construction.csv briefly said 45..60 while the code obeyed its
  /// own constants.
  float old_house_collapse_years = 12.0F;
  /// WAS THERE A has_wear COLUMN AT ALL? Without this the config cannot tell
  /// "this unit has nothing to wear" from "the table said nothing", because
  /// both arrive as has_wear = 0 — and those are two different answers to
  /// give a reader (core_common/deadline.h: kNotApplicable against kNoData).
  /// A stack is dropped from consideration for good; a missing column is a
  /// hole somebody has to fill.
  bool wear_column_present = false;
};

/// @brief Reads the five tables into `config` — and, since task A5, the
/// wear columns of two of them and the repair knobs of construction.csv.
/// @param error Receives the reason on failure, table and row named.
/// @return false when a present table is malformed or contradicts another —
///         a type that claims a plot and names neither radius nor marking
///         class, a recipe naming a resource with no mass, a buildable type
///         with no level 1.
bool ParseConstructionConfig(const ITableSet& tables,
                             ConstructionConfig& config,
                             std::string& error);

}  // namespace core

#endif  // CORE_CONSTRUCTION_CONSTRUCTION_CONFIG_H_
