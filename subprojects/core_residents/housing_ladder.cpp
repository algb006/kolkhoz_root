// The ladder of a family without a roof (housing_ladder.h).

#include "housing_ladder.h"

#include <cstdint>
#include <limits>
#include <vector>

#include "core_common/emit_event.h"
#include "core_common/family_state.h"
#include "core_common/order_state.h"
#include "core_common/resident_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "demography.h"
#include "housing.h"

namespace core {
namespace {

bool IsHousing(const LifeConfig& config, UnitTypeId type) {
  return type.value < config.definitions.units.is_housing.size() &&
         config.definitions.units.is_housing[type.value] != 0;
}

/// The family's own roof stands.
bool HasHouse(const WorldState& world, const FamilyRow& family) {
  return family.house.value != kInvalidEntityIdValue &&
         FindRow(world.units, family.house) != kNoRow;
}

/// Into `house`, off the ladder: the tent struck, the lodging left, the
/// request forgotten.
void MoveIn(WorldState& current, std::uint32_t family_row, UnitId house) {
  const std::uint32_t house_row = FindRow(current.units, house);
  FamilyRow& family = current.families.rows[family_row];
  current.units.rows[house_row].household = current.families.row_ids[family_row];
  family.house = house;
  family.lost_house_position = current.units.rows[house_row].position;
  family.in_tent = 0;
  family.lodged_in = UnitId{};
  family.asked_to_leave = 0;
}

/// With the certificate: every member leaves the kolkhoz for good (the path
/// the ladder's fourth rung took by itself until 2026-09-19).
void SendAway(WorldState& current, FamilyId family) {
  std::vector<ResidentId> members;
  for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
    if (current.residents.rows[row].family.value == family.value) {
      members.push_back(current.residents.row_ids[row]);
    }
  }
  SimEvent& gone =
      EmitEvent(current, EventKind::kFamilyLeftForNoHouse, EventSeverity::kInterrupting);
  gone.family = family;
  gone.amount = static_cast<std::int64_t>(members.size());
  for (const ResidentId member : members) {
    SimEvent& left = EmitEvent(current, EventKind::kResidentLeft, EventSeverity::kNotable);
    left.resident = member;
    left.family = family;
    RemoveResident(current, member);
  }
  current.ledger.current.departures += static_cast<std::uint32_t>(members.size());
  // A family with nobody in it was dropped by the last RemoveResident; one
  // that had nobody to begin with goes here.
  DropFamilyIfEmpty(current, family);
}

/// Some family is lodged in `house` already.
bool Hosts(const WorldState& world, UnitId house) {
  for (const FamilyRow& family : world.families.rows) {
    if (family.lodged_in.value == house.value) {
      return true;
    }
  }
  return false;
}

/// Some member of `guest` is close kin (AreCloseKin) of some member of `host`.
bool KinOf(const WorldState& world, FamilyId guest, FamilyId host) {
  for (std::uint32_t a = 0; a < world.residents.rows.size(); ++a) {
    if (world.residents.rows[a].family.value != guest.value) {
      continue;
    }
    for (std::uint32_t b = 0; b < world.residents.rows.size(); ++b) {
      if (world.residents.rows[b].family.value == host.value &&
          AreCloseKin(world.residents.rows[a],
                      world.residents.row_ids[a],
                      world.residents.rows[b],
                      world.residents.row_ids[b])) {
        return true;
      }
    }
  }
  return false;
}

/// Where a refused family is lodged (§20; boss seq 197): kin's house first,
/// then the nearest neighbour's — one lodged family to a house while any
/// other is free; all taken, the nearest house anyway («никаких
/// безвыходных»). Nearest to the plot its own house stood on; ties go to row
/// order. Invalid when no house has anybody in it.
UnitId ChooseLodging(const WorldState& world, std::uint32_t family_row) {
  const FamilyId guest = world.families.row_ids[family_row];
  const Vec2 from = world.families.rows[family_row].lost_house_position;
  // Pass 0: kin, not hosting. Pass 1: anybody, not hosting. Pass 2: anybody.
  for (int pass = 0; pass < 3; ++pass) {
    UnitId best;
    float best_distance = std::numeric_limits<float>::max();
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const UnitRow& unit = world.units.rows[row];
      const FamilyId host = unit.household;
      if (host.value == kInvalidEntityIdValue || host.value == guest.value || unit.level == 0) {
        continue;
      }
      const UnitId house = world.units.row_ids[row];
      if ((pass < 2 && Hosts(world, house)) || (pass == 0 && !KinOf(world, guest, host))) {
        continue;
      }
      const float dx = unit.position.x - from.x;
      const float dy = unit.position.y - from.y;
      const float distance = (dx * dx) + (dy * dy);
      if (distance < best_distance) {
        best_distance = distance;
        best = house;
      }
    }
    if (best.value != kInvalidEntityIdValue) {
      return best;
    }
  }
  return UnitId{};
}

/// Refused: lodged. A village with no house lived in has nowhere to lodge
/// anybody: the refusal cannot be carried out, and the family leaves as if
/// signed (boss seq 199 (в): «никаких безвыходных»).
void Lodge(WorldState& current, std::uint32_t family_row) {
  const UnitId host = ChooseLodging(current, family_row);
  if (host.value == kInvalidEntityIdValue) {
    SendAway(current, current.families.row_ids[family_row]);
    return;
  }
  FamilyRow& family = current.families.rows[family_row];
  family.lodged_in = host;
  family.asked_to_leave = 0;
  family.in_tent = 0;
  SimEvent& lodged = EmitEvent(current, EventKind::kFamilyLodged, EventSeverity::kNotable);
  lodged.family = current.families.row_ids[family_row];
  lodged.unit = host;
}

/// The cost of lodging (boss seq 199-200): the guest and its host carry
/// `lodging_satisfaction_penalty` off their satisfaction while it lasts — a
/// level, set each morning; a family neither lodged nor hosting carries none.
void ChargeLodging(const LifeConfig& config, WorldState& current) {
  std::vector<std::uint8_t> hosting(current.families.rows.size(), 0);
  for (const FamilyRow& guest : current.families.rows) {
    if (guest.lodged_in.value == kInvalidEntityIdValue) {
      continue;
    }
    const std::uint32_t house_row = FindRow(current.units, guest.lodged_in);
    if (house_row == kNoRow) {
      continue;
    }
    const std::uint32_t host_row =
        FindRow(current.families, current.units.rows[house_row].household);
    if (host_row != kNoRow) {
      hosting[host_row] = 1;
    }
  }
  for (std::uint32_t row = 0; row < current.families.rows.size(); ++row) {
    FamilyRow& family = current.families.rows[row];
    const bool lodged = family.lodged_in.value != kInvalidEntityIdValue;
    family.lodging_penalty =
        lodged || hosting[row] != 0 ? config.lodging_satisfaction_penalty : 0.0F;
  }
}

OrderRefusal ReserveHouse(const LifeConfig& config, WorldState& current, const OrderRow& order) {
  const std::uint32_t row = FindRow(current.units, order.unit);
  if (row == kNoRow) {
    return OrderRefusal::kNoSuchSubject;
  }
  UnitRow& unit = current.units.rows[row];
  if (!IsHousing(config, unit.type) || unit.level == 0 || unit.dead != 0) {
    return OrderRefusal::kNotEligible;
  }
  if (order.enable != 0 && unit.household.value != kInvalidEntityIdValue) {
    return OrderRefusal::kNotEmpty;
  }
  unit.reserved_for_specialist = order.enable != 0 ? 1U : 0U;
  return OrderRefusal::kNone;
}

OrderRefusal AnswerLeaveRequest(WorldState& current, const OrderRow& order) {
  const std::uint32_t row = FindRow(current.families, order.family);
  if (row == kNoRow) {
    return OrderRefusal::kNoSuchSubject;
  }
  if (current.families.rows[row].asked_to_leave == 0) {
    return OrderRefusal::kNotEligible;
  }
  if (order.enable != 0) {
    SendAway(current, order.family);
  } else {
    current.families.rows[row].asked_to_leave = 0;
    Lodge(current, row);
  }
  return OrderRefusal::kNone;
}

}  // namespace

bool TentWeather(const LifeConfig& config, Month month) {
  const auto index = static_cast<std::uint8_t>(month);
  return index >= config.tent_from_month && index <= config.tent_to_month;
}

void RunRoofless(const LifeConfig& config, WorldState& current) {
  const bool cold = !TentWeather(config, current.calendar.date.month);
  const auto day = static_cast<std::uint32_t>(current.calendar.day);
  // THE LODGED FIRST (boss seq 197): a free house takes them out of a
  // stranger's house before it goes to anybody else. A lodging whose house
  // is gone is no lodging: the family is back on the ladder below.
  for (std::uint32_t row = 0; row < current.families.rows.size(); ++row) {
    FamilyRow& family = current.families.rows[row];
    if (family.lodged_in.value == kInvalidEntityIdValue || HasHouse(current, family)) {
      continue;
    }
    if (FindRow(current.units, family.lodged_in) == kNoRow) {
      family.lodged_in = UnitId{};
      continue;
    }
    const UnitId house = FreeHouse(config, current, cold);
    if (house.value != kInvalidEntityIdValue) {
      MoveIn(current, row, house);
    }
  }
  for (std::uint32_t row = 0; row < current.families.rows.size(); ++row) {
    FamilyRow& family = current.families.rows[row];
    if (HasHouse(current, family) || family.lodged_in.value != kInvalidEntityIdValue) {
      continue;
    }
    const FamilyId id = current.families.row_ids[row];
    const UnitId house = FreeHouse(config, current, cold);
    if (house.value != kInvalidEntityIdValue) {
      MoveIn(current, row, house);
      continue;
    }
    family.house = UnitId{};
    if (!cold) {
      if (family.in_tent == 0) {
        family.in_tent = 1;
        SimEvent& tent = EmitEvent(current, EventKind::kFamilyInTent, EventSeverity::kNotable);
        tent.family = id;
      }
      continue;
    }
    // THE COLD AND NOWHERE TO GO: the certificate is asked for, and silence
    // refuses it (§20 step 4). Until 2026-09-19 the family left by itself.
    family.in_tent = 0;
    if (family.asked_to_leave == 0) {
      family.asked_to_leave = 1;
      family.asked_day = day;
      SimEvent& asks = EmitEvent(current, EventKind::kLeaveRequested, EventSeverity::kInterrupting);
      asks.family = id;
      asks.amount = 0;  // the reason: no house
      continue;
    }
    if (static_cast<float>(day - family.asked_day) >= config.leave_request_answer_days) {
      Lodge(current, row);
    }
  }
  // After the morning's moves: who is lodged or hosts today pays today.
  ChargeLodging(config, current);
}

void ConsumeHousingOrders(const LifeConfig& config, WorldState& current) {
  for (std::uint32_t row = 0; row < current.orders.rows.size(); ++row) {
    // By index and re-read: a family sent away removes rows elsewhere, never
    // an order, but the reference would not survive a reallocation.
    const OrderRow order = current.orders.rows[row];
    if (order.status != OrderStatus::kPending) {
      continue;
    }
    OrderRefusal refusal = OrderRefusal::kNone;
    if (order.kind == OrderKind::kReserveHouse) {
      refusal = ReserveHouse(config, current, order);
    } else if (order.kind == OrderKind::kAnswerLeaveRequest) {
      refusal = AnswerLeaveRequest(current, order);
    } else {
      continue;
    }
    current.orders.rows[row].status =
        refusal == OrderRefusal::kNone ? OrderStatus::kDone : OrderStatus::kRefused;
    current.orders.rows[row].refusal = refusal;
  }
}

}  // namespace core
