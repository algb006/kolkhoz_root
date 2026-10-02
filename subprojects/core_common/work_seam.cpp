// The seam of a work assignment (work_seam.h). Moved out of core_labor on
// 2026-09-05 when the resident's activity needed the same seven cases to
// tell "nobody gave him work" from "he was given work and there is nothing
// to work with".

#include "core_common/work_seam.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "core_common/module_rules.h"
#include "core_common/state_table_ops.h"

namespace core {

const float* WorkSeamOf(const WorldState& world, const WorkAssignment& work) {
  if (work.kind == WorkKind::kHerdCare) {
    const std::uint32_t row = FindRow(world.herds, work.herd);
    return row == kNoRow ? nullptr : &world.herds.rows[row].care_days_remaining;
  }
  if (work.kind == WorkKind::kConstruction) {
    const std::uint32_t row = FindRow(world.units, work.unit);
    // A site that finished or was demolished since the morning: the crew
    // simply has nothing to drain, exactly as with a field production has
    // moved on.
    return row == kNoRow ? nullptr : &world.units.rows[row].construction.labor_days_remaining;
  }
  // A PIECE OF ROAD UNDER WORK (7e): its labour, until the day it is applied.
  if (work.kind == WorkKind::kRoadWork) {
    const std::uint32_t row = FindRow(world.road_works, work.road_work);
    return row == kNoRow ? nullptr : &world.road_works.rows[row].labor_days_remaining;
  }
  // A PRODUCING UNIT (2026-09-13): its own production seam, and only while it
  // can produce at all — built, alive, not paused, its parent sound. A pause
  // or a yard falling still sends the sawyers home the same hour.
  if (work.kind == WorkKind::kUnitWork) {
    const std::uint32_t row = FindRow(world.units, work.unit);
    if (row == kNoRow) {
      return nullptr;
    }
    const UnitRow& unit = world.units.rows[row];
    if (unit.level == 0 || unit.dead != 0 || unit.paused != 0 || !ModuleParentSound(world, unit)) {
      return nullptr;
    }
    return &unit.production_days_remaining;
  }
  // THE PEREVALKA (kEmptyStore; 2026-09-19): carting out of a store being
  // emptied drains the unit's own carting seam — only while the order
  // stands and its carrying is not paused (emptying 1, not 2).
  if (work.kind == WorkKind::kHauling && work.unit.value != kInvalidEntityIdValue) {
    const std::uint32_t row = FindRow(world.units, work.unit);
    if (row == kNoRow) {
      return nullptr;
    }
    const UnitRow& unit = world.units.rows[row];
    return unit.emptying == 1 ? &unit.haul_days_remaining : nullptr;
  }
  // A TIMBER LOT AT THE DISTRICT CENTRE (decision 279, 0.36.17): carting
  // drains the lot's own carting seam while anything of it waits there.
  if (work.limit_delivery.value != kInvalidEntityIdValue) {
    const std::uint32_t row = FindRow(world.limit_deliveries, work.limit_delivery);
    if (row == kNoRow || work.kind != WorkKind::kHauling) {
      return nullptr;  // fetched and gone since the morning
    }
    const LimitDeliveryRow& lot = world.limit_deliveries.rows[row];
    return lot.own_carts != 0 ? &lot.haul_days_remaining : nullptr;
  }
  // A STAND: felling drains the felling seam while timber is marked, and
  // carting drains the stand's own carting seam while logs lie there — the
  // same two rules a field follows, on the stand's row (2026-09-13).
  if (work.stand.value != kInvalidEntityIdValue) {
    const std::uint32_t stand_row = FindRow(world.stands, work.stand);
    if (stand_row == kNoRow) {
      return nullptr;
    }
    const TimberStandRow& stand = world.stands.rows[stand_row];
    if (work.kind == WorkKind::kFelling) {
      return stand.marked_m3 > 0.0F ? &stand.work_days_remaining : nullptr;
    }
    // A planting drains the same cell as its PLANTING seam while it is not
    // yet planted (timber_planting.h); a planting holds nothing to fell until
    // it has grown, so the two never share the cell at once.
    if (work.kind == WorkKind::kPlanting) {
      return stand.kind == TimberStandKind::kPlanted && stand.planted_day == kNeverPlanted
                 ? &stand.work_days_remaining
                 : nullptr;
    }
    if (work.kind == WorkKind::kHauling) {
      return stand.load_grams > 0 ? &stand.haul_days_remaining : nullptr;
    }
    return nullptr;
  }
  // AN EXTRACTION SITE, by the stand's two rules on the site's row: digging
  // drains the digging seam while a mark stands, carting drains the carting
  // seam while a load lies there (2026-09-14).
  if (work.extraction_site.value != kInvalidEntityIdValue) {
    const std::uint32_t site_row = FindRow(world.extraction_sites, work.extraction_site);
    if (site_row == kNoRow) {
      return nullptr;
    }
    const ExtractionSiteRow& site = world.extraction_sites.rows[site_row];
    if (work.kind == WorkKind::kExtraction) {
      return site.marked_grams > 0 ? &site.work_days_remaining : nullptr;
    }
    if (work.kind == WorkKind::kHauling) {
      return site.load_grams > 0 ? &site.haul_days_remaining : nullptr;
    }
    return nullptr;
  }
  const std::uint32_t row = FindRow(world.fields, work.field);
  if (row == kNoRow) {
    return nullptr;
  }
  if (work.kind == WorkKind::kHauling) {
    // Hauling drains ITS OWN seam. It used to share the field's work seam,
    // on the strength of a comment claiming the two could never overlap —
    // and the canon's own rotation overlapped them in the same step. What
    // ends the haul is the load being gone, not a phase changing under it.
    return world.fields.rows[row].reaped_grams > 0 ? &world.fields.rows[row].haul_days_remaining
                                                   : nullptr;
  }
  if (KindOfPhase(world.fields.rows[row].phase) != work.kind) {
    return nullptr;  // production has moved the field on since the morning
  }
  return &world.fields.rows[row].work_days_remaining;
}

bool WorkPlaceOf(const WorldState& world, const WorkAssignment& work, Vec2& place) {
  if (work.kind == WorkKind::kHerdCare) {
    const std::uint32_t herd_row = FindRow(world.herds, work.herd);
    if (herd_row == kNoRow) {
      return false;
    }
    const std::uint32_t unit_row = FindRow(world.units, world.herds.rows[herd_row].unit);
    if (unit_row == kNoRow) {
      return false;
    }
    place = world.units.rows[unit_row].position;
    return true;
  }
  if (work.kind == WorkKind::kRoadWork) {
    const std::uint32_t row = FindRow(world.road_works, work.road_work);
    if (row == kNoRow) {
      return false;  // applied and gone since the morning
    }
    place = world.road_works.rows[row].place;
    return true;
  }
  if (work.kind == WorkKind::kConstruction || work.kind == WorkKind::kUnitWork ||
      (work.kind == WorkKind::kHauling && work.unit.value != kInvalidEntityIdValue)) {
    const std::uint32_t row = FindRow(world.units, work.unit);
    if (row == kNoRow) {
      return false;
    }
    place = world.units.rows[row].position;
    return true;
  }
  if (work.stand.value != kInvalidEntityIdValue) {
    const std::uint32_t stand_row = FindRow(world.stands, work.stand);
    if (stand_row == kNoRow) {
      return false;
    }
    place = world.stands.rows[stand_row].position;
    return true;
  }
  // The timber lot's carter heads for the map's northern border end, where
  // the district's road begins (road_route.h, DistrictExitPoint; 0.36.17).
  if (work.limit_delivery.value != kInvalidEntityIdValue) {
    if (FindRow(world.limit_deliveries, work.limit_delivery) == kNoRow) {
      return false;
    }
    place = DistrictExitPoint(world);
    return true;
  }
  if (work.extraction_site.value != kInvalidEntityIdValue) {
    const std::uint32_t site_row = FindRow(world.extraction_sites, work.extraction_site);
    if (site_row == kNoRow) {
      return false;
    }
    place = world.extraction_sites.rows[site_row].position;
    return true;
  }
  const std::uint32_t row = FindRow(world.fields, work.field);
  if (row == kNoRow) {
    return false;
  }
  place = world.fields.rows[row].center;
  return true;
}

bool HomePositionOf(const WorldState& world, FamilyId family, Vec2& home) {
  const std::uint32_t family_row = FindRow(world.families, family);
  if (family_row == kNoRow) {
    return false;
  }
  const FamilyRow& household = world.families.rows[family_row];
  // A family in a tent lives on the plot its house stood on (housing design
  // §20): the accountant counts the road from there, and the day starts there.
  if (household.in_tent != 0) {
    home = household.lost_house_position;
    return true;
  }
  // A lodged family lives in the house it is lodged in (housing §20).
  const UnitId roof = household.house.value == kInvalidEntityIdValue &&
                              household.lodged_in.value != kInvalidEntityIdValue
                          ? household.lodged_in
                          : household.house;
  const std::uint32_t house_row = FindRow(world.units, roof);
  if (house_row == kNoRow) {
    return false;
  }
  home = world.units.rows[house_row].position;
  return true;
}

bool RidesTheBrigadesCart(WorkKind kind, LandKind land) {
  return (kind == WorkKind::kHarvest || kind == WorkKind::kSowing) && land == LandKind::kArable;
}

bool BrigadeCartIsOut(const WorldState& world, WorkKind kind, FieldId field) {
  for (const ResidentRow& person : world.residents.rows) {
    if (person.work.kind == kind && person.work.field.value == field.value &&
        person.work.rides_horse != 0) {
      return true;
    }
  }
  return false;
}

bool WorkRidesOut(const WorldState& world, const WorkAssignment& work) {
  if (RidesOut(work.kind)) {
    return true;
  }
  // A CARTER RIDES ON THE HORSE THE PLACEMENT GAVE HIM, and on no other
  // (boss, boss-core-topup-horses seq 2). Until 0.34.51 the labour hour let
  // every carter ride while the village had one horse anywhere, and this
  // function let none: a carter 2.5 km out was sent by the trot with every
  // horse in the plough, and his activity walked the road his pay rode.
  if (work.kind == WorkKind::kHauling) {
    return work.rides_horse != 0;
  }
  if (work.kind != WorkKind::kHarvest && work.kind != WorkKind::kSowing) {
    return false;
  }
  const std::uint32_t row = FindRow(world.fields, work.field);
  if (row == kNoRow) {
    return false;
  }
  const LandKind land = world.fields.rows[row].kind;
  // The same test the labour sub-step's CollectJobs sets `harnessed` by.
  if (work.kind == WorkKind::kHarvest &&
      (land == LandKind::kMeadow || land == LandKind::kFloodplainMeadow)) {
    return true;
  }
  // THE BRIGADE RIDES WITH ITS DRIVER (AssignmentJob::brigade_cart; 0.37.89):
  // the reaping of the arable and the sowing go out on the one cart the
  // placement took for the field, and the horse is written on the first hand
  // placed. No driver on the field today — the pool was dry — and they walk.
  return RidesTheBrigadesCart(work.kind, land) &&
         (work.rides_horse != 0 || BrigadeCartIsOut(world, work.kind, work.field));
}

TravelMode WorkTravelMode(const WorldState& world, const WorkAssignment& work) {
  if (!WorkRidesOut(world, work)) {
    return TravelMode::kWalk;
  }
  if (work.kind == WorkKind::kHauling) {
    // Logs off a stand, or a timber lot from the district (0.36.17): the log
    // cart; produce: the cart that keeps to the roads.
    const bool logs = work.stand.value != kInvalidEntityIdValue ||
                      work.limit_delivery.value != kInvalidEntityIdValue;
    return logs ? TravelMode::kLogCart : TravelMode::kCart;
  }
  return TravelMode::kTeam;
}

HarnessCount CountHarness(const WorldState& world) {
  HarnessCount count;
  std::vector<FieldId> meadows_mown;

  // The loads being carted, by their seam: riders and walkers apart, so the
  // walkers count only as far as the load wanted a cart (work_seam.h).
  struct Load {
    const float* seam = nullptr;
    std::uint32_t riders = 0;
    std::uint32_t walkers = 0;
  };

  std::vector<Load> loads;
  for (const ResidentRow& person : world.residents.rows) {
    const WorkAssignment& work = person.work;
    if (IsHorseWork(work.kind)) {
      ++count.in_traces;
      ++count.harnessed;
      ++count.releasable;
      continue;
    }
    if (work.kind == WorkKind::kHauling) {
      const float* const seam = WorkSeamOf(world, work);
      auto load = std::ranges::find_if(loads, [seam](const Load& one) { return one.seam == seam; });
      if (load == loads.end()) {
        loads.push_back(Load{.seam = seam});
        load = loads.end() - 1;
      }
      if (work.rides_horse != 0) {
        ++count.in_traces;
        ++count.releasable;
        ++count.harnessed;
        ++load->riders;
      } else {
        ++load->walkers;
      }
      continue;
    }
    if (work.kind != WorkKind::kHarvest && work.kind != WorkKind::kSowing) {
      continue;
    }
    const std::uint32_t row = FindRow(world.fields, work.field);
    if (row == kNoRow) {
      continue;
    }
    const LandKind land = world.fields.rows[row].kind;
    // THE BRIGADE'S CART (0.37.89): one horse a field, written on its driver.
    // The release takes it first — the brigade walks, the work goes on.
    if (RidesTheBrigadesCart(work.kind, land)) {
      if (work.rides_horse != 0) {
        ++count.in_traces;
        ++count.harnessed;
        ++count.releasable;
      }
      continue;
    }
    // A meadow's cut rides (WorkRidesOut), and its horse is the brigade's:
    // one a meadow, however many mow it.
    const bool meadow_cut = work.kind == WorkKind::kHarvest &&
                            (land == LandKind::kMeadow || land == LandKind::kFloodplainMeadow);
    if (meadow_cut && std::ranges::find(meadows_mown, work.field) == meadows_mown.end()) {
      meadows_mown.push_back(work.field);
      ++count.in_traces;
      ++count.harnessed;
    }
  }
  // THE WALKERS, AS FAR AS THE LOAD WANTED A CART (0.37.105): the seam's
  // norm-days beyond one a rider — a cart-day each while the settlement has
  // carts, a walker's day each while it has none, and then they all count.
  for (const Load& load : loads) {
    std::uint32_t wanted = load.walkers;  // a load with no seam left to read: as before
    if (load.seam != nullptr) {
      const float beyond = *load.seam - static_cast<float>(load.riders);
      wanted = beyond > 0.0F ? static_cast<std::uint32_t>(std::ceil(beyond)) : 0U;
    }
    count.harnessed += load.walkers < wanted ? load.walkers : wanted;
  }
  return count;
}

bool SettlementHasCarts(const WorldState& world, LivestockKindId horse_kind) {
  if (horse_kind.value == kInvalidDefIdValue) {
    return false;
  }
  return std::ranges::any_of(world.herds.rows, [horse_kind](const HerdRow& herd) {
    return herd.kind.value == horse_kind.value && herd.household_owned == 0 && herd.adult_count > 0;
  });
}

float* WorkSeamOf(WorldState& world, const WorkAssignment& work) {
  // One body, two constnesses: the const overload does the reasoning and
  // this one only gives the answer back writable. Two bodies would be two
  // rosters of work kinds, and the day a kind is added one of them would
  // still be right.
  return const_cast<float*>(WorkSeamOf(static_cast<const WorldState&>(world), work));
}

}  // namespace core
