// The funeral step (core_residents/funeral_rules.h; #1084).

#include "funeral_rules.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/emit_event.h"
#include "core_common/event_state.h"
#include "core_common/order_state.h"
#include "core_common/quantities.h"
#include "core_common/road_route.h"
#include "core_common/state_table_ops.h"
#include "core_common/work_seam.h"

namespace core {

namespace {

bool Contains(std::span<const ResidentId> list, ResidentId id) {
  return std::ranges::find(list, id) != list.end();
}

void AddOnce(std::vector<ResidentId>& list, ResidentId id) {
  if (id.value != kInvalidEntityIdValue && !Contains(list, id)) {
    list.push_back(id);
  }
}

float AgeOf(const FuneralConfig& config, const ResidentRow& person, SimDay day) {
  return BiologicalAgeYears(config.life_speedup, person.birth_day, day);
}

/// A cemetery that takes a grave: built and of the cemetery type (burial.cpp's
/// own test; its room is not asked since #1367).
bool TakesAGrave(const FuneralConfig& config, const UnitRow& unit) {
  return config.cemetery_type.value != kInvalidDefIdValue &&
         unit.type.value == config.cemetery_type.value && unit.level > 0;
}

/// The dead's house: its position and its settlement. false with no house.
bool HouseOf(const WorldState& world, FamilyId family, Vec2& at, SettlementId& settlement) {
  settlement = SettlementId{};
  if (!HomePositionOf(world, family, at)) {
    return false;
  }
  const std::uint32_t row = FindRow(world.families, family);
  if (row != kNoRow) {
    const std::uint32_t house = FindRow(world.units, world.families.rows[row].house);
    if (house != kNoRow) {
      settlement = world.units.rows[house].settlement;
    }
  }
  return true;
}

/// One hop of kin from `person`: mother, father, spouse, and the children who
/// name him as a parent.
void OneHop(const WorldState& world, ResidentId person, std::vector<ResidentId>& out) {
  const std::uint32_t row = FindRow(world.residents, person);
  if (row != kNoRow) {
    const ResidentRow& self = world.residents.rows[row];
    AddOnce(out, self.mother);
    AddOnce(out, self.father);
    AddOnce(out, self.spouse);
  }
  for (std::uint32_t other = 0; other < world.residents.rows.size(); ++other) {
    const ResidentRow& child = world.residents.rows[other];
    if (child.mother == person || child.father == person) {
      AddOnce(out, world.residents.row_ids[other]);
    }
  }
}

/// Whether `person` is the last adult of his household, the same day's dead
/// not counted (the loud rule's «последний взрослый двора»).
bool LastAdultOfHousehold(const FuneralConfig& config,
                          const WorldState& world,
                          ResidentId person,
                          std::span<const ResidentId> died) {
  const std::uint32_t row = FindRow(world.residents, person);
  if (row == kNoRow) {
    return false;
  }
  const FamilyId family = world.residents.rows[row].family;
  const SimDay day = world.calendar.day;
  for (std::uint32_t other = 0; other < world.residents.rows.size(); ++other) {
    const ResidentRow& member = world.residents.rows[other];
    if (member.family == family && !Contains(died, world.residents.row_ids[other]) &&
        AgeOf(config, member, day) >= config.adult_age_years) {
      return false;
    }
  }
  return true;
}

bool LoudDeath(const FuneralConfig& config,
               const WorldState& world,
               ResidentId person,
               std::span<const ResidentId> died) {
  if (person == world.named.elder) {
    return true;  // a named character
  }
  const std::uint32_t row = FindRow(world.residents, person);
  if (row != kNoRow &&
      AgeOf(config, world.residents.rows[row], world.calendar.day) < config.adult_age_years) {
    return true;  // a child
  }
  return LastAdultOfHousehold(config, world, person, died);
}

/// The chairman was away in the district at some hour of `day`.
bool ChairmanAwayOn(const WorldState& world, SimDay day) {
  const Tick from = static_cast<Tick>(day) * kTicksPerDay;
  const Tick to = from + kTicksPerDay;
  const ChairmanState& chairman = world.chairman;
  return chairman.away_until_tick > chairman.away_from_tick && chairman.away_from_tick < to &&
         chairman.away_until_tick > from;
}

/// The party's kin get `mood`; the dead's households get `satisfaction` for a
/// month from today.
void ApplyChairmanEffect(const FuneralConfig& config,
                         WorldState& current,
                         const FuneralRow& funeral,
                         float mood,
                         float satisfaction) {
  const SimDay day = current.calendar.day;
  for (std::uint32_t place = 0; place < funeral.kin_count && place < funeral.party.size();
       ++place) {
    const std::uint32_t row = FindRow(current.residents, funeral.party[place]);
    if (row != kNoRow) {
      AddFuneralMood(config, current.residents.rows[row], mood, day);
    }
  }
  std::vector<FamilyId> seen;
  for (const FamilyId family : funeral.households) {
    if (std::ranges::find(seen, family) != seen.end()) {
      continue;
    }
    seen.push_back(family);
    const std::uint32_t row = FindRow(current.families, family);
    if (row != kNoRow) {
      FamilyRow& household = current.families.rows[row];
      household.funeral_satisfaction = satisfaction;
      household.funeral_satisfaction_until = day + kDaysPerMonth;
    }
  }
}

}  // namespace

std::vector<ResidentId> FuneralParty(const FuneralConfig& config,
                                     const WorldState& world,
                                     ResidentId deceased,
                                     std::span<const ResidentId> also_dead,
                                     std::uint32_t& kin_count) {
  const SimDay day = world.calendar.day;
  std::vector<ResidentId> hop_one;
  OneHop(world, deceased, hop_one);
  std::vector<ResidentId> kin = hop_one;
  for (const ResidentId near : hop_one) {
    OneHop(world, near, kin);
  }
  std::vector<ResidentId> party;
  const auto walks = [&](ResidentId id, float from_years) {
    if (id == deceased || Contains(also_dead, id)) {
      return false;
    }
    const std::uint32_t row = FindRow(world.residents, id);
    return row != kNoRow && AgeOf(config, world.residents.rows[row], day) >= from_years;
  };
  // THE KIN, adults only, in the order the hops found them.
  for (const ResidentId id : kin) {
    if (walks(id, config.adult_age_years)) {
      AddOnce(party, id);
    }
  }
  // THE HOUSEHOLD, but its small children.
  const std::uint32_t dead_row = FindRow(world.residents, deceased);
  const FamilyId family = dead_row != kNoRow ? world.residents.rows[dead_row].family : FamilyId{};
  for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
    const ResidentId id = world.residents.row_ids[row];
    if (world.residents.rows[row].family == family && walks(id, config.household_from_years)) {
      AddOnce(party, id);
    }
  }
  kin_count = static_cast<std::uint32_t>(party.size());
  // THE NEIGHBOURS: one adult of every other household within
  // kNeighbourMetres of his house, in his settlement, the oldest id first.
  Vec2 home;
  SettlementId settlement;
  if (!HouseOf(world, family, home, settlement)) {
    return party;
  }
  for (std::uint32_t other = 0; other < world.families.rows.size(); ++other) {
    const FamilyId neighbour = world.families.row_ids[other];
    if (neighbour == family) {
      continue;
    }
    Vec2 at;
    SettlementId theirs;
    if (!HouseOf(world, neighbour, at, theirs) || theirs != settlement ||
        std::hypot(at.x - home.x, at.y - home.y) > kNeighbourMetres) {
      continue;
    }
    ResidentId pick;
    for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
      const ResidentId id = world.residents.row_ids[row];
      if (world.residents.rows[row].family == neighbour && walks(id, config.adult_age_years) &&
          !Contains(party, id) && (pick.value == kInvalidEntityIdValue || id.value < pick.value)) {
        pick = id;
      }
    }
    AddOnce(party, pick);
  }
  return party;
}

UnitId CemeteryFor(const FuneralConfig& config,
                   const WorldState& world,
                   std::span<const ResidentId> deceased) {
  std::vector<Vec2> homes;
  SettlementId settlement;
  for (const ResidentId id : deceased) {
    const std::uint32_t row = FindRow(world.residents, id);
    Vec2 at;
    SettlementId theirs;
    if (row != kNoRow && HouseOf(world, world.residents.rows[row].family, at, theirs)) {
      homes.push_back(at);
      settlement = theirs;
    }
  }
  std::uint32_t best = kNoRow;
  float best_km = 0.0F;
  bool best_own = false;
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    const UnitRow& unit = world.units.rows[row];
    if (!TakesAGrave(config, unit)) {
      continue;
    }
    const bool own =
        settlement.value != kInvalidEntityIdValue && unit.settlement.value == settlement.value;
    float km = 0.0F;
    for (const Vec2 home : homes) {
      km += RoadKm(world, TravelMode::kWalk, home, unit.position);
    }
    const bool better = best == kNoRow || (own && !best_own) ||
                        (own == best_own &&
                         (km < best_km || (km == best_km && world.units.row_ids[row].value <
                                                                world.units.row_ids[best].value)));
    if (better) {
      best = row;
      best_km = km;
      best_own = own;
    }
  }
  return best == kNoRow ? UnitId{} : world.units.row_ids[best];
}

std::vector<UnitId> OpenFunerals(const FuneralConfig& config,
                                 WorldState& current,
                                 std::span<const ResidentId> died) {
  std::vector<UnitId> laid_in(died.size());
  const SimDay day = current.calendar.day;
  const SimDay burial_day = day + 1U;
  // ONE FUNERAL A SETTLEMENT (social.md «Какое кладбище»): the dead grouped by
  // their house's settlement, in the order they died; a dead with no house
  // is a funeral of his own.
  std::vector<bool> taken(died.size(), false);
  for (std::size_t first = 0; first < died.size(); ++first) {
    if (taken[first]) {
      continue;
    }
    const auto settlement_of = [&](ResidentId id, bool& housed) {
      housed = false;
      const std::uint32_t row = FindRow(current.residents, id);
      Vec2 at;
      SettlementId settlement;
      if (row != kNoRow && HouseOf(current, current.residents.rows[row].family, at, settlement)) {
        housed = true;
      }
      return settlement;
    };
    bool housed = false;
    const SettlementId settlement = settlement_of(died[first], housed);
    std::vector<std::size_t> members{first};
    taken[first] = true;
    for (std::size_t next = first + 1; housed && next < died.size(); ++next) {
      bool next_housed = false;
      if (!taken[next] && settlement_of(died[next], next_housed) == settlement && next_housed) {
        members.push_back(next);
        taken[next] = true;
      }
    }
    FuneralRow funeral;
    funeral.settlement = settlement;
    funeral.burial_day = burial_day;
    bool loud = false;
    for (const std::size_t index : members) {
      const ResidentId id = died[index];
      funeral.deceased.push_back(id);
      const std::uint32_t row = FindRow(current.residents, id);
      funeral.households.push_back(row != kNoRow ? current.residents.rows[row].family : FamilyId{});
      loud = loud || LoudDeath(config, current, id, died);
    }
    funeral.cemetery = CemeteryFor(config, current, funeral.deceased);
    // THE PARTY: each dead's kin and household first, then the neighbours.
    std::vector<ResidentId> kin;
    std::vector<ResidentId> neighbours;
    for (const ResidentId id : funeral.deceased) {
      std::uint32_t kin_count = 0;
      const std::vector<ResidentId> party = FuneralParty(config, current, id, died, kin_count);
      for (std::uint32_t place = 0; place < party.size(); ++place) {
        AddOnce(place < kin_count ? kin : neighbours, party[place]);
      }
    }
    funeral.party = kin;
    for (const ResidentId id : neighbours) {
      AddOnce(funeral.party, id);
    }
    funeral.kin_count = static_cast<std::uint32_t>(kin.size());
    // THE ROAD, one way on foot, from the first dead's house.
    const std::uint32_t cemetery_row = FindRow(current.units, funeral.cemetery);
    Vec2 home;
    SettlementId unused;
    if (cemetery_row != kNoRow && HouseOf(current, funeral.households.front(), home, unused)) {
      funeral.road_km =
          RoadKm(current, TravelMode::kWalk, home, current.units.rows[cemetery_row].position);
    }
    // LOUD, within the cap: one loud burial DAY in kLoudEveryDays - a second
    // settlement's loud funeral on the same burial day stays loud.
    if (loud && (current.last_loud_burial_day == kNoLoudBurialDay ||
                 current.last_loud_burial_day == burial_day ||
                 burial_day >= current.last_loud_burial_day + kLoudEveryDays)) {
      funeral.loud = 1;
      current.last_loud_burial_day = burial_day;
    }
    // THE GRIEF of the kin, from today.
    for (const ResidentId id : kin) {
      const std::uint32_t row = FindRow(current.residents, id);
      if (row != kNoRow) {
        AddFuneralMood(config, current.residents.rows[row], config.grief_mood, day);
      }
    }
    for (const std::size_t index : members) {
      laid_in[index] = funeral.cemetery;
    }
    const FuneralId id = AppendRow(current.funerals, funeral);
    SimEvent& invitation =
        EmitEvent(current,
                  EventKind::kFuneralInvitation,
                  funeral.loud != 0 ? EventSeverity::kInterrupting : EventSeverity::kNotable);
    invitation.amount = static_cast<std::int64_t>(id.value);
    invitation.resident = current.funerals.rows.back().deceased.front();
    invitation.family = current.funerals.rows.back().households.front();
  }
  return laid_in;
}

void TakeAttendOrders(WorldState& current) {
  const SimDay day = current.calendar.day;
  for (OrderRow& order : current.orders.rows) {
    if (order.status != OrderStatus::kPending || order.kind != OrderKind::kAttendFuneral) {
      continue;
    }
    OrderRefusal refusal = OrderRefusal::kNone;
    // The order names its funeral by `amount` (the invitation's own field).
    const std::uint32_t row =
        order.amount > 0 && order.amount <= static_cast<Grams>(UINT32_MAX)
            ? FindRow(current.funerals, FuneralId{static_cast<std::uint32_t>(order.amount)})
            : kNoRow;
    if (row == kNoRow || current.funerals.rows[row].burial_day < day) {
      refusal = OrderRefusal::kNoSuchSubject;
    } else if (current.funerals.rows[row].chairman != FuneralChairman::kInvited) {
      refusal = OrderRefusal::kRuleForbids;
    } else {
      current.funerals.rows[row].chairman = FuneralChairman::kWent;
    }
    order.status = refusal == OrderRefusal::kNone ? OrderStatus::kDone : OrderStatus::kRefused;
    order.refusal = refusal;
  }
}

float FuneralMoodTerm(const FuneralConfig& config, const ResidentRow& person, SimDay day) {
  if (person.funeral_mood_amount == 0.0F || day < person.funeral_mood_day ||
      config.mood_fade_days == 0) {
    return 0.0F;
  }
  const float gone =
      static_cast<float>(day - person.funeral_mood_day) / static_cast<float>(config.mood_fade_days);
  return gone >= 1.0F ? 0.0F : person.funeral_mood_amount * (1.0F - gone);
}

void AddFuneralMood(const FuneralConfig& config, ResidentRow& person, float amount, SimDay day) {
  person.funeral_mood_amount = FuneralMoodTerm(config, person, day) + amount;
  person.funeral_mood_day = day;
}

void RunFuneralDay(const FuneralConfig& config, WorldState& current) {
  const SimDay day = current.calendar.day;
  // The burial days he went to a funeral on, read BEFORE any row goes: a
  // closed row takes its «went» with it.
  std::vector<SimDay> went_on;
  for (const FuneralRow& funeral : current.funerals.rows) {
    if (funeral.chairman == FuneralChairman::kWent) {
      went_on.push_back(funeral.burial_day);
    }
  }
  // THE FUNERALS BURIED YESTERDAY CLOSE (or earlier, for a world loaded late).
  for (std::uint32_t row = 0; row < current.funerals.rows.size();) {
    FuneralRow& funeral = current.funerals.rows[row];
    if (funeral.burial_day >= day) {
      ++row;
      continue;
    }
    if (funeral.chairman == FuneralChairman::kWent) {
      ApplyChairmanEffect(
          config, current, funeral, config.presence_mood, config.presence_satisfaction);
    } else if (funeral.loud != 0) {
      const SimDay burial_day = funeral.burial_day;
      const bool at_another = std::ranges::find(went_on, burial_day) != went_on.end();
      if (ChairmanAwayOn(current, burial_day) || at_another) {
        funeral.chairman = FuneralChairman::kExcused;
      } else {
        funeral.chairman = FuneralChairman::kMissed;
        ApplyChairmanEffect(
            config, current, funeral, config.absence_mood, config.absence_satisfaction);
      }
    }
    RemoveRow(current.funerals, current.funerals.row_ids[row]);
  }
  // THE MOOD TERM INTO THE MOOD (resident_state.h): on a month's first day
  // the sugar issue has just composed the mood afresh from mood_base, so the
  // whole term goes back in; any other day only its change since yesterday.
  const bool composed = day % kDaysPerMonth == 0;
  for (ResidentRow& person : current.residents.rows) {
    const float term = FuneralMoodTerm(config, person, day);
    const float delta = composed ? term : term - person.funeral_mood_applied;
    if (delta != 0.0F) {
      person.mood = std::clamp(person.mood + delta, kMetricMin, kMetricMax);
    }
    person.funeral_mood_applied = term;
  }
}

}  // namespace core
