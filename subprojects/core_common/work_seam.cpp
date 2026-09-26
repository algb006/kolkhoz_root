// The seam of a work assignment (work_seam.h). Moved out of core_labor on
// 2026-09-05 when the resident's activity needed the same seven cases to
// tell "nobody gave him work" from "he was given work and there is nothing
// to work with".

#include "core_common/work_seam.h"

#include <cstdint>

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
  if (work.kind != WorkKind::kHarvest) {
    return false;
  }
  // The same test the labour sub-step's CollectJobs sets `harnessed` by.
  const std::uint32_t row = FindRow(world.fields, work.field);
  return row != kNoRow && (world.fields.rows[row].kind == LandKind::kMeadow ||
                           world.fields.rows[row].kind == LandKind::kFloodplainMeadow);
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

float* WorkSeamOf(WorldState& world, const WorkAssignment& work) {
  // One body, two constnesses: the const overload does the reasoning and
  // this one only gives the answer back writable. Two bodies would be two
  // rosters of work kinds, and the day a kind is added one of them would
  // still be right.
  return const_cast<float*>(WorkSeamOf(static_cast<const WorldState&>(world), work));
}

}  // namespace core
