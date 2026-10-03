// The groom's tasks' day (logistics_tasks.h).

#include "logistics_tasks.h"

#include <cstdint>
#include <vector>

#include "core_common/herd_state.h"
#include "core_common/labor_state.h"
#include "core_common/logistics_state.h"
#include "core_common/quantities.h"
#include "core_common/spoilage.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"

namespace core {
namespace {

/// A task's load, as one comparable address: the kind and the row id.
struct LoadAddress {
  LogisticsLoadKind kind = LogisticsLoadKind::kLogisticsLoadKindCount;
  std::uint32_t id = kInvalidEntityIdValue;
};

LoadAddress AddressOf(const LogisticsTaskRow& task) {
  switch (task.load_kind) {
    case LogisticsLoadKind::kFieldHeap:
      return {task.load_kind, task.field.value};
    case LogisticsLoadKind::kStandLogs:
      return {task.load_kind, task.stand.value};
    case LogisticsLoadKind::kSiteDig:
      return {task.load_kind, task.extraction_site.value};
    case LogisticsLoadKind::kDistrictLot:
      return {task.load_kind, task.limit_delivery.value};
    case LogisticsLoadKind::kStoreTransfer:
      return {task.load_kind, task.unit.value};
    case LogisticsLoadKind::kLogisticsLoadKindCount:
      break;
  }
  return {};
}

/// Every load lying in the world today, in the tables' row order.
std::vector<LoadAddress> LoadsOf(const WorldState& world) {
  std::vector<LoadAddress> loads;
  for (std::uint32_t row = 0; row < world.fields.rows.size(); ++row) {
    if (world.fields.rows[row].reaped_grams > 0) {
      loads.push_back({LogisticsLoadKind::kFieldHeap, world.fields.row_ids[row].value});
    }
  }
  for (std::uint32_t row = 0; row < world.stands.rows.size(); ++row) {
    if (world.stands.rows[row].load_grams > 0) {
      loads.push_back({LogisticsLoadKind::kStandLogs, world.stands.row_ids[row].value});
    }
  }
  for (std::uint32_t row = 0; row < world.extraction_sites.rows.size(); ++row) {
    if (world.extraction_sites.rows[row].load_grams > 0) {
      loads.push_back({LogisticsLoadKind::kSiteDig, world.extraction_sites.row_ids[row].value});
    }
  }
  for (std::uint32_t row = 0; row < world.limit_deliveries.rows.size(); ++row) {
    const LimitDeliveryRow& lot = world.limit_deliveries.rows[row];
    if (lot.own_carts != 0 && lot.haul_days_remaining > 0.0F) {
      loads.push_back({LogisticsLoadKind::kDistrictLot, world.limit_deliveries.row_ids[row].value});
    }
  }
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    if (world.units.rows[row].emptying != 0) {
      loads.push_back({LogisticsLoadKind::kStoreTransfer, world.units.row_ids[row].value});
    }
  }
  return loads;
}

bool Same(const LoadAddress& left, const LoadAddress& right) {
  return left.kind == right.kind && left.id == right.id;
}

LogisticsTaskRow TaskFor(const LogisticsConfig& config, const LoadAddress& load, SimDay today) {
  LogisticsTaskRow task;
  task.load_kind = load.kind;
  switch (load.kind) {
    case LogisticsLoadKind::kFieldHeap:
      task.field = FieldId{load.id};
      break;
    case LogisticsLoadKind::kStandLogs:
      task.stand = TimberStandId{load.id};
      break;
    case LogisticsLoadKind::kSiteDig:
      task.extraction_site = ExtractionSiteId{load.id};
      break;
    case LogisticsLoadKind::kDistrictLot:
      task.limit_delivery = LimitDeliveryId{load.id};
      break;
    case LogisticsLoadKind::kStoreTransfer:
      task.unit = UnitId{load.id};
      break;
    case LogisticsLoadKind::kLogisticsLoadKindCount:
      break;
  }
  const auto kind = static_cast<std::uint32_t>(load.kind);
  task.base_level =
      kind < config.default_level.size() ? config.default_level[kind] : LogisticsLevel::kOrdinary;
  task.level = task.base_level;
  task.origin = LogisticsOrigin::kAuto;
  task.aged_from_day = today;
  return task;
}

/// Whether this work is a carter's on this task's load.
bool Serves(const WorkAssignment& work, const LogisticsTaskRow& task) {
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

/// One step up the ageing: background -> ordinary -> term; a term never
/// ages into urgent (econ [5]: «иначе уровень 0 станет свалкой старых
/// сроков»).
LogisticsLevel AgedLevel(const LogisticsConfig& config, LogisticsLevel base, float days) {
  if (base == LogisticsLevel::kBackground) {
    if (days >= config.age_background_days + config.age_ordinary_days) {
      return LogisticsLevel::kTerm;
    }
    return days >= config.age_background_days ? LogisticsLevel::kOrdinary : base;
  }
  if (base == LogisticsLevel::kOrdinary && days >= config.age_ordinary_days) {
    return LogisticsLevel::kTerm;
  }
  return base;
}

/// A kolkhoz herd at a unit went underfed today (HerdRow::fed_share).
bool AHerdIsHungry(const WorldState& world) {
  for (const HerdRow& herd : world.herds.rows) {
    if (herd.household_owned == 0 && herd.unit.value != kInvalidEntityIdValue &&
        herd.adult_count > 0 && herd.fed_share < 1.0F) {
      return true;
    }
  }
  return false;
}

/// The threat a field's heap carries: tomorrow's spoilage at least the
/// share or the tonnes (the arithmetic of production's heaps,
/// field_haul.cpp SpoilFieldHeaps; keeping factor of a store STUB 1.0), or
/// feed while a herd is hungry.
bool HeapSpoils(const LogisticsConfig& config, const FieldRow& field) {
  const ResourceId resource = field.reaped_resource;
  if (field.reaped_grams <= 0 || resource.value >= config.spoil_days.size()) {
    return false;
  }
  const Grams gone = SpoiledToday(
      field.reaped_grams, config.spoil_days[resource.value], config.field_heap_keeping_factor);
  const auto share_line =
      static_cast<Grams>(config.urgent_spoil_share * static_cast<float>(field.reaped_grams));
  const Grams tonnes_line = GramsFromKilograms(config.urgent_spoil_tonnes * 1000.0F);
  return gone > 0 && (gone >= share_line || gone >= tonnes_line);
}

bool HeapIsFeed(const LogisticsConfig& config, const FieldRow& field) {
  const ResourceId resource = field.reaped_resource;
  return resource.value < config.feed.size() && config.feed[resource.value] != 0;
}

}  // namespace

void SyncTasks(const LogisticsConfig& config,
               WorldState& current,
               SimDay today,
               TaskDayCount& count) {
  const std::vector<LoadAddress> loads = LoadsOf(current);
  // The tasks whose load is gone, by id: removing one moves the last row.
  std::vector<LogisticsTaskId> gone;
  for (std::uint32_t row = 0; row < current.logistics_tasks.rows.size(); ++row) {
    const LoadAddress address = AddressOf(current.logistics_tasks.rows[row]);
    bool lies = false;
    for (const LoadAddress& load : loads) {
      lies = lies || Same(load, address);
    }
    if (!lies) {
      gone.push_back(current.logistics_tasks.row_ids[row]);
    }
  }
  for (const LogisticsTaskId task : gone) {
    RemoveRow(current.logistics_tasks, task);
    ++count.ended;
  }
  for (const LoadAddress& load : loads) {
    bool tasked = false;
    for (const LogisticsTaskRow& task : current.logistics_tasks.rows) {
      tasked = tasked || Same(AddressOf(task), load);
    }
    if (!tasked) {
      AppendRow(current.logistics_tasks, TaskFor(config, load, today));
      ++count.made;
    }
  }
}

void MarkServed(WorldState& current, SimDay today, TaskDayCount& count) {
  for (LogisticsTaskRow& task : current.logistics_tasks.rows) {
    for (const ResidentRow& person : current.residents.rows) {
      if (Serves(person.work, task)) {
        task.aged_from_day = today;
        ++count.served;
        break;
      }
    }
  }
}

void AgeAndRaise(const LogisticsConfig& config,
                 WorldState& current,
                 SimDay today,
                 Tick now,
                 TaskDayCount& count) {
  const bool hungry = AHerdIsHungry(current);
  for (LogisticsTaskRow& task : current.logistics_tasks.rows) {
    // A PAUSED TASK DOES NOT AGE: its count stands still (§12) — its start
    // moves with the days it is held.
    if (task.paused && task.aged_from_day < today) {
      ++task.aged_from_day;
    }
    const float days =
        today > task.aged_from_day ? static_cast<float>(today - task.aged_from_day) : 0.0F;
    const LogisticsLevel was = task.level;
    LogisticsLevel level = AgedLevel(config, task.base_level, days);
    // RAISED BY AGEING TODAY: the days crossed a step's line today.
    if (days >= 1.0F && AgedLevel(config, task.base_level, days - 1.0F) != level) {
      ++count.aged_up;
    }
    if (task.load_kind == LogisticsLoadKind::kFieldHeap) {
      const std::uint32_t row = FindRow(current.fields, task.field);
      if (row != kNoRow) {
        const FieldRow& field = current.fields.rows[row];
        if (HeapSpoils(config, field)) {
          level = LogisticsLevel::kUrgent;
          ++count.urgent_by_spoil;
        } else if (hungry && HeapIsFeed(config, field)) {
          level = LogisticsLevel::kUrgent;
          ++count.urgent_by_hunger;
        }
      }
    }
    // The player's level 0 stays (base_level kUrgent, B7).
    if (task.base_level == LogisticsLevel::kUrgent) {
      level = LogisticsLevel::kUrgent;
    }
    if (level == LogisticsLevel::kUrgent && was != LogisticsLevel::kUrgent) {
      task.urgent_since = now;
    }
    task.level = level;
  }
}

}  // namespace core
