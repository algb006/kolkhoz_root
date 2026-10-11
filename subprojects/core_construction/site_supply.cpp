// How a site is supplied (core_construction/site_supply.h).

#include "site_supply.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "core_common/unit_state.h"

namespace core {

void AddTo(ResourceAmounts& amounts, ResourceId resource, Grams delta) {
  if (delta == 0 || resource.value == kInvalidDefIdValue) {
    return;
  }
  if (resource.value >= amounts.size()) {
    amounts.resize(static_cast<std::size_t>(resource.value) + 1, 0);
  }
  amounts[resource.value] += delta;
  if (amounts[resource.value] < 0) {
    amounts[resource.value] = 0;
  }
}

Grams TakeFromStores(WorldState& current,
                     std::uint32_t site_row,
                     ResourceId resource,
                     Grams wanted) {
  Grams taken = 0;
  for (std::uint32_t row = 0; row < current.units.rows.size() && taken < wanted; ++row) {
    if (row == site_row || current.units.rows[row].level == 0) {
      continue;
    }
    // Not another upgrade's recipe: it was checked and carried in for that
    // works, and a second start must not undo the first one's check.
    const Grams have = UnreservedOf(current.units.rows[row], resource);
    if (have <= 0) {
      continue;
    }
    const Grams give = have < wanted - taken ? have : wanted - taken;
    AddTo(current.units.rows[row].stock, resource, -give);
    taken += give;
  }
  return taken;
}

namespace {

/// Who gives: a stand's felled logs, a pit's dug load, or a standing unit's
/// stock. The order of the enum is the order of the taking.
enum class GiverKind : std::uint8_t { kStand, kPit, kStore };

/// One give of a fixing: who gives, from where, and how much.
struct Give {
  GiverKind kind = GiverKind::kStore;
  std::uint32_t row = 0;
  Vec2 from;
  Grams grams = 0;
};

/// THE ONE RULE OF WHO GIVES (TakeForSite and its rehearsal).
///
/// WHAT LIES FELLED AT A STAND OR DUG AT A PIT FIRST (the direct leg; boss,
/// core-boss-c2-site-supply-2026-10-09 [20]): it goes straight to the site,
/// and the store gives only the rest. Until the direct leg the fixing drew on
/// the stores alone, and on the canon two thirds of the logs and of the clay
/// fixed for works had been carted into their pile within the twelve days
/// before — one tonne, two journeys (pair 4's print (e): 6 120 t of 14 275).
///
/// Then the standing units with unreserved `resource`. Each group the nearest
/// to the site first by the straight line, then by row, each giving what it
/// has until `wanted` is met.
std::vector<Give> GivesFor(const WorldState& world,
                           std::uint32_t site_row,
                           ResourceId resource,
                           Grams wanted,
                           ResourceId log_resource) {
  std::vector<Give> gives;
  if (wanted <= 0 || resource.value == kInvalidDefIdValue) {
    return gives;
  }
  std::vector<Give> givers;
  if (resource.value == log_resource.value) {
    for (std::uint32_t row = 0; row < world.stands.rows.size(); ++row) {
      const TimberStandRow& stand = world.stands.rows[row];
      if (stand.load_grams > 0) {
        givers.push_back(Give{.kind = GiverKind::kStand,
                              .row = row,
                              .from = stand.position,
                              .grams = stand.load_grams});
      }
    }
  }
  for (std::uint32_t row = 0; row < world.extraction_sites.rows.size(); ++row) {
    const ExtractionSiteRow& pit = world.extraction_sites.rows[row];
    if (pit.resource.value == resource.value && pit.load_grams > 0) {
      givers.push_back(
          Give{.kind = GiverKind::kPit, .row = row, .from = pit.position, .grams = pit.load_grams});
    }
  }
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    const UnitRow& unit = world.units.rows[row];
    if (row != site_row && unit.level > 0 && UnreservedOf(unit, resource) > 0) {
      givers.push_back(Give{.kind = GiverKind::kStore,
                            .row = row,
                            .from = unit.position,
                            .grams = UnreservedOf(unit, resource)});
    }
  }
  const Vec2 site = world.units.rows[site_row].position;
  const auto distance_sq = [site](const Give& give) {
    return ((give.from.x - site.x) * (give.from.x - site.x)) +
           ((give.from.y - site.y) * (give.from.y - site.y));
  };
  // The stores after the stands and the pits; within a group by the distance.
  // Stable: equal givers keep the order they were collected in (by row).
  std::ranges::stable_sort(givers, [&distance_sq](const Give& left, const Give& right) {
    const bool left_store = left.kind == GiverKind::kStore;
    const bool right_store = right.kind == GiverKind::kStore;
    if (left_store != right_store) {
      return right_store;
    }
    return distance_sq(left) < distance_sq(right);
  });
  Grams taken = 0;
  for (Give& give : givers) {
    if (taken >= wanted) {
      break;
    }
    give.grams = give.grams < wanted - taken ? give.grams : wanted - taken;
    gives.push_back(give);
    taken += give.grams;
  }
  return gives;
}

/// The grams of the gives, the largest of them raised into `giver`.
Grams SumGives(const std::vector<Give>& gives, LargestGiver& giver) {
  Grams taken = 0;
  for (const Give& give : gives) {
    taken += give.grams;
    if (give.grams > giver.grams) {
      giver.grams = give.grams;
      giver.position = give.from;
    }
  }
  return taken;
}

/// Takes `grams` off a stand's or a pit's lying load and shrinks its own
/// carting seam by the same share: the seam was sized for the whole load, and
/// what is spoken for a works leaves by the works' carts (the direct leg).
void TakeFromLoad(Grams& load, float& haul_remaining, float& haul_written, Grams grams) {
  if (grams <= 0 || load <= 0) {
    return;
  }
  const Grams taken = grams < load ? grams : load;
  const float left = static_cast<float>(load - taken) / static_cast<float>(load);
  load -= taken;
  haul_remaining *= left;
  haul_written *= left;
}

/// The leg of `site` loaded at `from` by this way; a new one at the back when
/// the works have none there.
SupplyLeg& LegAt(ConstructionState& works, Vec2 from, bool log_way) {
  for (SupplyLeg& leg : works.legs) {
    if (leg.from.x == from.x && leg.from.y == from.y && (leg.log_way != 0) == log_way) {
      return leg;
    }
  }
  SupplyLeg leg;
  leg.from = from;
  leg.log_way = log_way ? 1U : 0U;
  works.legs.push_back(leg);
  return works.legs.back();
}

}  // namespace

void SyncFrontLeg(ConstructionState& works) {
  if (!works.legs.empty()) {
    works.supply_from = works.legs.front().from;
    works.supply_log_way = works.legs.front().log_way;
  }
}

Grams TakeForSite(WorldState& current,
                  std::uint32_t site_row,
                  ResourceId resource,
                  Grams wanted,
                  ResourceId log_resource,
                  LargestGiver& giver) {
  const std::vector<Give> gives = GivesFor(current, site_row, resource, wanted, log_resource);
  const Grams taken = SumGives(gives, giver);
  ConstructionState& works = current.units.rows[site_row].construction;
  for (const Give& give : gives) {
    switch (give.kind) {
      case GiverKind::kStand: {
        TimberStandRow& stand = current.stands.rows[give.row];
        TakeFromLoad(
            stand.load_grams, stand.haul_days_remaining, stand.haul_days_written, give.grams);
        break;
      }
      case GiverKind::kPit: {
        ExtractionSiteRow& pit = current.extraction_sites.rows[give.row];
        TakeFromLoad(pit.load_grams, pit.haul_days_remaining, pit.haul_days_written, give.grams);
        break;
      }
      case GiverKind::kStore:
        AddTo(current.units.rows[give.row].stock, resource, -give.grams);
        break;
    }
    SupplyLeg& leg = LegAt(works, give.from, give.kind == GiverKind::kStand);
    AddTo(leg.grams, resource, give.grams);
    leg.unpriced += give.grams;
  }
  AddTo(works.awaiting, resource, taken);
  SyncFrontLeg(works);
  return taken;
}

Grams RehearseTakeForSite(const WorldState& world,
                          std::uint32_t site_row,
                          ResourceId resource,
                          Grams wanted,
                          ResourceId log_resource,
                          LargestGiver& giver) {
  return SumGives(GivesFor(world, site_row, resource, wanted, log_resource), giver);
}

void StampTheWait(UnitRow& site, bool awaited_before, SimDay today) {
  if (!awaited_before && AnythingAwaits(site.construction)) {
    site.construction.supply_since_day = today;
  }
}

Grams FixedForSite(const UnitRow& site, ResourceId resource) {
  return AmountOf(site.stock, resource) + AmountOf(site.construction.awaiting, resource);
}

void FoldAwaitingIntoStock(UnitRow& site) {
  for (std::size_t index = 0; index < site.construction.awaiting.size(); ++index) {
    AddTo(site.stock,
          ResourceId{static_cast<std::uint16_t>(index)},
          site.construction.awaiting[index]);
  }
  site.construction.awaiting.clear();
  site.construction.legs.clear();
  site.construction.supply_days_first = 0.0F;
  site.construction.supply_days_all = 0.0F;
  site.construction.supply_days_remaining = 0.0F;
  site.construction.supply_days_written = 0.0F;
}

}  // namespace core
