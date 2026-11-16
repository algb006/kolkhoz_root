// The chairman's day orders of day_order_book.h (#1778).

#include "day_order_book.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <utility>
#include <vector>

#include "core_common/away_in_district.h"
#include "core_common/day_order_state.h"
#include "core_common/labor_state.h"
#include "core_common/order_state.h"
#include "core_common/resident_state.h"
#include "core_common/state_table_ops.h"
#include "work_orders.h"

namespace core {
namespace {

bool IsDayKind(OrderKind kind) {
  return kind == OrderKind::kAssignCrew || kind == OrderKind::kAssignDayWork;
}

/// @brief The due entry of an order, or nullptr.
DayOrderDue* DueOf(DayOrderBook& book, OrderId order) {
  for (DayOrderDue& due : book.due) {
    if (due.order.value == order.value) {
      return &due;
    }
  }
  return nullptr;
}

const DayOrderDue* DueOf(const DayOrderBook& book, OrderId order) {
  for (const DayOrderDue& due : book.due) {
    if (due.order.value == order.value) {
      return &due;
    }
  }
  return nullptr;
}

/// @brief The day orders' row for an id, accepted, or nullptr.
OrderRow* AcceptedRow(WorldState& world, OrderId id) {
  const std::uint32_t row = FindRow(world.orders, id);
  if (row == kNoRow || world.orders.rows[row].status != OrderStatus::kAccepted) {
    return nullptr;
  }
  return &world.orders.rows[row];
}

const OrderRow* AcceptedRow(const WorldState& world, OrderId id) {
  const std::uint32_t row = FindRow(world.orders, id);
  if (row == kNoRow || world.orders.rows[row].status != OrderStatus::kAccepted) {
    return nullptr;
  }
  return &world.orders.rows[row];
}

/// @brief Is `resident` held for `day` by a day order already: a crew's pick, or a day work?
bool HeldForDay(const WorldState& world, ResidentId resident, SimDay day) {
  const DayOrderBook& book = world.day_orders;
  for (const CrewPick& pick : book.picks) {
    const DayOrderDue* const due = DueOf(book, pick.crew);
    if (pick.resident.value == resident.value && due != nullptr && due->day == day) {
      return true;
    }
  }
  for (const DayOrderDue& due : book.due) {
    const OrderRow* const row = AcceptedRow(world, due.order);
    if (row != nullptr && row->kind == OrderKind::kAssignDayWork && due.day == day &&
        row->resident.value == resident.value) {
      return true;
    }
  }
  return false;
}

/// @brief May a crew pick `row`'s resident for `day`?
bool FreeForCrew(const LaborConfig& config,
                 const WorldState& world,
                 std::uint32_t row,
                 const OrderRow& crew,
                 SimDay day) {
  const ResidentRow& resident = world.residents.rows[row];
  const ResidentId id = world.residents.row_ids[row];
  if (resident.family.value == kInvalidEntityIdValue || OffWork(resident, world.calendar.tick)) {
    return false;
  }
  // The standing order's own check, asked as if the order named him: alive, of age, no post
  // and no appointment waiting.
  OrderRow probe = crew;
  probe.kind = OrderKind::kAssignWork;
  probe.resident = id;
  if (CheckAssignWork(config, world, probe) != OrderRefusal::kNone) {
    return false;
  }
  return StandingWorkRow(world, id, kNoRow) == kNoRow && !HeldForDay(world, id, day);
}

/// @brief Picks up to `crew.crew_size` free residents for `day` by the chairman's level.
void PickCrew(
    const LaborConfig& config, WorldState& current, const OrderRow& crew, OrderId id, SimDay day) {
  const auto roster = static_cast<std::uint32_t>(current.residents.rows.size());
  if (roster == 0) {
    return;
  }
  // Level 0 «по наличию»: the accountant's rotation (AssignmentParams::rotation) - rows from
  // the day's offset, wrapping. STUB: levels 1-3 the same (day_order_book.h).
  const std::uint32_t start = day % roster;
  std::uint32_t picked = 0;
  for (std::uint32_t step = 0; step < roster && picked < crew.crew_size; ++step) {
    const std::uint32_t row = (start + step) % roster;
    if (!FreeForCrew(config, current, row, crew, day)) {
      continue;
    }
    current.day_orders.picks.push_back(
        CrewPick{.crew = id, .resident = current.residents.row_ids[row]});
    ++picked;
  }
}

/// @brief Takes `resident`'s picks for `day` out of every crew.
void UnpickForDay(WorldState& current, ResidentId resident, SimDay day) {
  DayOrderBook& book = current.day_orders;
  std::erase_if(book.picks, [&](const CrewPick& pick) {
    const DayOrderDue* const due = DueOf(book, pick.crew);
    return pick.resident.value == resident.value && due != nullptr && due->day == day;
  });
}

DayOrderLine LineOf(const OrderRow& row) {
  DayOrderLine line;
  line.form = row.kind == OrderKind::kAssignCrew ? DayOrderForm::kCrew : DayOrderForm::kNamed;
  line.work = row.work;
  line.field = row.field;
  line.herd = row.herd;
  line.unit = row.unit;
  line.count = row.kind == OrderKind::kAssignCrew ? row.crew_size : 1U;
  line.resident = row.kind == OrderKind::kAssignDayWork ? row.resident : ResidentId{};
  return line;
}

}  // namespace

void ReadDayOrders(const LaborConfig& config, WorldState& current) {
  for (std::uint32_t row = 0; row < current.orders.rows.size(); ++row) {
    OrderRow& order = current.orders.rows[row];
    if (order.status != OrderStatus::kPending || !IsDayKind(order.kind)) {
      continue;
    }
    const OrderId id = current.orders.row_ids[row];
    const SimDay day = current.calendar.day + 1U;
    const OrderRefusal refusal = order.kind == OrderKind::kAssignCrew
                                     ? CheckWorkTarget(current, order)
                                     : CheckAssignWork(config, current, order);
    if (refusal != OrderRefusal::kNone) {
      order.status = OrderStatus::kRefused;
      order.refusal = refusal;
      continue;
    }
    if (order.kind == OrderKind::kAssignDayWork) {
      // A newer day work for the same man cancels the older one (the standing order's rule),
      // and his name leaves any crew's picks for that day.
      for (const DayOrderDue& due : current.day_orders.due) {
        OrderRow* const older = AcceptedRow(current, due.order);
        if (older != nullptr && older->kind == OrderKind::kAssignDayWork &&
            older->resident.value == order.resident.value) {
          older->status = OrderStatus::kCancelled;
          older->refusal = OrderRefusal::kNone;
        }
      }
      UnpickForDay(current, order.resident, day);
    }
    order.status = OrderStatus::kAccepted;
    order.refusal = OrderRefusal::kNone;
    current.day_orders.due.push_back(DayOrderDue{.order = id, .day = day});
    if (order.kind == OrderKind::kAssignCrew) {
      PickCrew(config, current, order, id, day);
    }
  }
  // A cancelled or refused day order leaves the book with its picks.
  DayOrderBook& book = current.day_orders;
  std::erase_if(book.picks,
                [&](const CrewPick& pick) { return AcceptedRow(current, pick.crew) == nullptr; });
  std::erase_if(book.due,
                [&](const DayOrderDue& due) { return AcceptedRow(current, due.order) == nullptr; });
}

bool ReleaseFromDayOrders(WorldState& current, ResidentId resident) {
  DayOrderBook& book = current.day_orders;
  const std::size_t picks_before = book.picks.size();
  std::erase_if(book.picks,
                [&](const CrewPick& pick) { return pick.resident.value == resident.value; });
  bool released = book.picks.size() != picks_before;
  for (const DayOrderDue& due : book.due) {
    OrderRow* const row = AcceptedRow(current, due.order);
    if (row != nullptr && row->kind == OrderKind::kAssignDayWork &&
        row->resident.value == resident.value) {
      row->status = OrderStatus::kDone;  // it ran as ordered and was stood down
      row->refusal = OrderRefusal::kNone;
      released = true;
    }
  }
  return released;
}

std::vector<OrderedWork> OrderedWorksToday(const WorldState& world) {
  std::vector<OrderedWork> works;
  for (const OrderRow& order : world.orders.rows) {
    if (order.kind == OrderKind::kAssignWork && order.status == OrderStatus::kAccepted) {
      works.push_back(OrderedWork{.order = &order, .resident = order.resident});
    }
  }
  const SimDay today = world.calendar.day;
  for (const CrewPick& pick : world.day_orders.picks) {
    const DayOrderDue* const due = DueOf(world.day_orders, pick.crew);
    const OrderRow* const crew = AcceptedRow(world, pick.crew);
    if (due != nullptr && due->day == today && crew != nullptr) {
      works.push_back(OrderedWork{.order = crew, .resident = pick.resident});
    }
  }
  for (const DayOrderDue& due : world.day_orders.due) {
    const OrderRow* const row = AcceptedRow(world, due.order);
    if (row != nullptr && row->kind == OrderKind::kAssignDayWork && due.day == today) {
      works.push_back(OrderedWork{.order = row, .resident = row->resident});
    }
  }
  return works;
}

namespace {

/// @brief Does `works[index]` ask a horse of the pool: a ploughman or harrower always, a carter
///        while his order's horses last (counted along the list).
std::vector<bool> HorseWanted(const std::vector<OrderedWork>& works) {
  std::vector<bool> wanted(works.size(), false);
  std::vector<std::pair<const OrderRow*, std::uint16_t>> given;
  for (std::size_t index = 0; index < works.size(); ++index) {
    const OrderRow& order = *works[index].order;
    if (IsHorseWork(order.work)) {
      wanted[index] = true;
      continue;
    }
    if (order.work != WorkKind::kHauling || order.horses == 0) {
      continue;
    }
    auto found =
        std::ranges::find_if(given, [&](const auto& entry) { return entry.first == &order; });
    if (found == given.end()) {
      given.emplace_back(&order, std::uint16_t{0});
      found = std::prev(given.end());
    }
    if (found->second < order.horses) {
      ++found->second;
      wanted[index] = true;
    }
  }
  return wanted;
}

}  // namespace

std::uint32_t HorsesOrdered(const std::vector<OrderedWork>& works) {
  const std::vector<bool> wanted = HorseWanted(works);
  return static_cast<std::uint32_t>(std::ranges::count(wanted, true));
}

void GiveOrderedHorses(std::vector<OrderedWork>& works, std::uint32_t claimed) {
  const std::vector<bool> wanted = HorseWanted(works);
  for (std::size_t index = 0; index < works.size() && claimed > 0; ++index) {
    if (wanted[index]) {
      works[index].horse = true;
      --claimed;
    }
  }
}

void CloseDayOrders(WorldState& current, bool day_off) {
  DayOrderBook& book = current.day_orders;
  const SimDay today = current.calendar.day;
  if (day_off) {
    for (DayOrderDue& due : book.due) {
      if (due.day == today) {
        due.day = today + 1U;  // the next working day
      }
    }
    return;
  }
  std::vector<DayOrderLine> closed;
  for (const DayOrderDue& due : book.due) {
    if (due.day != today) {
      continue;
    }
    OrderRow* const row = AcceptedRow(current, due.order);
    if (row == nullptr) {
      continue;
    }
    closed.push_back(LineOf(*row));
    row->status = OrderStatus::kDone;
    row->refusal = OrderRefusal::kNone;
  }
  std::erase_if(book.picks, [&](const CrewPick& pick) {
    const DayOrderDue* const due = DueOf(book, pick.crew);
    return due == nullptr || due->day == today;
  });
  std::erase_if(book.due, [&](const DayOrderDue& due) { return due.day == today; });
  book.yesterday = std::move(closed);
  book.yesterday_day = today;
  book.yesterday_known = true;
}

CrewView CrewNamesOf(const WorldState& state, OrderId crew) {
  CrewView view;
  view.measured = true;
  const OrderRow* const row = AcceptedRow(state, crew);
  const DayOrderDue* const due = DueOf(state.day_orders, crew);
  if (row == nullptr || row->kind != OrderKind::kAssignCrew || due == nullptr) {
    return view;  // `crew` invalid: no accepted crew by that id
  }
  view.crew = crew;
  view.work = row->work;
  view.field = row->field;
  view.herd = row->herd;
  view.unit = row->unit;
  view.asked = row->crew_size;
  view.day = due->day;
  for (const CrewPick& pick : state.day_orders.picks) {
    if (pick.crew.value == crew.value) {
      view.names.push_back(pick.resident);
    }
  }
  return view;
}

YesterdayDayOrders YesterdayDayOrdersIn(const WorldState& state) {
  YesterdayDayOrders view;
  view.measured = true;
  if (state.day_orders.yesterday_known) {
    view.day = state.day_orders.yesterday_day;
    view.lines = state.day_orders.yesterday;
  }
  return view;
}

}  // namespace core
