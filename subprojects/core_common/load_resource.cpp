// What resource a logistics load is (core_common/load_resource.h).

#include "core_common/load_resource.h"

#include <cstdint>

#include "core_common/state_table_ops.h"

namespace core {
namespace {

/// The first resource by id `amounts` holds, invalid when all are nought.
ResourceId FirstHeld(const ResourceAmounts& amounts) {
  for (std::uint32_t index = 0; index < amounts.size(); ++index) {
    if (amounts[index] > 0) {
      return ResourceId{static_cast<std::uint16_t>(index)};
    }
  }
  return ResourceId{};
}

/// The front supply leg's amounts of the works `task` supplies, or null.
const ResourceAmounts* FrontLegOf(const WorldState& world, const LogisticsTaskRow& task) {
  const std::uint32_t row = FindRow(world.units, task.unit);
  if (row == kNoRow || world.units.rows[row].construction.legs.empty()) {
    return nullptr;
  }
  return &world.units.rows[row].construction.legs.front().grams;
}

}  // namespace

ResourceId MostGramsOf(const ResourceAmounts& amounts) {
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

ResourceId TaskLoadResource(const WorldState& world, const LogisticsTaskRow& task, ResourceId log) {
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
      return row != kNoRow ? MostGramsOf(world.limit_deliveries.rows[row].goods) : ResourceId{};
    }
    case LogisticsLoadKind::kStoreTransfer: {
      const std::uint32_t row = FindRow(world.units, task.unit);
      return row != kNoRow ? MostGramsOf(world.units.rows[row].stock) : ResourceId{};
    }
    case LogisticsLoadKind::kSiteSupply: {
      const ResourceAmounts* const front = FrontLegOf(world, task);
      return front != nullptr ? MostGramsOf(*front) : ResourceId{};
    }
    case LogisticsLoadKind::kMilkLeg: {
      const std::uint32_t row = FindRow(world.herds, task.herd);
      return row != kNoRow ? world.herds.rows[row].leg_resource : ResourceId{};
    }
    case LogisticsLoadKind::kRickSupply: {
      const std::uint32_t row = FindRow(world.units, task.unit);
      return row != kNoRow ? world.units.rows[row].rick_supply.resource : ResourceId{};
    }
    case LogisticsLoadKind::kManureField: {
      // The heap's own load (#1056 C2): the manure_pile keeps nothing else.
      const std::uint32_t field = FindRow(world.fields, task.field);
      const std::uint32_t heap =
          field != kNoRow ? FindRow(world.units, world.fields.rows[field].manure_dose.from)
                          : kNoRow;
      return heap != kNoRow ? MostGramsOf(world.units.rows[heap].stock) : ResourceId{};
    }
    case LogisticsLoadKind::kLogisticsLoadKindCount:
      break;
  }
  return ResourceId{};
}

ResourceId WalkerLoadResource(const WorldState& world,
                              const LogisticsTaskRow& task,
                              ResourceId log) {
  if (task.load_kind == LogisticsLoadKind::kSiteSupply) {
    const ResourceAmounts* const front = FrontLegOf(world, task);
    return front != nullptr ? FirstHeld(*front) : ResourceId{};
  }
  return TaskLoadResource(world, task, log);
}

ResourceId WalkerLoadResourceOfWork(const WorldState& world,
                                    const WorkAssignment& work,
                                    ResourceId log) {
  // BY THE WORK'S OWN SUBJECT, NOT BY ITS TASK ROW: the morning prices its
  // jobs before the day's tasks need exist, and a load with no task row read
  // as «no resource» would be priced as a sack in silence.
  if (work.kind != WorkKind::kHauling) {
    return ResourceId{};
  }
  LogisticsTaskRow as_task;
  if (work.field.value != kInvalidEntityIdValue) {
    // The reaped heap first; the winter dose only while no heap lies there
    // (#1056 C2; land_state.h, ManureDoseStands).
    const std::uint32_t row = FindRow(world.fields, work.field);
    as_task.load_kind = row != kNoRow && ManureDoseStands(world.fields.rows[row])
                            ? LogisticsLoadKind::kManureField
                            : LogisticsLoadKind::kFieldHeap;
    as_task.field = work.field;
  } else if (work.stand.value != kInvalidEntityIdValue) {
    as_task.load_kind = LogisticsLoadKind::kStandLogs;
    as_task.stand = work.stand;
  } else if (work.extraction_site.value != kInvalidEntityIdValue) {
    as_task.load_kind = LogisticsLoadKind::kSiteDig;
    as_task.extraction_site = work.extraction_site;
  } else if (work.limit_delivery.value != kInvalidEntityIdValue) {
    as_task.load_kind = LogisticsLoadKind::kDistrictLot;
    as_task.limit_delivery = work.limit_delivery;
  } else if (work.herd.value != kInvalidEntityIdValue) {
    // A hauling on a herd is its milk leg (#1056 C1.1).
    as_task.load_kind = LogisticsLoadKind::kMilkLeg;
    as_task.herd = work.herd;
  } else if (work.unit.value != kInvalidEntityIdValue) {
    // A unit being emptied is carted out first (work_seam.cpp); then its
    // works' supply; then a rick's supply from another rick (#1056 C1.2).
    const std::uint32_t row = FindRow(world.units, work.unit);
    as_task.load_kind = LogisticsLoadKind::kSiteSupply;
    if (row != kNoRow && world.units.rows[row].emptying != 0) {
      as_task.load_kind = LogisticsLoadKind::kStoreTransfer;
    } else if (row != kNoRow && RickSupplyStands(world.units.rows[row])) {
      as_task.load_kind = LogisticsLoadKind::kRickSupply;
    }
    as_task.unit = work.unit;
  } else {
    return ResourceId{};
  }
  return WalkerLoadResource(world, as_task, log);
}

}  // namespace core
