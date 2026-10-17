// The rating earned beyond the plan (district_reputation.h).

#include "district_reputation.h"

#include <algorithm>
#include <cstdint>

#include "core_common/quantities.h"
#include "district_plan.h"

namespace core {
namespace {

void StepReputation(WorldState& current, float step) {
  if (!(step > 0.0F)) {
    return;
  }
  current.chairman.raikom_reputation =
      std::clamp(current.chairman.raikom_reputation + step, kMetricMin, kMetricMax);
}

}  // namespace

float OwnOverfulfilGrainTonnes(const ProductionConfig& config,
                               const WorldState& current,
                               float overfulfil_tonnes) {
  double lent_grams = 0.0;
  const ResourceAmounts& lent = current.ledger.current.goods_loan_taken;
  for (std::uint32_t index = 0; index < lent.size(); ++index) {
    lent_grams += static_cast<double>(lent[index]) *
                  GrainPerGram(config, ResourceId{static_cast<std::uint16_t>(index)});
  }
  const double own =
      static_cast<double>(overfulfil_tonnes) - (lent_grams / static_cast<double>(kGramsPerTonne));
  return own > 0.0 ? static_cast<float>(own) : 0.0F;
}

std::uint32_t OverfulfilTiersReached(const LimitCatalog& catalog, float grain_tonnes) {
  if (!(grain_tonnes > 0.0F)) {
    return 0;
  }
  if (!(grain_tonnes > catalog.overfulfil_tier1_t)) {
    return 1;
  }
  return grain_tonnes > catalog.overfulfil_tier1_t + catalog.overfulfil_tier2_t ? 3U : 2U;
}

void StepReputationBeyondPlan(const ProductionConfig& config,
                              WorldState& current,
                              float overfulfil_tonnes) {
  if (!(config.plan_overfulfil_reputation_per_tier > 0.0F)) {
    return;
  }
  const std::uint32_t tiers = OverfulfilTiersReached(
      config.limit, OwnOverfulfilGrainTonnes(config, current, overfulfil_tonnes));
  StepReputation(current,
                 std::min(static_cast<float>(tiers) * config.plan_overfulfil_reputation_per_tier,
                          config.plan_overfulfil_reputation_max));
}

void StepReputationForCleanVisit(const ProductionConfig& config, WorldState& current) {
  StepReputation(current, config.district_visit_clean_reputation);
}

}  // namespace core
