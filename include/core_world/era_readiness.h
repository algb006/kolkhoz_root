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

  /// How many levels each type has, dense by type row — the LADDER.
  ///
  /// THE TRANSITION ASKS ITS LEVEL ONLY OF TYPES THAT HAVE ONE (boss, design
  /// commit 9177c6f7). Of 111 types in unit_levels.csv, 34 carry a second
  /// level and 77 carry one, and units rules §11 says what that means in
  /// words: «у остальных пока только первая, и это значит „ещё не
  /// расписано"». Requiring a level the design has not written is locking the
  /// era with its own unfinishedness rather than with the state of the farm.
  ///
  /// Read by the REGISTER and not by a list of names, so that the day a
  /// ladder is written the requirement grows by itself — which is right:
  /// more ladders means more that must be put in order.
  std::vector<std::uint32_t> ladder;
};

/// @brief The era's food-variety threshold — the top of its own era's norm,
/// 4 categories for Era I (metrics design §8). Read from the same table row
/// the food rules read, so the number keeps one home.
float ReadFoodVarietyThreshold(const ITableSet& tables, Epoch era);

/// @brief The biology factor, from the same `life` row core_residents reads.
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

}  // namespace core

#endif  // CORE_WORLD_ERA_READINESS_H_
