// A logistics task as a work assignment (core_common/logistics_rules.h).

#include "core_common/logistics_rules.h"

#include "core_common/ids.h"

namespace core {

WorkAssignment HaulingWorkOf(const LogisticsTaskRow& task) {
  WorkAssignment work;
  RetargetWork(work, task);
  work.kind = WorkKind::kHauling;
  return work;
}

bool WorkServesTask(const WorkAssignment& work, const LogisticsTaskRow& task) {
  if (work.kind != WorkKind::kHauling) {
    return false;
  }
  switch (task.load_kind) {
    case LogisticsLoadKind::kFieldHeap:
      return work.field.value == task.field.value;
    case LogisticsLoadKind::kStandLogs:
      return work.stand.value == task.stand.value;
    case LogisticsLoadKind::kSiteDig:
      return work.extraction_site.value == task.extraction_site.value;
    case LogisticsLoadKind::kDistrictLot:
      return work.limit_delivery.value == task.limit_delivery.value;
    case LogisticsLoadKind::kStoreTransfer:
      return work.unit.value == task.unit.value;
    case LogisticsLoadKind::kLogisticsLoadKindCount:
      break;
  }
  return false;
}

void RetargetWork(WorkAssignment& work, const LogisticsTaskRow& task) {
  work.field = FieldId{};
  work.herd = HerdId{};
  work.unit = UnitId{};
  work.stand = TimberStandId{};
  work.extraction_site = ExtractionSiteId{};
  work.limit_delivery = LimitDeliveryId{};
  work.road_work = RoadWorkId{};
  switch (task.load_kind) {
    case LogisticsLoadKind::kFieldHeap:
      work.field = task.field;
      break;
    case LogisticsLoadKind::kStandLogs:
      work.stand = task.stand;
      break;
    case LogisticsLoadKind::kSiteDig:
      work.extraction_site = task.extraction_site;
      break;
    case LogisticsLoadKind::kDistrictLot:
      work.limit_delivery = task.limit_delivery;
      break;
    case LogisticsLoadKind::kStoreTransfer:
      work.unit = task.unit;
      break;
    case LogisticsLoadKind::kLogisticsLoadKindCount:
      break;
  }
}

}  // namespace core
