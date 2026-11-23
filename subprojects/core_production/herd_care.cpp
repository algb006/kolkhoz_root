/// @file
/// @brief «Недоуход»: the care curve and the horses' draught under it
/// (herd_care.h, #1443).

#include "herd_care.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "core_common/herd_state.h"
#include "herd_life.h"
#include "production_config.h"

namespace core {

float CareFactor(const FarmingConfig& farming, float floor, float coverage) {
  return CareFactorAt(farming, floor, coverage, farming.herd_care_full_coverage);
}

float FullCareCoverage(const ProductionConfig& config, const WorldState& world) {
  const AvatarDef& avatar = AvatarOf(config.avatars, world.chairman.avatar);
  const bool within = avatar.herd_full_care_max_households == 0 ||
                      world.families.rows.size() <= avatar.herd_full_care_max_households;
  return avatar.herd_full_care_threshold > 0.0F && within ? avatar.herd_full_care_threshold
                                                          : config.farming.herd_care_full_coverage;
}

float CareFactorAt(const FarmingConfig& farming, float floor, float coverage, float full) {
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

namespace {

/// A share 0..1 as a rounded percent 0..100.
std::uint8_t PercentOf(float share) {
  const float clamped = std::clamp(share, 0.0F, 1.0F);
  return static_cast<std::uint8_t>(std::lround(clamped * 100.0F));
}

}  // namespace

HerdCareLossView HerdCareLossAt(const ProductionConfig& config,
                                const WorldState& world,
                                UnitId unit) {
  HerdCareLossView view;
  const float full = FullCareCoverage(config, world);
  float lowest = 1.0F;
  float worst_loss = 0.0F;
  for (const HerdRow& herd : world.herds.rows) {
    if (herd.household_owned != 0 || herd.unit.value != unit.value ||
        herd.kind.value >= config.livestock.size()) {
      continue;
    }
    view.measured = true;
    lowest = std::min(lowest, herd.care_coverage);
    // The herd day's own fourth factor (HerdProduceFactor): the same floor, the same line.
    const float factor = CareFactorAt(config.farming,
                                      config.livestock[herd.kind.value].care_produce_floor,
                                      herd.care_coverage,
                                      full);
    worst_loss = std::max(worst_loss, 1.0F - factor);
    view.horses_here = view.horses_here || herd.kind.value == config.horse_kind.value;
  }
  if (!view.measured) {
    return view;
  }
  view.coverage_pct = PercentOf(lowest);
  view.produce_loss_pct = PercentOf(worst_loss);
  if (view.horses_here) {
    view.draught_pct = PercentOf(CareDraughtFactor(config, world));
  }
  return view;
}

}  // namespace core
