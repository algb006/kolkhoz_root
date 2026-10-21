// The seam of a work assignment (work_seam.h). Moved out of core_labor on
// 2026-09-05 when the resident's activity needed the same seven cases to
// tell "nobody gave him work" from "he was given work and there is nothing
// to work with".

#include "core_common/work_seam.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core_common/hauled_book.h"
#include "core_common/logistics_rules.h"
#include "core_common/module_rules.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"

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
  // THE MILK LEG (#1056 C1.1; logistics_state.h, kMilkLeg): a hauling on a
  // herd carts its barn's milk to the milk's store - the herd's own leg seam.
  if (work.kind == WorkKind::kHauling && work.herd.value != kInvalidEntityIdValue) {
    const std::uint32_t row = FindRow(world.herds, work.herd);
    return row == kNoRow ? nullptr : &world.herds.rows[row].leg_days_remaining;
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
    if (unit.emptying == 1) {
      return &unit.haul_days_remaining;
    }
    // THE SUPPLY OF A WORKS (routing stage V2): carting TO a unit whose works
    // have materials awaiting drains its supply seam — not while it is paused
    // (construction design §6). A store being emptied is carted OUT first:
    // one hauling work names one unit, and the order above is the chairman's.
    if (unit.emptying == 0 && unit.paused == 0 && AnythingAwaits(unit.construction)) {
      return &unit.construction.supply_days_remaining;
    }
    // A RICK'S SUPPLY FROM ANOTHER RICK (#1056 C1.2; unit_state.h,
    // RickSupplySeam): asked after the two above.
    return RickSupplyStands(unit) ? &unit.rick_supply.days_remaining : nullptr;
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
  // The herd's barn, for its care and for its milk leg (#1056 C1.1: a
  // hauling on a herd is loaded at the barn).
  if (work.kind == WorkKind::kHerdCare ||
      (work.kind == WorkKind::kHauling && work.herd.value != kInvalidEntityIdValue)) {
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
    const UnitRow& unit = world.units.rows[row];
    // The supply of a works (V2) is loaded at its SENDER: the carter's place
    // is there, as the labour sub-step offered it (labor_system.cpp).
    const bool supplying =
        work.kind == WorkKind::kHauling && unit.emptying == 0 && AnythingAwaits(unit.construction);
    place = supplying ? unit.construction.supply_from : unit.position;
    // A rick's supply is loaded at the rick it comes from (#1056 C1.2).
    if (work.kind == WorkKind::kHauling && !supplying && RickSupplyStands(unit)) {
      const std::uint32_t from_row = FindRow(world.units, unit.rick_supply.from);
      if (from_row == kNoRow) {
        return false;
      }
      place = world.units.rows[from_row].position;
    }
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

namespace {

/// One work and one target: the kind and every target id alike.
bool SameWorkAndTarget(const WorkAssignment& left, const WorkAssignment& right) {
  return left.kind == right.kind && left.field.value == right.field.value &&
         left.herd.value == right.herd.value && left.unit.value == right.unit.value &&
         left.stand.value == right.stand.value &&
         left.extraction_site.value == right.extraction_site.value &&
         left.limit_delivery.value == right.limit_delivery.value &&
         left.road_work.value == right.road_work.value;
}

}  // namespace

bool RidesThePeoplesCart(const WorldState& world, const WorkAssignment& work) {
  if (!TakesThePeoplesCart(work.kind)) {
    return false;
  }
  if (work.rides_horse != 0) {
    return true;  // its driver
  }
  const std::uint32_t row = FindRow(world.residents, work.rides_cart_of);
  if (row == kNoRow) {
    return false;
  }
  const WorkAssignment& driver = world.residents.rows[row].work;
  return driver.rides_horse != 0 && SameWorkAndTarget(driver, work);
}

bool WorkRidesOut(const WorldState& world, const WorkAssignment& work) {
  if (RidesOut(work.kind)) {
    return true;
  }
  if (TakesThePeoplesCart(work.kind)) {
    return RidesThePeoplesCart(world, work);
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
  // The same test the labour sub-step's CollectJobs sets `harnessed` by. The
  // mowers ride with their mower, written on the first of them, and with none
  // they walk (A4, 0.37.168).
  if (work.kind == WorkKind::kHarvest &&
      (land == LandKind::kMeadow || land == LandKind::kFloodplainMeadow)) {
    return work.rides_horse != 0 || BrigadeCartIsOut(world, work.kind, work.field);
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
    bool logs = work.stand.value != kInvalidEntityIdValue ||
                work.limit_delivery.value != kInvalidEntityIdValue;
    // AND LOGS STRAIGHT OFF A STAND TO A WORKS (the direct leg; save 147): the
    // supply's front leg says which cart it is (ConstructionState::
    // supply_log_way), as the seam's own rate does (field_haul.cpp, LegRate).
    if (!logs && work.unit.value != kInvalidEntityIdValue) {
      const std::uint32_t row = FindRow(world.units, work.unit);
      logs = row != kNoRow && world.units.rows[row].emptying == 0 &&
             AnythingAwaits(world.units.rows[row].construction) &&
             world.units.rows[row].construction.supply_log_way != 0;
    }
    return logs ? TravelMode::kLogCart : TravelMode::kCart;
  }
  return TravelMode::kTeam;
}

HarnessCount CountHarness(const WorldState& world) {
  return CountHarness(world, LivestockKindId{});
}

HarnessCount CountHarness(const WorldState& world, LivestockKindId horse_kind) {
  HarnessCount count;
  // With no grown horse every job walks for want of one (boss, [110]).
  const bool horses_kept = SettlementHasCarts(world, horse_kind);

  // The loads being carted, by their seam: riders and walkers apart, so the
  // walkers count only as far as the load wanted a cart (work_seam.h).
  struct Load {
    const float* seam = nullptr;
    std::uint32_t riders = 0;
    std::uint32_t walkers = 0;
    /// The load was priced ON FOOT by the carrier's choice - a milk leg whose
    /// walk holds the day's trips (HerdRow::leg_on_foot): it wants no cart.
    bool walked_by_choice = false;
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
        bool walked_by_choice = false;
        if (horses_kept && work.herd.value != kInvalidEntityIdValue) {
          const std::uint32_t herd_row = FindRow(world.herds, work.herd);
          walked_by_choice = herd_row != kNoRow && world.herds.rows[herd_row].leg_on_foot != 0;
        }
        loads.push_back(Load{.seam = seam, .walked_by_choice = walked_by_choice});
        load = loads.end() - 1;
      }
      if (work.rides_horse != 0) {
        ++count.in_traces;
        ++count.releasable;
        ++count.harnessed;
        ++count.light_in_traces;  // a cart or a sledge: light work
        ++load->riders;
      } else {
        ++load->walkers;
      }
      continue;
    }
    // THE CARTS THAT CARRY A CREW — the brigade's of the reaping and the
    // sowing (0.37.89), the people's (A3) and a meadow's mower (A4; 0.37.168):
    // one horse each, written on its driver. The release takes them first —
    // the crew walks, the work goes on. Until 0.37.168 a meadow being mown
    // counted one horse whether or not the pool had one for it.
    if ((TakesThePeoplesCart(work.kind) || work.kind == WorkKind::kHarvest ||
         work.kind == WorkKind::kSowing) &&
        work.rides_horse != 0) {
      ++count.in_traces;
      ++count.harnessed;
      ++count.releasable;
      // A crew's cart is light work; a MEADOW'S MOWER is not (the mowing
      // machine is field work, econ §A: «plough, harrow, mower stay full»).
      bool mower = false;
      if (work.kind == WorkKind::kHarvest) {
        const std::uint32_t field_row = FindRow(world.fields, work.field);
        mower = field_row != kNoRow && world.fields.rows[field_row].kind == LandKind::kMeadow;
      }
      count.light_in_traces += mower ? 0U : 1U;
    }
  }
  // THE WALKERS, AS FAR AS THE LOAD WANTED A CART (0.37.105): the seam's
  // norm-days beyond one a rider — a cart-day each while the settlement has
  // carts, a walker's day each while it has none, and then they all count.
  // A LOAD WALKED BY CHOICE WANTS NO CART (MR.2's trace; boss, the #1056
  // thread [109]: «Мало лошадей» is «нехватка тягла», time design): the milk
  // leg priced at a walker's trip is in walker-days, so every can-carrier
  // read as a cart-day pulled by hand - 6 670 noon walkers in the novice's
  // year 1 against the base's 476 - lit «too few horses», and the novice
  // bought ~120 horses that ate year 2's hay.
  for (const Load& load : loads) {
    if (load.walked_by_choice) {
      continue;
    }
    std::uint32_t wanted = load.walkers;  // a load with no seam left to read: as before
    if (load.seam != nullptr) {
      const float beyond = *load.seam - static_cast<float>(load.riders);
      wanted = beyond > 0.0F ? static_cast<std::uint32_t>(std::ceil(beyond)) : 0U;
    }
    count.harnessed += load.walkers < wanted ? load.walkers : wanted;
  }
  return count;
}

std::array<std::uint16_t, kCartUseCount> HorsesByUse(const WorldState& world,
                                                     std::uint32_t draught_horses) {
  std::array<std::uint32_t, kCartUseCount> held{};
  const auto add = [&held](CartUse use) { ++held[static_cast<std::size_t>(use)]; };
  const auto on_meadow = [&world](FieldId field) {
    const std::uint32_t row = FindRow(world.fields, field);
    if (row == kNoRow) {
      return false;
    }
    const LandKind land = world.fields.rows[row].kind;
    return land == LandKind::kMeadow || land == LandKind::kFloodplainMeadow;
  };
  // THE SAME HORSES CountHarness PUTS IN THE TRACES (above), classed: one a
  // ploughman or harrower, one a carter on a horse, one a crew's cart on its
  // driver.
  for (const ResidentRow& person : world.residents.rows) {
    const WorkAssignment& work = person.work;
    if (IsHorseWork(work.kind)) {
      add(CartUse::kPlough);
      continue;
    }
    if (work.rides_horse == 0) {
      continue;
    }
    if (work.kind == WorkKind::kHauling) {
      if (work.field.value != kInvalidEntityIdValue) {
        add(on_meadow(work.field) ? CartUse::kHay : CartUse::kFields);
        continue;
      }
      const std::uint32_t unit =
          work.unit.value != kInvalidEntityIdValue ? FindRow(world.units, work.unit) : kNoRow;
      const bool supply = unit != kNoRow && world.units.rows[unit].emptying == 0 &&
                          AnythingAwaits(world.units.rows[unit].construction);
      add(supply ? CartUse::kSiteSupply : CartUse::kOtherLoad);
      continue;
    }
    if (work.kind == WorkKind::kHarvest || work.kind == WorkKind::kSowing) {
      add(work.kind == WorkKind::kHarvest && on_meadow(work.field) ? CartUse::kHay
                                                                   : CartUse::kFields);
      continue;
    }
    if (TakesThePeoplesCart(work.kind)) {
      add(CartUse::kPeoplesCart);
    }
  }
  std::uint32_t in_use = 0;
  for (const std::uint32_t count : held) {
    in_use += count;
  }
  held[static_cast<std::size_t>(CartUse::kFree)] =
      draught_horses > in_use ? draught_horses - in_use : 0U;
  std::array<std::uint16_t, kCartUseCount> out{};
  for (std::size_t use = 0; use < kCartUseCount; ++use) {
    out[use] = static_cast<std::uint16_t>(std::min<std::uint32_t>(held[use], 0xFFFFU));
  }
  return out;
}

std::uint32_t DraughtHorsesOf(const WorldState& world, LivestockKindId horse_kind) {
  if (horse_kind.value == kInvalidDefIdValue) {
    return 0;
  }
  std::uint32_t horses = 0;
  for (const HerdRow& herd : world.herds.rows) {
    if (herd.kind.value == horse_kind.value) {
      horses += herd.adult_count;
    }
  }
  return horses;
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

float* WorkSeamWrittenOf(WorldState& world, const WorkAssignment& work) {
  float* const seam = WorkSeamOf(world, work);
  if (seam == nullptr || work.kind != WorkKind::kHauling) {
    return nullptr;
  }
  // The row the seam lies on, by its address: one roster (WorkSeamOf's), and
  // here only each remaining cell's written twin beside it.
  if (const std::uint32_t row = FindRow(world.herds, work.herd); row != kNoRow) {
    HerdRow& herd = world.herds.rows[row];
    return seam == &herd.leg_days_remaining ? &herd.leg_days_written : nullptr;
  }
  if (const std::uint32_t row = FindRow(world.units, work.unit); row != kNoRow) {
    UnitRow& unit = world.units.rows[row];
    if (seam == &unit.haul_days_remaining) {
      return &unit.haul_days_written;
    }
    if (seam == &unit.construction.supply_days_remaining) {
      return &unit.construction.supply_days_written;
    }
    return seam == &unit.rick_supply.days_remaining ? &unit.rick_supply.days_written : nullptr;
  }
  if (const std::uint32_t row = FindRow(world.limit_deliveries, work.limit_delivery);
      row != kNoRow) {
    LimitDeliveryRow& lot = world.limit_deliveries.rows[row];
    return seam == &lot.haul_days_remaining ? &lot.haul_days_written : nullptr;
  }
  if (const std::uint32_t row = FindRow(world.stands, work.stand); row != kNoRow) {
    TimberStandRow& stand = world.stands.rows[row];
    return seam == &stand.haul_days_remaining ? &stand.haul_days_written : nullptr;
  }
  if (const std::uint32_t row = FindRow(world.extraction_sites, work.extraction_site);
      row != kNoRow) {
    ExtractionSiteRow& site = world.extraction_sites.rows[row];
    return seam == &site.haul_days_remaining ? &site.haul_days_written : nullptr;
  }
  if (const std::uint32_t row = FindRow(world.fields, work.field); row != kNoRow) {
    FieldRow& field = world.fields.rows[row];
    return seam == &field.haul_days_remaining ? &field.haul_days_written : nullptr;
  }
  return nullptr;
}

float CoDrainCarriedTasks(WorldState& world, LogisticsTaskId carrier, float drained) {
  if (!(drained > 0.0F) || carrier.value == kInvalidEntityIdValue) {
    return 0.0F;
  }
  float total = 0.0F;
  for (std::uint32_t row = 0; row < world.logistics_tasks.rows.size(); ++row) {
    const LogisticsTaskRow& task = world.logistics_tasks.rows[row];
    if (task.carried_by.value != carrier.value || !(task.carried_share > 0.0F)) {
      continue;
    }
    const WorkAssignment carried = HaulingWorkOf(task);
    float* const seam = WorkSeamOf(world, carried);
    if (seam == nullptr || !(*seam > 0.0F)) {
      continue;
    }
    const float take = std::min(*seam, drained * task.carried_share);
    *seam -= take;
    total += take;
    // Who drained it, for production's settle (hauled_book.h): the carrier's cart.
    NoteHaulDrain(world, carried, take, /*on_foot=*/false);
  }
  return total;
}

// THE TRIP'S RECEIVER (stage В, F3 (A)): the `haul_to` production wrote with
// the load's seam (field_haul.cpp), or the works itself for its supply. A
// point at the map's origin is a receiver never written: no store stands in
// the corner of the map, and a row made before its first settlement holds the
// default. AN APPROXIMATION, NAMED: «unwritten» is read off the point, not off
// a flag of its own.
bool WorkReceiverOf(const WorldState& world, const WorkAssignment& work, Vec2& receiver) {
  if (work.kind != WorkKind::kHauling) {
    return false;
  }
  const auto written = [&receiver](Vec2 point) {
    if (point.x == 0.0F && point.y == 0.0F) {
      return false;
    }
    receiver = point;
    return true;
  };
  // The milk leg (#1056 C1.1): the milk's store production wrote with the seam.
  if (work.herd.value != kInvalidEntityIdValue) {
    const std::uint32_t row = FindRow(world.herds, work.herd);
    return row != kNoRow && written(world.herds.rows[row].leg_to);
  }
  if (work.unit.value != kInvalidEntityIdValue) {
    const std::uint32_t row = FindRow(world.units, work.unit);
    if (row == kNoRow) {
      return false;
    }
    const UnitRow& unit = world.units.rows[row];
    // A works' supply goes to the works, and a rick's supply to the rick
    // (#1056 C1.2); a store being emptied, to its store.
    return (unit.emptying == 0 && AnythingAwaits(unit.construction)) || RickSupplyStands(unit)
               ? written(unit.position)
               : written(unit.haul_to);
  }
  if (work.stand.value != kInvalidEntityIdValue) {
    const std::uint32_t row = FindRow(world.stands, work.stand);
    return row != kNoRow && written(world.stands.rows[row].haul_to);
  }
  if (work.limit_delivery.value != kInvalidEntityIdValue) {
    const std::uint32_t row = FindRow(world.limit_deliveries, work.limit_delivery);
    return row != kNoRow && written(world.limit_deliveries.rows[row].haul_to);
  }
  if (work.extraction_site.value != kInvalidEntityIdValue) {
    const std::uint32_t row = FindRow(world.extraction_sites, work.extraction_site);
    return row != kNoRow && written(world.extraction_sites.rows[row].haul_to);
  }
  if (work.field.value != kInvalidEntityIdValue) {
    const std::uint32_t row = FindRow(world.fields, work.field);
    return row != kNoRow && written(world.fields.rows[row].haul_to);
  }
  return false;
}

}  // namespace core
