// The rating earned beyond the plan (district_reputation.h).

#include "district_reputation.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "core_common/quantities.h"
#include "district_plan.h"

namespace core {
namespace {

/// Moves the rating by `step` within its scale; returns the points it moved.
float StepReputation(WorldState& current, float step) {
  const float before = current.chairman.raikom_reputation;
  current.chairman.raikom_reputation = std::clamp(before + step, kMetricMin, kMetricMax);
  return current.chairman.raikom_reputation - before;
}

}  // namespace

float OwnOverfulfilGrainTonnes(const ProductionConfig& config, const WorldState& current) {
  if (!(config.limit.overfulfil_grain_kcal_per_gram > 0.0F)) {
    return 0.0F;
  }
  // THE SOWING PLAN'S POSITIONS ONLY - grain and potato (boss, the #1043
  // thread [6]): milk is over its due every year for a plan-only village, so
  // it rewarded time, not play; the winter milk outside the plan goes with it.
  // The limit points keep both (PlanOverfulfilGrainTonnes). Returns with
  // milk when the milk plan presses like grain (district.md). The short
  // positions' deduction is the limit's own, over the same positions.
  std::vector<std::uint32_t> resources;
  for (const ProductionConfig::PlanPosition& position : config.plan_positions) {
    if (position.crop.value >= config.crops.size()) {
      continue;
    }
    const std::uint32_t resource = config.crops[position.crop.value].resource.value;
    if (std::ranges::find(resources, resource) == resources.end()) {
      resources.push_back(resource);
    }
  }
  double over_grams = 0.0;
  double short_grams = 0.0;
  for (const std::uint32_t resource : resources) {
    const Grams due = resource < current.plan.due.size() ? current.plan.due[resource] : 0;
    if (due <= 0) {
      continue;
    }
    const Grams delivered =
        resource < current.plan.delivered.size() ? current.plan.delivered[resource] : 0;
    const double grain = GrainPerGram(config, ResourceId{static_cast<std::uint16_t>(resource)});
    if (delivered > due) {
      over_grams += static_cast<double>(delivered - due) * grain;
    } else if (!PositionDelivered(config, due, delivered)) {
      short_grams += static_cast<double>(due - delivered) * grain;
    }
  }
  // THE GRAIN THE DISTRICT LENT buys no rating («a cumulative counter that a
  // sale can raise is a pump»).
  double lent_grams = 0.0;
  const ResourceAmounts& lent = current.ledger.current.goods_loan_taken;
  for (std::uint32_t index = 0; index < lent.size(); ++index) {
    lent_grams += static_cast<double>(lent[index]) *
                  GrainPerGram(config, ResourceId{static_cast<std::uint16_t>(index)});
  }
  const double own = over_grams -
                     (static_cast<double>(config.limit.overfulfil_shortfall_factor) * short_grams) -
                     lent_grams;
  return own > 0.0 ? static_cast<float>(own / static_cast<double>(kGramsPerTonne)) : 0.0F;
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

void StepReputationBeyondPlan(const ProductionConfig& config, WorldState& current) {
  const float own = OwnOverfulfilGrainTonnes(config, current);
  const std::uint32_t tiers = OverfulfilTiersReached(config.limit, own);
  YearLedger& book = current.ledger.current;
  book.reputation_own_overfulfil_t = own;
  book.reputation_tiers_reached = static_cast<std::uint16_t>(tiers);
  if (!(config.plan_overfulfil_reputation_per_tier > 0.0F)) {
    return;
  }
  book.reputation_tier_step += StepReputation(
      current,
      std::min(static_cast<float>(tiers) * config.plan_overfulfil_reputation_per_tier,
               config.plan_overfulfil_reputation_max));
}

void StepReputationForCleanVisit(const ProductionConfig& config, WorldState& current) {
  YearLedger& book = current.ledger.current;
  ++book.reputation_clean_visits;
  if (current.chairman.reputation_recovery_owed == 0) {
    return;  // nothing to recover: a clean visit is the plain order of things
  }
  current.chairman.reputation_recovery_owed = 0;
  if (config.district_visit_clean_reputation > 0.0F) {
    book.reputation_visit_step += StepReputation(current, config.district_visit_clean_reputation);
  }
}

void OweReputationRecovery(WorldState& current) {
  current.chairman.reputation_recovery_owed = 1;
}

void BookReputationLoss(WorldState& current, float before) {
  const float lost = before - current.chairman.raikom_reputation;
  if (lost > 0.0F) {
    current.ledger.current.reputation_losses += lost;
  }
}

}  // namespace core
