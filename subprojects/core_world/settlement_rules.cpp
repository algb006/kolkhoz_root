// The settlement step (settlement_rules.h; #1089): join daily, form, merge,
// split and move the gong at the month's turn.

#include "settlement_rules.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "core_catalog/definitions.h"
#include "core_common/calendar.h"
#include "core_common/emit_event.h"
#include "core_common/event_state.h"
#include "core_common/plot.h"
#include "core_common/settlement_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_tables/stub_tables.h"

namespace core {

namespace {

/// An unassigned dwelling seen in a forming chain at the last month's turn
/// (UnitRow::outlying's third value; the two-turns rule).
constexpr std::uint8_t kFormPending = 2;

float Distance(const Vec2& a, const Vec2& b) {
  const float dx = a.x - b.x;
  const float dy = a.y - b.y;
  return std::sqrt((dx * dx) + (dy * dy));
}

bool MonthsTurn(const CalendarState& calendar) {
  return HourFromTick(calendar.tick) + 1U == kTicksPerDay &&
         calendar.day % kDaysPerMonth == kDaysPerMonth - 1U;
}

/// The dwellings' rows, in row order.
std::vector<std::uint32_t> DwellingRows(const SettlementConfig& config, const WorldState& world) {
  const std::vector<std::uint8_t> inhabited = InhabitedUnits(world);
  std::vector<std::uint32_t> rows;
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    if (IsDwelling(config, world.units.rows[row], row, inhabited)) {
      rows.push_back(row);
    }
  }
  return rows;
}

/// The chains of `members` (unit rows) linked within `metres`: each chain's
/// rows, the chains in the order of their first member.
std::vector<std::vector<std::uint32_t>> Chains(const WorldState& world,
                                               const std::vector<std::uint32_t>& members,
                                               float metres) {
  std::vector<int> chain_of(members.size(), -1);
  std::vector<std::vector<std::uint32_t>> chains;
  for (std::size_t seed = 0; seed < members.size(); ++seed) {
    if (chain_of[seed] >= 0) {
      continue;
    }
    const int chain = static_cast<int>(chains.size());
    chains.emplace_back();
    std::vector<std::size_t> frontier{seed};
    chain_of[seed] = chain;
    while (!frontier.empty()) {
      const std::size_t at = frontier.back();
      frontier.pop_back();
      chains[static_cast<std::size_t>(chain)].push_back(members[at]);
      for (std::size_t other = 0; other < members.size(); ++other) {
        if (chain_of[other] < 0 && Distance(world.units.rows[members[at]].position,
                                            world.units.rows[members[other]].position) <= metres) {
          chain_of[other] = chain;
          frontier.push_back(other);
        }
      }
    }
  }
  return chains;
}

/// The nearest distance between a row of `left` and one of `right`.
float Gap(const WorldState& world,
          const std::vector<std::uint32_t>& left,
          const std::vector<std::uint32_t>& right) {
  float gap = std::numeric_limits<float>::infinity();
  for (const std::uint32_t a : left) {
    for (const std::uint32_t b : right) {
      gap = std::min(gap, Distance(world.units.rows[a].position, world.units.rows[b].position));
    }
  }
  return gap;
}

/// The member dwellings of `id`, in row order.
std::vector<std::uint32_t> MembersOf(const WorldState& world,
                                     const std::vector<std::uint32_t>& dwellings,
                                     SettlementId id) {
  std::vector<std::uint32_t> members;
  for (const std::uint32_t row : dwellings) {
    if (world.units.rows[row].settlement == id) {
      members.push_back(row);
    }
  }
  return members;
}

Vec2 CentreOf(const WorldState& world, const std::vector<std::uint32_t>& rows) {
  Vec2 sum{.x = 0.0F, .y = 0.0F};
  for (const std::uint32_t row : rows) {
    sum.x += world.units.rows[row].position.x;
    sum.y += world.units.rows[row].position.y;
  }
  const auto n = static_cast<float>(std::max<std::size_t>(rows.size(), 1));
  return Vec2{.x = sum.x / n, .y = sum.y / n};
}

PlotRules RulesOf(const SettlementConfig& config) {
  return PlotRules{.radius_by_type = config.radius_by_type, .map_side_m = config.map_side_m};
}

float GongRadius(const SettlementConfig& config) {
  return config.fire_gong.value < config.radius_by_type.size()
             ? config.radius_by_type[config.fire_gong.value]
             : 0.0F;
}

/// A new gong at the free plot nearest `centre`; invalid when the world has
/// no gong type.
UnitId PlaceGong(const SettlementConfig& config, WorldState& current, const Vec2& centre) {
  if (config.fire_gong.value == kInvalidDefIdValue) {
    return UnitId{};
  }
  UnitRow gong;
  gong.type = config.fire_gong;
  gong.level = 1;
  gong.position = FreePlot(current.units, RulesOf(config), centre, GongRadius(config));
  return AppendRow(current.units, gong);
}

/// A new settlement of `members`, its gong, its event.
SettlementId FormSettlement(const SettlementConfig& config,
                            WorldState& current,
                            const std::vector<std::uint32_t>& members) {
  SettlementRow row;
  row.centre = CentreOf(current, members);
  row.formed_day = current.calendar.day;
  const SettlementId id = AppendRow(current.settlements, row);
  for (const std::uint32_t member : members) {
    current.units.rows[member].settlement = id;
    current.units.rows[member].outlying = 0;
  }
  // The gong after the members, so its AppendRow does not move their rows
  // under the loop (AppendRow appends: the rows above stay).
  const UnitId gong = PlaceGong(config, current, row.centre);
  const std::uint32_t settlement_row = FindRow(current.settlements, id);
  current.settlements.rows[settlement_row].gong = gong;
  if (gong.value != kInvalidEntityIdValue) {
    current.units.rows[FindRow(current.units, gong)].settlement = id;
  }
  SimEvent& formed = EmitEvent(current, EventKind::kSettlementFormed, EventSeverity::kNotable);
  formed.amount = static_cast<std::int64_t>(id.value);
  formed.unit = gong;
  return id;
}

std::int64_t Pack(SettlementId low, SettlementId high) {
  return static_cast<std::int64_t>(low.value) | (static_cast<std::int64_t>(high.value) << 32);
}

/// Whether `a` is the elder of the two: the start village always, then the
/// earlier formed day, then the smaller id.
bool Elder(const WorldState& world, SettlementId a, SettlementId b) {
  if (a.value == kStartSettlement || b.value == kStartSettlement) {
    return a.value == kStartSettlement;
  }
  const SettlementRow& left = world.settlements.rows[FindRow(world.settlements, a)];
  const SettlementRow& right = world.settlements.rows[FindRow(world.settlements, b)];
  return left.formed_day != right.formed_day ? left.formed_day < right.formed_day
                                             : a.value < b.value;
}

void RemoveGong(WorldState& current, UnitId gong) {
  if (gong.value != kInvalidEntityIdValue && FindRow(current.units, gong) != kNoRow) {
    RemoveRow(current.units, gong);
  }
}

/// The month's turn: merge, split, form, the centres and the gongs.
void TurnTheMonth(const SettlementConfig& config, WorldState& current) {
  // MERGE: two settlements with dwellings within the join distance at two
  // turns in a row (merge_with names the partner seen at the last).
  {
    std::vector<std::uint32_t> dwellings = DwellingRows(config, current);
    const std::vector<SettlementId> ids(current.settlements.row_ids.begin(),
                                        current.settlements.row_ids.end());
    std::vector<SettlementId> seen(ids.size());
    for (std::size_t i = 0; i < ids.size(); ++i) {
      for (std::size_t j = i + 1; j < ids.size(); ++j) {
        if (Gap(current,
                MembersOf(current, dwellings, ids[i]),
                MembersOf(current, dwellings, ids[j])) <= kJoinMetres) {
          seen[i] = seen[i].value == kInvalidEntityIdValue ? ids[j] : seen[i];
          seen[j] = seen[j].value == kInvalidEntityIdValue ? ids[i] : seen[j];
        }
      }
    }
    for (std::size_t i = 0; i < ids.size(); ++i) {
      const std::uint32_t row = FindRow(current.settlements, ids[i]);
      if (row == kNoRow) {
        continue;  // absorbed above
      }
      SettlementRow& settlement = current.settlements.rows[row];
      const SettlementId partner = seen[i];
      if (partner.value == kInvalidEntityIdValue ||
          FindRow(current.settlements, partner) == kNoRow) {
        settlement.merge_with = SettlementId{};
        continue;
      }
      if (settlement.merge_with != partner) {
        settlement.merge_with = partner;
        continue;
      }
      const SettlementId survivor = Elder(current, ids[i], partner) ? ids[i] : partner;
      const SettlementId absorbed = survivor == ids[i] ? partner : ids[i];
      const UnitId absorbed_gong =
          current.settlements.rows[FindRow(current.settlements, absorbed)].gong;
      for (UnitRow& unit : current.units.rows) {
        unit.settlement = unit.settlement == absorbed ? survivor : unit.settlement;
      }
      RemoveRow(current.settlements, absorbed);
      RemoveGong(current, absorbed_gong);
      current.settlements.rows[FindRow(current.settlements, survivor)].merge_with = SettlementId{};
      SimEvent& merged = EmitEvent(current, EventKind::kSettlementMerged, EventSeverity::kNotable);
      merged.amount = Pack(survivor, absorbed);
      dwellings = DwellingRows(config, current);
    }
  }
  // SPLIT: a chain beyond the form distance of the rest at two turns in a
  // row, both sides with kFormDwellings; the side with the oldest dwelling
  // (the smallest unit id) keeps the id.
  {
    const std::vector<std::uint32_t> dwellings = DwellingRows(config, current);
    const std::vector<SettlementId> ids(current.settlements.row_ids.begin(),
                                        current.settlements.row_ids.end());
    for (const SettlementId id : ids) {
      const std::vector<std::uint32_t> members = MembersOf(current, dwellings, id);
      const std::vector<std::vector<std::uint32_t>> chains = Chains(current, members, kFormMetres);
      std::uint32_t settlement_row = FindRow(current.settlements, id);
      if (chains.size() < 2) {
        current.settlements.rows[settlement_row].split_pending = 0;
        continue;
      }
      // The side that keeps the id: the chain with the oldest dwelling.
      std::size_t keeper = 0;
      std::uint32_t oldest = std::numeric_limits<std::uint32_t>::max();
      for (std::size_t chain = 0; chain < chains.size(); ++chain) {
        for (const std::uint32_t row : chains[chain]) {
          if (current.units.row_ids[row].value < oldest) {
            oldest = current.units.row_ids[row].value;
            keeper = chain;
          }
        }
      }
      const bool keeper_big = chains[keeper].size() >= kFormDwellings;
      std::vector<std::size_t> splitting;
      for (std::size_t chain = 0; chain < chains.size(); ++chain) {
        if (chain != keeper && chains[chain].size() >= kFormDwellings) {
          splitting.push_back(chain);
        }
      }
      if (!keeper_big || splitting.empty()) {
        current.settlements.rows[settlement_row].split_pending = 0;
        continue;
      }
      if (current.settlements.rows[settlement_row].split_pending == 0) {
        current.settlements.rows[settlement_row].split_pending = 1;
        continue;
      }
      current.settlements.rows[settlement_row].split_pending = 0;
      for (const std::size_t chain : splitting) {
        const SettlementId fresh = FormSettlement(config, current, chains[chain]);
        settlement_row = FindRow(current.settlements, id);
        SimEvent& split = EmitEvent(current, EventKind::kSettlementSplit, EventSeverity::kNotable);
        split.amount = Pack(id, fresh);
      }
    }
  }
  // FORM: unassigned dwellings chained within the join distance, kFormDwellings
  // or more, every one beyond the form distance of every settlement - at two
  // turns in a row (marked kFormPending at the first).
  {
    const std::vector<std::uint32_t> dwellings = DwellingRows(config, current);
    std::vector<std::uint32_t> assigned;
    std::vector<std::uint32_t> free;
    for (const std::uint32_t row : dwellings) {
      (current.units.rows[row].settlement.value == kInvalidEntityIdValue ? free : assigned)
          .push_back(row);
    }
    for (const std::vector<std::uint32_t>& chain : Chains(current, free, kJoinMetres)) {
      const bool apart = Gap(current, chain, assigned) > kFormMetres;
      if (chain.size() < kFormDwellings || !apart) {
        for (const std::uint32_t row : chain) {
          current.units.rows[row].outlying = 0;
        }
        continue;
      }
      const bool pending = std::ranges::all_of(chain, [&current](std::uint32_t row) {
        return current.units.rows[row].outlying == kFormPending;
      });
      if (!pending) {
        for (const std::uint32_t row : chain) {
          current.units.rows[row].outlying = kFormPending;
        }
        continue;
      }
      FormSettlement(config, current, chain);
    }
  }
  // THE CENTRES AND THE GONGS: a centre is its dwellings' mean; the gong
  // moves to the free plot nearest it when it shifted beyond kGongMoveMetres
  // (the month's turn is the at-most-monthly rule), unless frozen.
  const std::vector<std::uint32_t> dwellings = DwellingRows(config, current);
  for (std::uint32_t row = 0; row < current.settlements.rows.size(); ++row) {
    const SettlementId id = current.settlements.row_ids[row];
    const std::vector<std::uint32_t> members = MembersOf(current, dwellings, id);
    if (members.empty()) {
      continue;  // a village whose last dwelling went keeps its row until the empty sweep
    }
    SettlementRow& settlement = current.settlements.rows[row];
    settlement.centre = CentreOf(current, members);
    const std::uint32_t gong_row = FindRow(current.units, settlement.gong);
    if (gong_row == kNoRow) {
      settlement.gong = PlaceGong(config, current, settlement.centre);
      continue;
    }
    UnitRow& gong = current.units.rows[gong_row];
    if (config.gong_frozen || Distance(gong.position, settlement.centre) <= kGongMoveMetres) {
      continue;
    }
    gong.position = settlement.centre;  // vacate first, so FreePlot does not avoid itself
    gong.position = FreePlot(current.units, RulesOf(config), settlement.centre, GongRadius(config));
    settlement.gong_moved_day = current.calendar.day;
    SimEvent& moved = EmitEvent(current, EventKind::kFireGongMoved, EventSeverity::kRoutine);
    moved.unit = settlement.gong;
    moved.amount = static_cast<std::int64_t>(id.value);
  }
  // THE EMPTY SWEEP: a settlement with no dwelling left goes, with its gong;
  // never the start village (its id stands for ever, V5).
  std::vector<SettlementId> empty;
  for (std::uint32_t row = 0; row < current.settlements.rows.size(); ++row) {
    const SettlementId id = current.settlements.row_ids[row];
    if (id.value != kStartSettlement && MembersOf(current, dwellings, id).empty()) {
      empty.push_back(id);
    }
  }
  for (const SettlementId id : empty) {
    RemoveGong(current, current.settlements.rows[FindRow(current.settlements, id)].gong);
    RemoveRow(current.settlements, id);
    for (UnitRow& unit : current.units.rows) {
      unit.settlement = unit.settlement == id ? SettlementId{} : unit.settlement;
    }
  }
}

}  // namespace

std::vector<std::uint8_t> InhabitedUnits(const WorldState& world) {
  std::vector<std::uint8_t> inhabited(world.units.rows.size(), 0);
  for (const FamilyRow& family : world.families.rows) {
    const std::uint32_t row = FindRow(world.units, family.house);
    if (row != kNoRow) {
      inhabited[row] = 1;
    }
  }
  return inhabited;
}

bool IsDwelling(const SettlementConfig& config,
                const UnitRow& unit,
                std::uint32_t unit_row,
                const std::vector<std::uint8_t>& inhabited) {
  return unit.type.value < config.housing.size() && config.housing[unit.type.value] != 0 &&
         unit.level >= 1 && unit.dead == 0 && unit_row < inhabited.size() &&
         inhabited[unit_row] != 0;
}

bool ReadSettlementConfig(const ITableSet& tables, SettlementConfig& config, std::string& error) {
  const ITable* const types = tables.FindTable("unit_types");
  if (types == nullptr) {
    return true;  // a world with no unit types has no dwelling: no settlement but the seed
  }
  // A TABLE SET WITH NO `class` COLUMN (a test fixture's own unit_types) has
  // no dwelling: the start village stays the one settlement, nothing forms.
  // Not a refusal - the shipped table has the column, and a fixture that
  // never asked about villages must not stop building its world.
  const std::uint32_t klass = types->FindColumn("class");
  config.housing.assign(types->RowCount(), 0);
  for (std::uint32_t row = 0; klass != kNoTableColumn && row < types->RowCount(); ++row) {
    config.housing[row] = types->CellText(row, klass) == "housing" ? 1 : 0;
  }
  (void)error;
  const std::uint32_t gong = types->FindRowByKey("fire_gong");
  config.fire_gong =
      gong == kNoTableRow ? UnitTypeId{} : UnitTypeId{static_cast<std::uint16_t>(gong)};
  Definitions definitions;
  std::string definitions_error;
  if (LoadDefinitions(tables, StubTables::kAllowed, definitions, definitions_error)) {
    config.radius_by_type = definitions.units.keep_out_radius_m;
    config.map_side_m = definitions.map_side_m;
  }
  return true;
}

void SeedStartSettlement(const SettlementConfig& config, WorldState& world) {
  if (!world.settlements.rows.empty()) {
    return;
  }
  SettlementRow start;
  start.formed_day = 0;
  // The start layout's own gong: the first unit of the type.
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    if (config.fire_gong.value != kInvalidDefIdValue &&
        world.units.rows[row].type.value == config.fire_gong.value) {
      start.gong = world.units.row_ids[row];
      break;
    }
  }
  const std::vector<std::uint32_t> dwellings = DwellingRows(config, world);
  start.centre = CentreOf(world, dwellings);
  const SettlementId id = AppendRow(world.settlements, start);
  for (UnitRow& unit : world.units.rows) {
    unit.settlement = id;
    unit.outlying = 0;
  }
}

void RunSettlementDay(const SettlementConfig& config, WorldState& current) {
  if (current.settlements.rows.empty()) {
    return;  // a world never seeded (a test fixture) stays without settlements
  }
  // JOIN, DAILY: a dwelling with no settlement joins the one with a dwelling
  // within the join distance; else, within the form distance of one, the
  // nearest as an outlying yard; else it stays a candidate. A member's
  // outlying flag follows its neighbours.
  const std::vector<std::uint32_t> dwellings = DwellingRows(config, current);
  for (const std::uint32_t row : dwellings) {
    UnitRow& unit = current.units.rows[row];
    float nearest = std::numeric_limits<float>::infinity();
    SettlementId nearest_id;
    float own_nearest = std::numeric_limits<float>::infinity();
    for (const std::uint32_t other : dwellings) {
      if (other == row) {
        continue;
      }
      const UnitRow& them = current.units.rows[other];
      if (them.settlement.value == kInvalidEntityIdValue) {
        continue;
      }
      const float distance = Distance(unit.position, them.position);
      if (distance < nearest) {
        nearest = distance;
        nearest_id = them.settlement;
      }
      if (them.settlement == unit.settlement) {
        own_nearest = std::min(own_nearest, distance);
      }
    }
    if (unit.settlement.value != kInvalidEntityIdValue &&
        FindRow(current.settlements, unit.settlement) != kNoRow) {
      unit.outlying = dwellings.size() > 1 && own_nearest > kJoinMetres ? 1U : 0U;
      continue;
    }
    unit.settlement = SettlementId{};
    if (nearest <= kJoinMetres) {
      unit.settlement = nearest_id;
      unit.outlying = 0;
    } else if (nearest <= kFormMetres) {
      unit.settlement = nearest_id;
      unit.outlying = 1;
    }
  }
  if (MonthsTurn(current.calendar)) {
    TurnTheMonth(config, current);
  }
  // EVERY OTHER UNIT, DAILY: the settlement whose dwellings are nearest; an
  // unassigned dwelling (a candidate) reads its nearest settlement too, as an
  // outlying yard does.
  const std::vector<std::uint32_t> now = DwellingRows(config, current);
  for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
    UnitRow& unit = current.units.rows[row];
    const bool dwelling = std::ranges::find(now, row) != now.end();
    if (dwelling && unit.settlement.value != kInvalidEntityIdValue) {
      continue;
    }
    bool gong = false;
    for (const SettlementRow& settlement : current.settlements.rows) {
      gong = gong || settlement.gong == current.units.row_ids[row];
    }
    if (gong) {
      continue;  // a gong is its own settlement's
    }
    float nearest = std::numeric_limits<float>::infinity();
    SettlementId nearest_id{kStartSettlement};
    for (const std::uint32_t other : now) {
      const UnitRow& them = current.units.rows[other];
      if (them.settlement.value == kInvalidEntityIdValue || other == row) {
        continue;
      }
      const float distance = Distance(unit.position, them.position);
      if (distance < nearest) {
        nearest = distance;
        nearest_id = them.settlement;
      }
    }
    if (!dwelling) {
      unit.settlement = nearest_id;
    }
  }
}

}  // namespace core
