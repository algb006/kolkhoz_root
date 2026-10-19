// A carrier on foot in the plan's clock (porter_pairs.h).

#include "porter_pairs.h"

#include "core_common/cargo_form.h"
#include "core_common/haul.h"
#include "core_common/ids.h"
#include "core_common/load_resource.h"
#include "core_common/logistics_state.h"
#include "core_common/quantities.h"

namespace core {

float WalkerDrainOf(const LogisticsConfig& config,
                    const WorldState& world,
                    std::uint32_t task_row,
                    const PlanPaces& paces) {
  if (!paces.has_carts) {
    return paces.cart_drain;
  }
  const ResourceId resource =
      WalkerLoadResource(world, world.logistics_tasks.rows[task_row], config.log_resource);
  return paces.cart_drain *
         WalkerShareOfCartDay(WalkerLoadGrams(config.porter, resource),
                              CartLoadGrams(config.porter,
                                            CartGearToday(world, CartSpecsOf(config)).load_kg,
                                            resource),  // SL: labour's same share
                              paces.walk_hours_per_km,
                              paces.harness_hours_per_km,
                              paces.loaded_hours_per_km);
}

PairTerms PairTermsOf(const LogisticsConfig& config,
                      const WorldState& world,
                      std::uint32_t task_row,
                      const PlanPaces& paces) {
  PairTerms terms;
  const LogisticsTaskRow& task = world.logistics_tasks.rows[task_row];
  const ResourceId resource = WalkerLoadResource(world, task, config.log_resource);
  terms.log = task.load_kind == LogisticsLoadKind::kSiteSupply &&
              resource.value != kInvalidDefIdValue && resource.value == config.porter.log.value &&
              config.porter.log_pair_loaded_pace > 0.0F;
  terms.pairs = terms.log || CargoFormOf(config.porter, resource) == CargoForm::kManure;
  if (terms.log) {
    terms.loaded_hours_per_km = paces.walk_hours_per_km / config.porter.log_pair_loaded_pace;
    terms.drain =
        paces.cart_drain *
        (paces.has_carts ? LogPairShareOfCartDay(
                               config.porter,
                               CartLoadGrams(config.porter,
                                             CartGearToday(world, CartSpecsOf(config)).load_kg,
                                             resource),  // SL: labour's same share
                               paces.walk_hours_per_km,
                               paces.harness_hours_per_km,
                               paces.loaded_hours_per_km)
                         : 1.0F);
  }
  return terms;
}

}  // namespace core
