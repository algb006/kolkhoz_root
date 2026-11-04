/// @file
/// @brief The yard's inheritance (inheritance_checks.h).
/// @threading SINGLE_THREADED

#include "inheritance_checks.h"

#include <cstdint>
#include <iostream>

#include "core_common/herd_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "demography.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

core::HerdRow YardHerd(std::uint16_t kind, core::FamilyId family, std::uint16_t adults) {
  core::HerdRow herd;
  herd.kind = core::LivestockKindId{kind};
  herd.household = family;
  herd.household_owned = 1;
  herd.adult_count = adults;
  herd.adult_age_game_years_total = static_cast<float>(adults);
  return herd;
}

/// The heir keeps goats (kind 7) and, in one world, an emptied pig row (kind
/// 2, no head); the ended family keeps two pigs. Returns the heir's pig rows
/// after the drop: their heads and whether any is marked.
struct PigsAtTheHeir {
  std::uint32_t heads = 0;
  bool marked = false;
  std::uint32_t rows = 0;
};

PigsAtTheHeir Drop(bool heir_has_an_empty_pig_row, std::uint16_t heir_pigs) {
  core::WorldState world;
  core::FamilyRow family;
  const core::FamilyId ended = core::AppendRow(world.families, family);
  const core::FamilyId heir = core::AppendRow(world.families, family);
  core::AppendRow(world.herds, YardHerd(7, heir, 3));
  if (heir_has_an_empty_pig_row || heir_pigs > 0) {
    core::AppendRow(world.herds, YardHerd(2, heir, heir_pigs));
  }
  core::AppendRow(world.herds, YardHerd(2, ended, 2));
  core::DropFamilyIfEmpty(world, ended, heir);
  PigsAtTheHeir out;
  for (const core::HerdRow& herd : world.herds.rows) {
    if (herd.kind.value == 2 && herd.household.value == heir.value) {
      out.heads += herd.adult_count;
      out.marked = out.marked || herd.inherited != 0;
      ++out.rows;
    }
  }
  return out;
}

}  // namespace

int CheckTheYardsInheritance() {
  int failures = 0;
  const PigsAtTheHeir no_row = Drop(false, 0);
  failures += Expect(no_row.heads == 2 && no_row.rows == 1 && no_row.marked,
                     "inheritance: pigs into a goat yard with no pig row move marked");
  // THE EMPTY FLOCK (#1374's p76, Y1PLACE: eight of eight first-mixed yards).
  const PigsAtTheHeir empty_row = Drop(true, 0);
  failures += Expect(empty_row.heads == 2 && empty_row.rows == 1 && empty_row.marked,
                     "inheritance: pigs merged into the heir's emptied pig row are marked, not "
                     "taken for the heir's own kind");
  // The pair's other half: a flock with heads IS the heir's own kind.
  const PigsAtTheHeir own_pigs = Drop(false, 1);
  failures += Expect(own_pigs.heads == 3 && own_pigs.rows == 1 && !own_pigs.marked,
                     "inheritance: pigs merged into the heir's own pigs are his, unmarked");
  return failures;
}
