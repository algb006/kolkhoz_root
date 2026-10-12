// What a leg of the groom's plan carries (leg_loads.h).

#include "leg_loads.h"

#include <cstdint>

#include "core_common/state_table_ops.h"
#include "logistics_plan.h"

namespace core {
namespace {

/// The resource of most grams in `amounts`, invalid when all are nought.
ResourceId MostGrams(const ResourceAmounts& amounts) {
  ResourceId most;
  Grams best = 0;
  for (std::uint32_t index = 0; index < amounts.size(); ++index) {
    if (amounts[index] > best) {
      best = amounts[index];
      most = ResourceId{static_cast<std::uint16_t>(index)};
    }
  }
  return most;
}

/// The form of `resource` by resources.csv `cargo_form`; kNone for none.
CargoForm FormOf(const LogisticsConfig& config, ResourceId resource) {
  return resource.value < config.cargo_form.size() ? config.cargo_form[resource.value]
                                                   : CargoForm::kNone;
}

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

}  // namespace

ResourceId LegResource(const WorldState& world, const CartLeg& leg, ResourceId log) {
  const std::uint32_t task_row = FindRow(world.logistics_tasks, leg.task);
  if (task_row == kNoRow) {
    return ResourceId{};
  }
  const LogisticsTaskRow& task = world.logistics_tasks.rows[task_row];
  switch (task.load_kind) {
    case LogisticsLoadKind::kFieldHeap: {
      const std::uint32_t row = FindRow(world.fields, task.field);
      return row != kNoRow ? world.fields.rows[row].reaped_resource : ResourceId{};
    }
    case LogisticsLoadKind::kStandLogs:
      return log;
    case LogisticsLoadKind::kSiteDig: {
      const std::uint32_t row = FindRow(world.extraction_sites, task.extraction_site);
      return row != kNoRow ? world.extraction_sites.rows[row].resource : ResourceId{};
    }
    case LogisticsLoadKind::kDistrictLot: {
      const std::uint32_t row = FindRow(world.limit_deliveries, task.limit_delivery);
      return row != kNoRow ? MostGrams(world.limit_deliveries.rows[row].goods) : ResourceId{};
    }
    case LogisticsLoadKind::kStoreTransfer: {
      const std::uint32_t row = FindRow(world.units, task.unit);
      return row != kNoRow ? MostGrams(world.units.rows[row].stock) : ResourceId{};
    }
    case LogisticsLoadKind::kSiteSupply: {
      const std::uint32_t row = FindRow(world.units, task.unit);
      if (row == kNoRow || world.units.rows[row].construction.legs.empty()) {
        return ResourceId{};
      }
      return MostGrams(world.units.rows[row].construction.legs.front().grams);
    }
    case LogisticsLoadKind::kLogisticsLoadKindCount:
      break;
  }
  return ResourceId{};
}

CargoForm LegCargoForm(const LogisticsConfig& config, const WorldState& world, const CartLeg& leg) {
  if (leg.task.value == kInvalidEntityIdValue) {
    return CargoForm::kNone;  // a ride
  }
  const bool stands_at_load = leg.from.x == leg.to.x && leg.from.y == leg.to.y;
  if (leg.loaded == 0 && (!stands_at_load || leg.depart == leg.arrive)) {
    // A trip's way back, empty; or a load of the chain the cart never
    // carted — kept in its place with no time on it (KeepInTheChain) — which
    // would otherwise be drawn loaded at a heap it never reached (the Fable
    // review of 11 October 2026, item 9).
    return CargoForm::kNone;
  }
  return FormOf(config, LegResource(world, leg, config.log_resource));
}

WalkerCarry WalkerCarryOf(const LogisticsConfig& config) {
  // TODAY'S WORLD: one carry for every form (labor.csv carry_kg_adult) - a
  // bundle and a stretcher's half weigh what a sack does until the porter's
  // rules land (#671, the second delivery).
  const Grams adult = GramsFromKilograms(config.carry_kg_adult);
  return WalkerCarry{.adult = adult, .rick = adult, .stretcher_pair = 2 * adult};
}

void FillTripLoads(const LogisticsConfig& config, const WorldState& world, GroomPlan& plan) {
  const WalkerCarry carry = WalkerCarryOf(config);
  for (CartPlan& cart : plan.carts) {
    for (CartLeg& leg : cart.legs) {
      if (leg.task.value == kInvalidEntityIdValue || leg.resource.value != kInvalidDefIdValue) {
        continue;  // a ride; or a leg written before, kept by a re-plan
      }
      leg.resource = LegResource(world, leg, config.log_resource);
      if (cart.on_foot && leg.loaded != 0) {
        const CargoForm form = FormOf(config, leg.resource);
        leg.carried = WalkerTripGrams(form, HoldOf(form, cart.partner), carry, 0);
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
  const WalkerCarry carry = WalkerCarryOf(config);
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
      porter.form = FormOf(config, leg.resource);
      if (leg.loaded != 0) {
        porter.grams = leg.carried;
        const Grams full =
            WalkerTripGrams(porter.form, HoldOf(porter.form, cart.partner), carry, 0);
        porter.load_share =
            full > 0 ? static_cast<float>(leg.carried) / static_cast<float>(full) : 0.0F;
      }
      out.push_back(porter);
      break;  // one leg at a time: the one begun and not ended
    }
  }
}

}  // namespace core
