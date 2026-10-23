// What a leg of the groom's plan carries (leg_loads.h).

#include "leg_loads.h"

#include <cstdint>

#include "core_common/load_resource.h"
#include "core_common/state_table_ops.h"
#include "logistics_plan.h"

namespace core {
namespace {

/// How a carrier holds `form`: in a pair on a stretcher or under a log when
/// his plan names a partner, alone otherwise (haul.h, WalkerHold).
WalkerHold HoldOf(CargoForm form, ResidentId partner) {
  if (partner.value == kInvalidEntityIdValue) {
    return WalkerHold::kAlone;
  }
  if (form == CargoForm::kManure) {
    return WalkerHold::kStretcher;
  }
  return form == CargoForm::kLong ? WalkerHold::kLogPair : WalkerHold::kAlone;
}

/// A full trip's grams for ONE carrier of `cart` with `resource` - the
/// bundle, the stretcher's half, the log's half under LogPairMayCarry, the
/// sack (haul.h, WalkerTripGrams). A resource with no form is a sack
/// (WalkerLoadGrams).
Grams FullTripOf(const LogisticsConfig& config, const CartPlan& cart, ResourceId resource) {
  const CargoForm form = CargoFormOf(config.porter, resource);
  const Grams grams = WalkerTripGrams(
      form, HoldOf(form, cart.partner), config.porter.carry, config.porter.log_piece_grams);
  return grams > 0 ? grams : config.porter.carry.adult;
}

}  // namespace

ResourceId LegResource(const WorldState& world, const CartLeg& leg, ResourceId log) {
  const std::uint32_t task_row = FindRow(world.logistics_tasks, leg.task);
  return task_row != kNoRow ? TaskLoadResource(world, world.logistics_tasks.rows[task_row], log)
                            : ResourceId{};
}

CargoForm LegCargoForm(const LogisticsConfig& config, const WorldState& world, const CartLeg& leg) {
  if (leg.task.value == kInvalidEntityIdValue) {
    return CargoForm::kNone;  // a ride
  }
  const bool stands_at_load = leg.from.x == leg.to.x && leg.from.y == leg.to.y;
  if (leg.loaded == 0 && (!stands_at_load || leg.kept_in_chain != 0)) {
    // A trip's way back, empty; or a load of the chain the cart never
    // carted — kept in its place with no time on it (KeepInTheChain) — which
    // would otherwise be drawn loaded at a heap it never reached (the Fable
    // review of 11 October 2026, item 9). Read by its marker since T1 (save
    // 178): «depart == arrive» was also a carting under an hour, floored.
    return CargoForm::kNone;
  }
  // The cart's look by the logistics' own column (LogisticsConfig::cargo_form,
  // the one CargoFormsMix reads) - the same reading as the porter's rules on
  // a parsed table set; a hand-made config sets the one it tests.
  const ResourceId resource = LegResource(world, leg, config.log_resource);
  return resource.value < config.cargo_form.size() ? config.cargo_form[resource.value]
                                                   : CargoForm::kNone;
}

void FillTripLoads(const LogisticsConfig& config, const WorldState& world, GroomPlan& plan) {
  for (CartPlan& cart : plan.carts) {
    for (CartLeg& leg : cart.legs) {
      if (leg.task.value == kInvalidEntityIdValue || leg.resource.value != kInvalidDefIdValue) {
        continue;  // a ride; or a leg written before, kept by a re-plan
      }
      if (!cart.on_foot) {
        leg.resource = LegResource(world, leg, config.log_resource);
        continue;
      }
      // ONE RESOURCE A TRIP (transport design §2): a works' supply by the
      // first resource its front leg holds, as production's settle moves it.
      const std::uint32_t task_row = FindRow(world.logistics_tasks, leg.task);
      leg.resource =
          task_row != kNoRow
              ? WalkerLoadResource(world, world.logistics_tasks.rows[task_row], config.log_resource)
              : ResourceId{};
      if (leg.loaded != 0) {
        leg.carried = FullTripOf(config, cart, leg.resource);
      }
    }
  }
}

void CollectPorterLegs(const LogisticsConfig& config,
                       const WorldState& completed,
                       std::vector<PorterLeg>& out) {
  const GroomPlan& plan = completed.groom_plan;
  if (plan.day != completed.calendar.day) {
    return;
  }
  const Tick now = completed.calendar.tick;
  for (const CartPlan& cart : plan.carts) {
    if (!cart.on_foot) {
      continue;
    }
    for (const CartLeg& leg : cart.legs) {
      if (!(leg.depart <= now && now < leg.arrive)) {
        continue;
      }
      PorterLeg porter;
      porter.porter = cart.driver;
      porter.partner = cart.partner;
      porter.from = leg.from;
      porter.to = leg.to;
      porter.depart = leg.depart;
      porter.arrive = leg.arrive;
      porter.loaded = leg.loaded;
      porter.resource = leg.resource;
      porter.form = CargoFormOf(config.porter, leg.resource);
      if (leg.loaded != 0) {
        porter.grams = leg.carried;
        const Grams full = FullTripOf(config, cart, leg.resource);
        porter.load_share =
            full > 0 ? static_cast<float>(leg.carried) / static_cast<float>(full) : 0.0F;
      }
      out.push_back(porter);
      break;  // one leg at a time: the one begun and not ended
    }
  }
}

}  // namespace core
