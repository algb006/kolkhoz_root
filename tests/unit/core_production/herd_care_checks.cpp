// The checks of undone herd care (herd_care_checks.h).

#include "herd_care_checks.h"

#include <cmath>
#include <iostream>

#include "core_common/herd_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "herd_care.h"
#include "production_config.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

bool Near(float a, float b) {
  return std::abs(a - b) < 0.0001F;
}

/// U, the curve (econ's gate): coverage 0.5 gives the cow's 0.6, 0.9 gives 1,
/// halfway gives the middle; a kind with no floor pays nothing.
int CheckTheCurve() {
  int failures = 0;
  const core::FarmingConfig farming;  // 0.9 / 0.5, the STUB lines
  failures += Expect(Near(core::CareFactor(farming, 0.6F, 0.5F), 0.6F) &&
                         Near(core::CareFactor(farming, 0.6F, 0.2F), 0.6F),
                     "undone care: at or below 0.5 the cow gives the floor's 0.6");
  failures += Expect(Near(core::CareFactor(farming, 0.6F, 0.9F), 1.0F) &&
                         Near(core::CareFactor(farming, 0.6F, 1.0F), 1.0F),
                     "undone care: at or above 0.9 she gives it all");
  failures += Expect(Near(core::CareFactor(farming, 0.6F, 0.7F), 0.8F),
                     "undone care: halfway, coverage 0.7, the middle 0.8");
  failures += Expect(Near(core::CareFactor(farming, 1.0F, 0.0F), 1.0F),
                     "undone care: a kind with no floor (an empty column) pays nothing");
  return failures;
}

/// The horses' draught: heads-weighted over the kolkhoz horses, the floor
/// 0.85 at 0.5; a family's horse and other kinds do not count.
int CheckTheDraught() {
  int failures = 0;
  core::ProductionConfig config;
  config.horse_kind = core::LivestockKindId{0};
  config.livestock.resize(2);
  config.livestock[0].care_draught_floor = 0.85F;
  core::WorldState world;
  core::HerdRow neglected;
  neglected.kind = core::LivestockKindId{0};
  neglected.adult_count = 2;
  neglected.care_coverage = 0.5F;
  core::HerdRow kept = neglected;
  kept.care_coverage = 1.0F;
  core::HerdRow family = neglected;
  family.household_owned = 1;
  core::HerdRow cows = neglected;
  cows.kind = core::LivestockKindId{1};
  core::AppendRow(world.herds, neglected);
  core::AppendRow(world.herds, kept);
  core::AppendRow(world.herds, family);
  core::AppendRow(world.herds, cows);
  const float draught = core::CareDraughtFactor(config, world);
  std::cout << "undone care: two kolkhoz pairs, one at 0.5, one kept - draught " << draught << '\n';
  failures += Expect(Near(draught, 0.925F),
                     "undone care: the team pulls 0.925 - half its heads at the floor 0.85");
  core::WorldState none;
  failures += Expect(core::CareDraughtFactor(config, none) == 1.0F,
                     "undone care: no kolkhoz horse, no draught price");
  return failures;
}

}  // namespace

int CheckUndoneHerdCare() {
  return CheckTheCurve() + CheckTheDraught();
}
