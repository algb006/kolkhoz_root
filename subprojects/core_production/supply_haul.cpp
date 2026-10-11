// The supply of a works: its seam, a leg at a time, and its price before a
// start (field_haul.h). Split out of field_haul.cpp when the direct leg took
// that file past the thousand-line limit: a load carted INTO the stores and a
// works' materials carted out of them to a site are different journeys.

#include <cstddef>
#include <cstdint>

#include "core_common/haul.h"
#include "core_common/ledger_state.h"
#include "core_common/quantities.h"
#include "core_common/road_route.h"
#include "core_common/supply_estimate.h"
#include "core_common/unit_state.h"
#include "core_common/work_seam.h"
#include "field_haul.h"
#include "haul_ways.h"

namespace core {

SupplyEstimate PriceSiteSupply(const ProductionConfig& config,
                               const WorldState& world,
                               SupplyEstimate plan,
                               Vec2 site) {
  if (plan.grams <= 0) {
    return plan;
  }
  // The seam's own arithmetic (SettleSiteSupply): the rate of today's road,
  // bed and carrier from the sender to the site.
  const HaulRate rate = RateToward(config, world, plan.sender, site);
  plan.cart_days = HaulDaysFor(plan.grams, rate, config.standard_day_hours);
  // The rate's own question (work_seam.h): no adult kolkhoz horse, no cart.
  plan.on_foot = !SettlementHasCarts(world, config.horse_kind);
  return plan;
}

namespace {

/// The grams of a dense amounts vector.
Grams SumOf(const ResourceAmounts& amounts) {
  Grams sum = 0;
  for (const Grams grams : amounts) {
    sum += grams;
  }
  return sum;
}

/// The rate of one leg of a works' supply: from its sender to the site, the
/// log cart's way for logs straight off a stand (StandHaulRate's mode).
HaulRate LegRate(const ProductionConfig& config,
                 const WorldState& world,
                 const SupplyLeg& leg,
                 Vec2 site) {
  return RateToward(
      config, world, leg.from, site, leg.log_way != 0 ? TravelMode::kLogCart : TravelMode::kCart);
}

}  // namespace

void SettleSiteSupply(const ProductionConfig& config, WorldState& current, bool evening) {
  for (UnitRow& unit : current.units.rows) {
    ConstructionState& works = unit.construction;
    // Legs carted out are dropped from the front; the sender follows.
    const auto drop_empty_front = [&works]() {
      bool dropped = false;
      while (!works.legs.empty() && SumOf(works.legs.front().grams) <= 0) {
        works.legs.erase(works.legs.begin());
        dropped = true;
      }
      if (!works.legs.empty()) {
        works.supply_from = works.legs.front().from;
        works.supply_log_way = works.legs.front().log_way;
      }
      return dropped;
    };
    if (drop_empty_front()) {
      works.supply_days_remaining = 0.0F;
      works.supply_days_written = 0.0F;
    }
    if (works.legs.empty()) {
      works.supply_days_remaining = 0.0F;
      works.supply_days_written = 0.0F;
      works.supply_days_first = 0.0F;
      works.supply_days_all = 0.0F;
      continue;
    }
    // THE WORKS' WHOLE LOAD AS FIRST PRICED (ConstructionState::
    // supply_days_first): what the fixing added since the last hour, each leg
    // by its own way and today's bed.
    for (SupplyLeg& leg : works.legs) {
      if (leg.unpriced > 0) {
        works.supply_days_first += HaulDaysFor(
            leg.unpriced, LegRate(config, current, leg, unit.position), config.standard_day_hours);
        leg.unpriced = 0;
      }
    }
    SupplyLeg& front = works.legs.front();
    Grams awaiting = SumOf(front.grams);
    // The drained share of what awaits ON THE FRONT LEG is on the site this
    // hour (stage V1's rule, DeliverDrained). A site has no door and no room
    // to refuse it, so all the drained work converts; a seam drained to
    // nought brings the leg's last gram, whatever the share's rounding left.
    const float done = works.supply_days_written > works.supply_days_remaining
                           ? works.supply_days_written - works.supply_days_remaining
                           : 0.0F;
    if (done > 0.0F && works.supply_days_written > 0.0F) {
      const float share = done / works.supply_days_written;
      Grams budget = works.supply_days_remaining <= 0.0F || share >= 1.0F
                         ? awaiting
                         : GramsFromFloat(static_cast<float>(awaiting) * share);
      // The way the carts took, for the year's book (CartLoadSource::kSupply),
      // measured before anything moved, as the day's other loads are.
      BookCartRun(config,
                  current,
                  CartLoadSource::kSupply,
                  LegRate(config, current, front, unit.position),
                  budget);
      for (std::size_t index = 0; index < front.grams.size() && budget > 0; ++index) {
        // Never more than the works' own total holds of it: the two homes
        // are bound gram for gram (ConstructionState::legs).
        const Grams total = index < works.awaiting.size() ? works.awaiting[index] : 0;
        Grams moved = front.grams[index] < budget ? front.grams[index] : budget;
        moved = moved < total ? moved : total;
        if (moved <= 0) {
          continue;
        }
        front.grams[index] -= moved;
        works.awaiting[index] -= moved;
        if (unit.stock.size() <= index) {
          unit.stock.resize(index + 1, 0);
        }
        unit.stock[index] += moved;
        budget -= moved;
        awaiting -= moved;
      }
      works.supply_days_written = works.supply_days_remaining;
    }
    // A LEG CARTED OUT GIVES THE SEAM TO THE NEXT: the seam is closed here and
    // written below in this same hour from the next sender («unwritten»), so
    // the carts of this hour's top-up go on to it.
    if (awaiting <= 0 && drop_empty_front()) {
      works.supply_days_remaining = 0.0F;
      works.supply_days_written = 0.0F;
      if (works.legs.empty()) {
        works.supply_days_first = 0.0F;
        works.supply_days_all = 0.0F;
        continue;
      }
      awaiting = SumOf(works.legs.front().grams);
    }
    // NOTHING IS CARTED TO A PAUSED WORKS (construction design §6, «Подвоз на
    // стоящую стройку прекращается»): its seam is closed, what awaits waits.
    if (unit.paused != 0) {
      works.supply_days_remaining = 0.0F;
      works.supply_days_written = 0.0F;
      continue;
    }
    // The seam is written in the evening, as every load's is — and in the
    // hour a load first appears with none (a site started this morning is
    // offered to the day's top-up, not to tomorrow's placement).
    const bool unwritten = works.supply_days_remaining <= 0.0F && works.supply_days_written <= 0.0F;
    if (!evening && !unwritten) {
      continue;
    }
    const HaulRate rate = LegRate(config, current, works.legs.front(), unit.position);
    works.supply_days_remaining =
        awaiting > 0 ? HaulDaysFor(awaiting, rate, config.standard_day_hours) : 0.0F;
    works.supply_days_written = works.supply_days_remaining;
    // And what is still to cart on every leg, for the crew's size
    // (ConstructionState::supply_days_all).
    works.supply_days_all = 0.0F;
    for (const SupplyLeg& leg : works.legs) {
      works.supply_days_all += HaulDaysFor(SumOf(leg.grams),
                                           LegRate(config, current, leg, unit.position),
                                           config.standard_day_hours);
    }
  }
}

}  // namespace core
