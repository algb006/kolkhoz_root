// The groom's tasks' day (logistics_tasks.h).

#include "logistics_tasks.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "core_common/herd_state.h"
#include "core_common/labor_state.h"
#include "core_common/logistics_rules.h"
#include "core_common/logistics_state.h"
#include "core_common/quantities.h"
#include "core_common/site_supply_queue.h"
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
    case LogisticsLoadKind::kSiteSupply:
    case LogisticsLoadKind::kRickSupply:
      return {task.load_kind, task.unit.value};
    case LogisticsLoadKind::kMilkLeg:
      return {task.load_kind, task.herd.value};
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
    // THE MUD CLOSES THE DISTRICT's ROAD to own carts both ways (routing stage
    // В5): the labour's offer of the same job asks it too (labor_system.cpp) -
    // asked at one door only, the other sent carters through the mud.
    if (lot.own_carts != 0 && lot.haul_days_remaining > 0.0F && !world.weather.mud) {
      loads.push_back({LogisticsLoadKind::kDistrictLot, world.limit_deliveries.row_ids[row].value});
    }
  }
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    if (world.units.rows[row].emptying != 0) {
      loads.push_back({LogisticsLoadKind::kStoreTransfer, world.units.row_ids[row].value});
    }
  }
  // The supply of a works (V2): while anything fixed for it awaits and it is
  // neither paused nor under an emptying order (the labour sub-step offers it
  // by the same three conditions) - EVERY such site, in the queue's order
  // (#595, core_common/site_supply_queue.h): the order is read again where it
  // acts, in the plan's OpenTasks; the list's order here only seats the
  // tasks' rows the same way.
  for (const std::uint32_t row : SiteSupplyOrder(world)) {
    loads.push_back({LogisticsLoadKind::kSiteSupply, world.units.row_ids[row].value});
  }
  // THE MILK LEG (#1056 C1.1): one task a milked kolkhoz herd, standing while
  // the herd has a leg at all - keyed on the herd, not on today's milk, which
  // the herd day lays in the barn after this hour-0 sync.
  for (std::uint32_t row = 0; row < world.herds.rows.size(); ++row) {
    if (world.herds.rows[row].leg_resource.value != kInvalidDefIdValue) {
      loads.push_back({LogisticsLoadKind::kMilkLeg, world.herds.row_ids[row].value});
    }
  }
  // A RICK'S SUPPLY (#1056 C1.2): one task a receiving rick while its supply
  // stands, by the labour's own condition (unit_state.h, RickSupplyStands).
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    const UnitRow& unit = world.units.rows[row];
    if (RickSupplyStands(unit) && unit.rick_supply.grams > 0) {
      loads.push_back({LogisticsLoadKind::kRickSupply, world.units.row_ids[row].value});
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
    case LogisticsLoadKind::kSiteSupply:
    case LogisticsLoadKind::kRickSupply:
      task.unit = UnitId{load.id};
      break;
    case LogisticsLoadKind::kMilkLeg:
      task.herd = HerdId{load.id};
      break;
    case LogisticsLoadKind::kLogisticsLoadKindCount:
      break;
  }
  const auto kind = static_cast<std::uint32_t>(load.kind);
  task.base_level =
      kind < config.default_level.size() ? config.default_level[kind] : LogisticsLevel::kOrdinary;
  task.level = task.base_level;
  task.urgent_by =
      task.level == LogisticsLevel::kUrgent ? LogisticsUrgentBy::kAged : LogisticsUrgentBy::kNone;
  task.origin = LogisticsOrigin::kAuto;
  task.aged_from_day = today;
  return task;
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

/// A kolkhoz herd went underfed today (HerdRow::fed_share) - AT ITS UNIT OR
/// BILLETED (0.37.245; boss, 13 October 2026). Until then only a herd with a
/// unit counted: the cows standing in the yards before their own yard is
/// built eat the farm's fodder as any herd does (herd_system.cpp, the stock
/// yard's fallback) and starved as any does, but raised no heap - and with
/// the December hay gone (0.37.244) the first winter's herds were short from
/// March and died in May and June of year 2 while the first scythes' hay lay
/// on the meadow at level 1.
bool AHerdIsHungry(const WorldState& world) {
  for (const HerdRow& herd : world.herds.rows) {
    if (herd.household_owned == 0 && herd.adult_count > 0 && herd.fed_share < 1.0F) {
      return true;
    }
  }
  return false;
}

/// The threat a field's heap carries (#1200; econ, hay-heap-shelf-gates §4b):
/// its loss over its expected wait at least the config's share of itself,
/// crop-blind. Until #1200 it was tomorrow's loss against the share or 1 t
/// (urgent_spoil_tonnes): the 1 t line raised a 40 t hay heap above the
/// harvest, and every crop's heap waited behind it (core-boss-oats-b-pair
/// [133]: wheat +35-48 % rot, vegetables +46-58 %, the novice's snow +35 t).
bool HeapSpoils(const LogisticsConfig& config, const WorldState& world, std::uint32_t field_row) {
  const FieldRow& field = world.fields.rows[field_row];
  if (field.reaped_grams <= 0) {
    return false;
  }
  const auto share_line =
      static_cast<Grams>(config.urgent_spoil_share * static_cast<float>(field.reaped_grams));
  const Grams lost = ExpectedHeapLoss(config, world, field_row);
  return lost > 0 && lost >= share_line;
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
  // THE RICK'S THRESHOLD, copied as it was priced (#1056 C1.2): a second
  // home that stores the value, bound to the rick's row every sync.
  for (LogisticsTaskRow& task : current.logistics_tasks.rows) {
    if (task.load_kind != LogisticsLoadKind::kRickSupply) {
      continue;
    }
    const std::uint32_t row = FindRow(current.units, task.unit);
    task.threshold_days = row != kNoRow ? current.units.rows[row].rick_supply.threshold_days : 0;
  }
}

std::uint32_t AppendNewHeapTasks(const LogisticsConfig& config, WorldState& current, SimDay today) {
  std::uint32_t made = 0;
  for (const LoadAddress& load : LoadsOf(current)) {
    if (load.kind != LogisticsLoadKind::kFieldHeap) {
      continue;
    }
    bool tasked = false;
    for (const LogisticsTaskRow& task : current.logistics_tasks.rows) {
      tasked = tasked || Same(AddressOf(task), load);
    }
    if (!tasked) {
      AppendRow(current.logistics_tasks, TaskFor(config, load, today));
      ++made;
    }
  }
  return made;
}

void MarkServed(WorldState& current, SimDay today, TaskDayCount& count) {
  for (LogisticsTaskRow& task : current.logistics_tasks.rows) {
    for (const ResidentRow& person : current.residents.rows) {
      if (WorkServesTask(person.work, task)) {
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
    // WHY LEVEL 0 (#1099, save 174): written beside the level, the last
    // reason below winning - the player's over the rot, as the level itself.
    LogisticsUrgentBy urgent_by =
        level == LogisticsLevel::kUrgent ? LogisticsUrgentBy::kAged : LogisticsUrgentBy::kNone;
    // RAISED BY AGEING TODAY: the days crossed a step's line today.
    if (days >= 1.0F && AgedLevel(config, task.base_level, days - 1.0F) != level) {
      ++count.aged_up;
    }
    if (task.load_kind == LogisticsLoadKind::kFieldHeap) {
      const std::uint32_t row = FindRow(current.fields, task.field);
      if (row != kNoRow) {
        const FieldRow& field = current.fields.rows[row];
        if (HeapSpoils(config, current, row)) {
          level = LogisticsLevel::kUrgent;
          urgent_by = LogisticsUrgentBy::kSpoil;
          ++count.urgent_by_spoil;
        } else if (hungry && HeapIsFeed(config, field)) {
          level = LogisticsLevel::kUrgent;
          urgent_by = LogisticsUrgentBy::kHunger;
          ++count.urgent_by_hunger;
        }
      }
    }
    // «МАТЕРИАЛЫ ДЛЯ СТРОЙКИ, КОТОРАЯ СТОИТ» (transport §12, level 1) is the
    // supply task's own level (kSiteSupply): what lies at a pit or a stand
    // for a works is that works' leg (the direct leg), carted by its task. A
    // pit's dig and a stand's logs go by the round.
    // The player's level 0 stays (base_level kUrgent, B7).
    if (task.base_level == LogisticsLevel::kUrgent) {
      level = LogisticsLevel::kUrgent;
      urgent_by = LogisticsUrgentBy::kPlayer;
    }
    // A RAISED SITE'S SUPPLY KEEPS ITS OWN LEVEL (#709's pair; boss, 12
    // October 2026; transport design §12: a carter's request closes «на месте
    // окна своего груза в общей очереди работ», a works' supply «на месте
    // самой стройки… после открытого окна», and «Поднять в очереди» orders
    // the sites AMONG THEMSELVES - «очередь — это порядок, а не запрет»).
    // From 0.37.232 a raised site's supply was put at level 0, and the
    // morning marks a load at level 0 urgent - a tier above every open
    // window. With the run chairman's housing lever (host's pair, nine
    // villages) the first year lost 35.8 t of 1936's potatoes to the snow
    // at level 0 and 0.0 t at the task's own level; the morning's queue on
    // days 37-41 showed the raised supply WITHOUT hands, so the path to the
    // potatoes was not the placement's tier and was not traced. The raise
    // now orders the sites only: first among the site supplies in the plan
    // (SiteSupplyOrder) and in the placement's tier (AssignmentJob::raised,
    // SiteSupplyRank).
    if (level == LogisticsLevel::kUrgent && was != LogisticsLevel::kUrgent) {
      task.urgent_since = now;
    }
    task.level = level;
    task.urgent_by = urgent_by;
  }
}

Grams ExpectedHeapLoss(const LogisticsConfig& config,
                       const WorldState& world,
                       std::uint32_t field_row) {
  if (field_row >= world.fields.rows.size()) {
    return 0;
  }
  const FieldRow& field = world.fields.rows[field_row];
  if (field.reaped_grams <= 0) {
    return 0;
  }
  const Grams today = FieldHeapLossToday(config.spoil_days,
                                         config.field_heap_keeping_factor,
                                         config.hay_weathering,
                                         field.reaped_resource,
                                         static_cast<std::uint32_t>(world.calendar.date.month),
                                         field.reaped_grams);
  if (today <= 0) {
    return 0;
  }
  // The carts on it today: haulers on a horse whose work is this field.
  const FieldId id = world.fields.row_ids[field_row];
  std::uint32_t carts = 0;
  for (const ResidentRow& resident : world.residents.rows) {
    const WorkAssignment& work = resident.work;
    carts +=
        work.kind == WorkKind::kHauling && work.rides_horse != 0 && work.field.value == id.value;
  }
  // At least the night ahead: a heap is carted tomorrow at the earliest.
  const float wait = std::max(
      std::max(field.haul_days_remaining, 0.0F) / static_cast<float>(std::max(carts, 1U)), 1.0F);
  const double share =
      static_cast<double>(today) / static_cast<double>(field.reaped_grams);  // the day's share
  const double kept = std::pow(1.0 - share, static_cast<double>(wait));
  return static_cast<Grams>(std::llround(static_cast<double>(field.reaped_grams) * (1.0 - kept)));
}

}  // namespace core
