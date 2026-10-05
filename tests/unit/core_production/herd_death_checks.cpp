// The checks of a starved herd's death severity (herd_death_checks.h).

#include "herd_death_checks.h"

#include <cstdint>
#include <iostream>

#include "core_common/event_state.h"
#include "core_common/herd_state.h"
#include "core_common/ledger_state.h"
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

}  // namespace

int CheckAStarvedKolkhozHerdDoesNotInterruptAgain() {
  int failures = 0;
  failures += Expect(DeathSeverity(false) == core::EventSeverity::kNotable,
                     "herd death: a kolkhoz herd's death of hunger is notable — its episode "
                     "interrupted once, by kHerdWentHungry");
  failures += Expect(DeathSeverity(true) == core::EventSeverity::kInterrupting,
                     "herd death: a household's herd's death of hunger still interrupts");
  return failures;
}
