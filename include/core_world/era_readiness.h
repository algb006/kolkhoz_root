/// @file
/// @brief The yearly readiness score: eight components into two indices, six
/// blocks beside them (epochs design §6; boss, parcels 128-134).
/// @threading SINGLE_THREADED
/// Called once a year, on the FIRST day of the new year and out of the year
/// that just closed, from the sequential slot that turns the books.
///
/// WHY IT LIVES IN core_world. Every other rule lives with its configuration,
/// and this one has none of its own: it sums the yearly output of every
/// module at once — the plan from production, the families from residents,
/// the wear from construction, the wintering from both — and core_world is
/// the one module allowed to name them all. The INPUTS are booked by their
/// owners (ledger_state.h); only the weighing is here.
///
/// THE NUMBERS ARE STUB. The weights are the design's structure and will not
/// move; the thresholds and the two demographic targets are balance, marked
/// where they stand, and are tuned by runs (polish backlog P3, P4).

#ifndef CORE_WORLD_ERA_READINESS_H_
#define CORE_WORLD_ERA_READINESS_H_

#include <vector>

#include "core_common/ids.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace core {

/// @brief What this module needs to know about the unit types to score the
/// funds, the social objects and the office — read once at wiring, because a
/// table cannot change under a running campaign.
struct ReadinessCatalog {
  /// Unit types of the era's social list: class `social`, of this era, and
  /// free-standing. The design's six for Era I — school, culture house,
  /// selpo, field canteen, bathhouse, stadium — come out of the table rather
  /// than out of a list written here, so that the day a seventh is added the
  /// component follows it (epochs design §6: «берётся он из класса social
  /// базы дизайна по колонке эпохи»).
  std::vector<UnitTypeId> social_objects;

  /// The office. Its own block, and the design says why: the district looks
  /// at it first.
  UnitTypeId office;

  /// Types that may stand on the farm's books at all — everything that is
  /// not a family's own house. Kolkhoz wear is measured over these.
  std::vector<UnitTypeId> kolkhoz_types;

  /// The repair base, the other half of "its own traction or a repair base".
  UnitTypeId repair_base;

  /// The LADDER, dense by type row: the era each rung opens in, 1-based as
  /// unit_levels.csv spells it, indexed by level − 1. Empty for a type the
  /// level table does not name.
  ///
  /// THE TRANSITION ASKS ITS LEVEL ONLY OF TYPES THAT HAVE ONE (boss, design
  /// commit 9177c6f7). Of 111 types in unit_levels.csv, 34 carry a second
  /// level and 77 carry one, and units rules §11 says what that means in
  /// words: «у остальных пока только первая, и это значит „ещё не
  /// расписано"». Requiring a level the design has not written is locking the
  /// era with its own unfinishedness rather than with the state of the farm.
  ///
  /// AND ONLY THE RUNGS THE CURRENT ERA OPENS (boss, 2026-09-18; epochs §6
  /// «доведены до требуемого уровня»). This was a COUNT of rungs until that
  /// day, and the count could not tell a second rung of Epoch I from one of
  /// Epoch II: of 49.9 standing kolkhoz buildings, 19.3 had their second rung
  /// in a later era, construction refused every order for it with
  /// kGateClosed — rightly — and the block stood shut in Epoch I by
  /// construction. A transition cannot require what the next era opens.
  ///
  /// Read by the REGISTER and not by a list of names, so that the day a
  /// ladder is written the requirement grows by itself — which is right:
  /// more ladders means more that must be put in order.
  std::vector<std::vector<std::uint8_t>> rung_eras;
};

/// @brief The level the transition requires of `type` in `era`: the highest
/// rung the era has opened, counting up from the first without a gap
/// (epochs §6, «требуемый уровень»). 0 for a type with no ladder at all.
/// @param type A unit type; one past the catalogue reads as no ladder.
std::uint8_t RequiredUnitLevel(const ReadinessCatalog& catalog, UnitTypeId type, Epoch era);

/// @brief The era's food-variety threshold — the top of its own era's norm,
/// 4 categories for Era I (metrics design §8). Read from the same table row
/// the food rules read, so the number keeps one home.
float ReadFoodVarietyThreshold(const ITableSet& tables, Epoch era);

/// @brief The biology factor, through the same door core_residents reads it
/// by (core_catalog/world_conventions.h); 1 for a set without it.
float ReadLifeSpeedup(const ITableSet& tables);

/// @brief Reads the catalogue once, out of unit_types.csv. Invalid ids and
/// empty lists where the table names no such type, which is a fact the score
/// must handle and not a failure to load: a table set with no social objects
/// in it is a world where that component cannot be scored, and saying so is
/// the difference between NULL and nought.
ReadinessCatalog ReadReadinessCatalog(const ITableSet& tables, Epoch era);

/// @brief Scores the year that just closed and folds it into the runs.
///
/// PRECONDITION: called after the books have rotated, so `ledger.closed` is
/// the year being scored. Called anywhere else it would score the year that
/// has not happened yet.
/// @param food_variety_categories The era's variety threshold — 4 for Era I
///        (metrics design §8), passed in because the number belongs to the
///        food rules and not to the scorer.
/// @param life_speedup The biology factor, for the childhood bracket.
void ScoreReadiness(const ReadinessCatalog& catalog,
                    float food_variety_categories,
                    float life_speedup,
                    WorldState& current);

/// @brief The three blocks that are a STATE OF THE VILLAGE NOW, read off
/// `world` as it stands: four social objects, every kolkhoz unit at its
/// level, the office at wear 1 per cent or less. The other three fields are
/// left 0 — they are accumulated over a term and are not this function's.
///
/// WHY LIVE (boss, epoch1-next seq 27). Units rules §11 checks the office
/// «когда вопрос выносят на собрание» — the moment the player chooses. Read
/// off the year's turn, an office repaired in March opened the door only in
/// January, by when it had worn past 1 per cent again: 17 of 33 years in the
/// runs were refused on it, and a player who repaired and then ordered was
/// refused for doing exactly what the design says. The same holds for a
/// social object built in March. What is ACCUMULATED — the food over four
/// seasons, the wintering over two years, the indices over three, the
/// traction off the horse work of the year — stays on the yearly turn.
TransitionBlocks StandingBlocks(const ReadinessCatalog& catalog, const WorldState& world);

/// @brief Why the village may not go into the next era now, or kNone when it
/// may: the first unmet condition of the transition, the indices first and
/// then the six blocks in epochs §6 order (order_state.h, kAdvanceEra).
/// @param readiness As of the last year's turn: the indices and the three
///        accumulated blocks are read from it. A state never yet scored
///        (year 0) has held nothing and answers kIndicesNotHeld.
/// @param standing StandingBlocks of the world now: the office, the social
///        objects and the units at level are read from it, NOT from
///        `readiness.blocks`, which holds the same three as of the turn.
/// @param era The era the village is in. Anything but Epoch I answers
///        kNotEligible: this build has no transition past it.
OrderRefusal TransitionRefusal(const ReadinessState& readiness,
                               const TransitionBlocks& standing,
                               Epoch era);

/// @brief Settles every pending kAdvanceEra: kDone and the next era when
/// TransitionRefusal says kNone, kRefused with its answer otherwise. A
/// second order in the same step finds the era already moved and answers
/// kNotEligible.
/// Side effect: writes `current.epoch`, the one writer of it.
void ConsumeTransitionOrders(const ReadinessCatalog& catalog, WorldState& current);

}  // namespace core

#endif  // CORE_WORLD_ERA_READINESS_H_
