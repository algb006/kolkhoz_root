/// @file
/// @brief «Недоуход»: the care curve and the horses' draught under it
/// (herd_care.h, #1443).

#include "herd_care.h"

#include "core_common/herd_state.h"
#include "herd_life.h"
#include "production_config.h"

namespace core {

float CareFactor(const FarmingConfig& farming, float floor, float coverage) {
  const float full = farming.herd_care_full_coverage;
  const float low = farming.herd_care_floor_coverage;
  if (coverage >= full || !(floor < 1.0F)) {
    return 1.0F;
  }
  if (coverage <= low || !(full > low)) {
    return floor;
  }
  return floor + ((1.0F - floor) * (coverage - low) / (full - low));
}

float CareDraughtFactor(const ProductionConfig& config, const WorldState& world) {
  float heads = 0.0F;
  float pulled = 0.0F;
  for (const HerdRow& herd : world.herds.rows) {
    if (herd.household_owned != 0 || herd.kind.value != config.horse_kind.value ||
        herd.kind.value >= config.livestock.size()) {
      continue;
    }
    const auto total = static_cast<float>(TotalHeads(herd));
    heads += total;
    pulled += total * CareFactor(config.farming,
                                 config.livestock[herd.kind.value].care_draught_floor,
                                 herd.care_coverage);
  }
  return heads > 0.0F ? pulled / heads : 1.0F;
}

}  // namespace core
