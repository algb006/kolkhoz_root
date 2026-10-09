// The checks of a starved herd's death severity (herd_death_checks.h).

#include "herd_death_checks.h"

#include <cstdint>
#include <iostream>

#include "core_common/event_state.h"
#include "core_common/herd_state.h"
#include "core_common/ledger_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "herd_life.h"
#include "production_config.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

/// The severity of the kHerdDied one starving day says for a herd of ten,
/// kolkhoz or household; kEventSeverityCount when no death was said.
core::EventSeverity DeathSeverity(bool household) {
  core::ProductionConfig config;
  config.livestock.resize(1);
  config.farming.unfed_death_after_days = 0;
  config.farming.unfed_death_percent_per_day = 50.0F;
  core::WorldState world;
  core::HerdRow herd;
  herd.adult_count = 10;
  herd.unfed_days = 3;
  herd.household_owned = household ? 1U : 0U;
  core::YearLedger book;
  core::RunHungerDeaths(config, config.livestock[0], herd, core::HerdId{7}, world, book);
  for (const core::SimEvent& event : world.step_events) {
    if (event.kind == core::EventKind::kHerdDied) {
      return event.severity;
    }
  }
  return core::EventSeverity::kEventSeverityCount;
}

/// The heads hatched in one day of the season by a family's ten hens, the
/// family in a house or in a barrack (0.37.205; RunBirths).
std::uint16_t HatchedInADay(bool in_barrack) {
  core::ProductionConfig config;
  config.livestock.resize(1);
  core::LivestockDef& hen = config.livestock[0];
  hen.births_per_game_year = 4.0F;
  hen.litter_heads = 1.0F;
  hen.household_cap_heads = 12.0F;
  config.farming.birth_from_month = 0;
  config.farming.birth_to_month = 0;
  core::WorldState world;
  core::FamilyRow family;
  family.in_barrack = in_barrack ? 1U : 0U;
  core::HerdRow herd;
  herd.household = core::AppendRow(world.families, family);
  herd.household_owned = 1;
  herd.adult_count = 10;
  core::YearLedger book;
  core::RunBirths(
      config, hen, core::LivestockKindId{0}, herd, core::HerdId{7}, true, false, world, book);
  return herd.juvenile_count;
}

}  // namespace

int CheckAStarvedKolkhozHerdDoesNotInterruptAgain() {
  int failures = 0;
  // The barrack's flock (0.37.205): the same hens hatch in a yard and not in
  // a barrack. The yard's count is the instrument's own proof — a rate of
  // nought would pass the barrack's line with nothing measured.
  const std::uint16_t in_yard = HatchedInADay(false);
  failures += Expect(in_yard == 10,
                     "barrack flock: ten hens of a yard hatch ten in a day of this "
                     "season (the check's own ground)");
  failures += Expect(HatchedInADay(true) == 0,
                     "barrack flock: a family's hens do not breed in a barrack — never more than "
                     "the family brought");
  failures += Expect(DeathSeverity(false) == core::EventSeverity::kNotable,
                     "herd death: a kolkhoz herd's death of hunger is notable — its episode "
                     "interrupted once, by kHerdWentHungry");
  failures += Expect(DeathSeverity(true) == core::EventSeverity::kInterrupting,
                     "herd death: a household's herd's death of hunger still interrupts");
  return failures;
}
