// Implementation of the construction subsystem
// (include/core_construction/construction_system.h; decisions in
// manual/71-construction.md).
//
// One sub-step, run last in the decisions slot, doing three things in a
// fixed order: consume the construction orders of the book, move sites
// through their phases at the day boundary, finish the ones whose labour
// seam has run out. Everything it touches is a unit row — a site IS a unit
// row, at level 0 until the day it is built.

#include "core_construction/construction_system.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "construction_config.h"
#include "core_common/calendar.h"
#include "core_common/event_state.h"
#include "core_common/ids.h"
#include "core_common/order_state.h"
#include "core_common/state_table_ops.h"
#include "core_log/log.h"

namespace core {
namespace {

/// @brief Squared distance: the plot check compares against a sum of radii,
/// and squaring both sides keeps it exact and cheap.
float DistanceSquared(const Vec2& from, const Vec2& to) {
  const float dx = to.x - from.x;
  const float dy = to.y - from.y;
  return (dx * dx) + (dy * dy);
}

/// @brief Emits one event into the step's outbox. Sequential code only —
/// which the whole of this module is (buffer-law rule 5).
void Emit(WorldState& current, EventKind kind, EventSeverity severity, UnitId unit) {
  SimEvent event;
  event.tick = current.calendar.tick;
  event.kind = kind;
  event.severity = severity;
  event.unit = unit;
  current.step_events.push_back(event);
}

class ConstructionSystem final : public IConstructionSystem {
 public:
  explicit ConstructionSystem(ConstructionConfig config) : config_(std::move(config)) {}

  void RunConstructionDecisions(const WorldState& /*previous*/, WorldState& current) override {
    ConsumeOrders(current);
    if (HourFromTick(current.calendar.tick) == 0) {
      DeliverMaterials(current);
    }
    FinishSites(current);
  }

 private:
  // -- orders ---------------------------------------------------------------

  /// Every construction order is decided in the step it is read: kDone or
  /// kRefused, never kAccepted or kActive (71-construction.md §3). The word
  /// is the chairman's; the work that follows is the unit's own state.
  void ConsumeOrders(WorldState& current) {
    // By index, because appending a unit row may reallocate nothing here but
    // the book itself is stable — orders are appended only by the engine.
    for (std::uint32_t row = 0; row < current.orders.rows.size(); ++row) {
      OrderRow& order = current.orders.rows[row];
      if (order.status != OrderStatus::kPending) {
        continue;
      }
      switch (order.kind) {
        case OrderKind::kBuildUnit:
          Settle(order, MarkSite(current, order));
          break;
        case OrderKind::kStartBuild:
          Settle(order, StartWorks(current, order.unit));
          break;
        case OrderKind::kUpgradeUnit:
          Settle(order, StartUpgrade(current, order.unit));
          break;
        case OrderKind::kDemolishUnit:
          Settle(order, Demolish(current, order.unit));
          break;
        default:
          break;  // not ours; another consumer's, or the events slot's refusal
      }
    }
  }

  static void Settle(OrderRow& order, OrderRefusal refusal) {
    order.status = refusal == OrderRefusal::kNone ? OrderStatus::kDone : OrderStatus::kRefused;
    order.refusal = refusal;
  }

  /// Pegs and string: the plot is taken, nothing is spent, and the row waits
  /// at level 0 for kStartBuild (construction design §6).
  OrderRefusal MarkSite(WorldState& current, const OrderRow& order) {
    const std::uint32_t type_row = order.unit_type.value;
    if (type_row >= config_.types.size()) {
      return OrderRefusal::kNoSuchSubject;
    }
    const BuildType& type = config_.types[type_row];
    if (type.player_built == 0 || type.levels.empty()) {
      return OrderRefusal::kRuleForbids;
    }
    if (!GateIsOpen(type.gate, type.era, current.epoch)) {
      return OrderRefusal::kGateClosed;
    }
    // The edge is data (construction_config.h): zero means the table set
    // declares no map, and then there is nothing to be outside of.
    if (config_.map_side_m > 0.0F &&
        !(order.position.x >= 0.0F && order.position.x <= config_.map_side_m &&
          order.position.y >= 0.0F && order.position.y <= config_.map_side_m)) {
      return OrderRefusal::kRuleForbids;
    }
    if (PlotOverlaps(current, order.position, type.plot_radius_m, UnitId{})) {
      return OrderRefusal::kTooClose;
    }

    UnitRow site;
    site.type = order.unit_type;
    site.position = order.position;
    site.level = 0;
    site.construction.phase = ConstructionPhase::kMarked;
    site.construction.target_level = 1;
    AppendRow(current.units, site);
    return OrderRefusal::kNone;
  }

  /// The works start only on the chairman's command — never by themselves
  /// when materials appear (construction design §6).
  OrderRefusal StartWorks(WorldState& current, UnitId unit) {
    const std::uint32_t row = FindRow(current.units, unit);
    if (row == kNoRow) {
      return OrderRefusal::kNoSuchSubject;
    }
    UnitRow& site = current.units.rows[row];
    if (site.construction.phase != ConstructionPhase::kMarked) {
      return OrderRefusal::kRuleForbids;
    }
    OpenWorks(current, unit, site, site.construction.target_level);
    return OrderRefusal::kNone;
  }

  /// A step up the ladder. No marking: the plot is already taken, and the
  /// unit keeps working at its current level while the next one is built
  /// (unit rules §11).
  OrderRefusal StartUpgrade(WorldState& current, UnitId unit) {
    const std::uint32_t row = FindRow(current.units, unit);
    if (row == kNoRow) {
      return OrderRefusal::kNoSuchSubject;
    }
    UnitRow& site = current.units.rows[row];
    if (site.level == 0 || site.construction.phase != ConstructionPhase::kNone) {
      return OrderRefusal::kRuleForbids;
    }
    const std::uint32_t type_row = site.type.value;
    if (type_row >= config_.types.size()) {
      return OrderRefusal::kNoSuchSubject;
    }
    const BuildType& type = config_.types[type_row];
    const std::uint32_t next = static_cast<std::uint32_t>(site.level) + 1;
    if (next > type.levels.size()) {
      return OrderRefusal::kRuleForbids;  // the top of its ladder
    }
    if (type.levels[next - 1].era > static_cast<std::uint8_t>(current.epoch)) {
      return OrderRefusal::kGateClosed;
    }
    OpenWorks(current, unit, site, static_cast<std::uint8_t>(next));
    return OrderRefusal::kNone;
  }

  /// A marked contour goes at once and for free; a standing unit is emptied
  /// first and then dismantled (unit rules §14, construction design §12).
  OrderRefusal Demolish(WorldState& current, UnitId unit) {
    const std::uint32_t row = FindRow(current.units, unit);
    if (row == kNoRow) {
      return OrderRefusal::kNoSuchSubject;
    }
    UnitRow& site = current.units.rows[row];
    if (site.construction.phase == ConstructionPhase::kDemolishing) {
      return OrderRefusal::kRuleForbids;
    }
    if (site.construction.phase == ConstructionPhase::kMarked) {
      RemoveRow(current.units, unit);
      return OrderRefusal::kNone;  // nothing was ever spent on it
    }
    if (site.household.value != kInvalidEntityIdValue || HerdStandsAt(current, unit)) {
      // The living is not demolished (unit rules §14): a household moves out
      // and animals are slaughtered before, and both are the player's doing.
      return OrderRefusal::kNotEmpty;
    }

    const float norm = LevelLaborDays(site.type, site.level);
    MoveStockOut(current, row);
    site.level = 0;
    site.construction.phase = ConstructionPhase::kDemolishing;
    site.construction.target_level = 0;
    site.construction.labor_days_total = norm * config_.demolition_labor_share;
    site.construction.labor_days_remaining = site.construction.labor_days_total;
    site.construction.max_crew = LevelCrew(site.type, site.level == 0 ? 1 : site.level);
    return OrderRefusal::kNone;
  }

  // -- the site's own life ---------------------------------------------------

  /// Opens the works for `level`: straight to done when the class is
  /// marking (the player's outline costs nothing), otherwise delivering.
  void OpenWorks(WorldState& current, UnitId unit, UnitRow& site, std::uint8_t level) {
    site.construction.target_level = level;
    site.construction.max_crew = LevelCrew(site.type, level);
    const BuildLevel* const step = LevelOf(site.type, level);
    if (step != nullptr && step->is_marking != 0) {
      CompleteBuild(current, unit, site);
      return;
    }
    site.construction.phase = ConstructionPhase::kDelivering;
    site.construction.labor_days_total = 0.0F;
    site.construction.labor_days_remaining = 0.0F;
  }

  /// STUB of project phase 1's logistics: whatever the recipe still lacks is
  /// taken from the stores in row order, distance ignored. Task A4 replaces
  /// this with routing behind the same seam — the site's own stock.
  void DeliverMaterials(WorldState& current) {
    for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
      if (current.units.rows[row].construction.phase != ConstructionPhase::kDelivering) {
        continue;
      }
      const UnitTypeId type = current.units.rows[row].type;
      const std::uint8_t level = current.units.rows[row].construction.target_level;
      const BuildLevel* const step = LevelOf(type, level);
      if (step == nullptr) {
        continue;
      }
      bool complete = true;
      for (const BuildMaterial& material : step->recipe) {
        const Grams have = AmountAt(current.units.rows[row].stock, material.resource);
        if (have >= material.grams) {
          continue;
        }
        const Grams taken = TakeFromStores(current, row, material.resource, material.grams - have);
        AddTo(current.units.rows[row].stock, material.resource, taken);
        if (AmountAt(current.units.rows[row].stock, material.resource) < material.grams) {
          complete = false;
        }
      }
      if (complete) {
        UnitRow& site = current.units.rows[row];
        site.construction.phase = ConstructionPhase::kBuilding;
        // Frozen at the start: "invested 40 of 120" must read the same after
        // a balance edit mid-build (71-construction.md §4).
        site.construction.labor_days_total = step->labor_days;
        site.construction.labor_days_remaining = step->labor_days;
        if (site.construction.labor_days_remaining <= 0.0F) {
          CompleteBuild(current, current.units.row_ids[row], site);
        }
      }
    }
  }

  /// A site whose labour seam has run out: the level moves and the recipe is
  /// spent, or the row goes.
  void FinishSites(WorldState& current) {
    std::vector<UnitId> gone;
    for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
      UnitRow& site = current.units.rows[row];
      if (site.construction.labor_days_remaining > 0.0F) {
        continue;
      }
      if (site.construction.phase == ConstructionPhase::kBuilding) {
        CompleteBuild(current, current.units.row_ids[row], site);
      } else if (site.construction.phase == ConstructionPhase::kDemolishing) {
        gone.push_back(current.units.row_ids[row]);
      }
    }
    for (const UnitId unit : gone) {
      Emit(current, EventKind::kUnitDemolished, EventSeverity::kNotable, unit);
      RemoveRow(current.units, unit);
    }
  }

  void CompleteBuild(WorldState& current, UnitId unit, UnitRow& site) {
    const BuildLevel* const step = LevelOf(site.type, site.construction.target_level);
    if (step != nullptr) {
      for (const BuildMaterial& material : step->recipe) {
        AddTo(site.stock, material.resource, -material.grams);
      }
    }
    site.level = site.construction.target_level;
    site.construction = ConstructionState{};
    Emit(current, EventKind::kUnitBuilt, EventSeverity::kNotable, unit);
  }

  // -- the world's own facts -------------------------------------------------

  static bool GateIsOpen(UnitGate gate, std::uint8_t era, Epoch epoch) {
    switch (gate) {
      case UnitGate::kEra:
        return era <= static_cast<std::uint8_t>(epoch);
      case UnitGate::kStart:
        return false;  // stands from day one; there is nothing to build
      case UnitGate::kEvent:
      case UnitGate::kQuest:
      case UnitGate::kUnit:
        // STUB until project phase III: the core has no events, no quests
        // and no "a predecessor stands here". Refusing is the honest answer
        // and matches the design — the power line comes with the newspaper.
        return false;
    }
    return false;
  }

  /// Two units may not stand closer than the sum of their radii (unit rules
  /// §9). A radius of zero takes no part: either the type has no plot at all
  /// or the player draws its outline, and the outline is the presentation's
  /// to guard.
  bool PlotOverlaps(const WorldState& current, const Vec2& place, float radius, UnitId ignore) {
    if (radius <= 0.0F) {
      return false;
    }
    for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
      if (current.units.row_ids[row].value == ignore.value) {
        continue;
      }
      const UnitRow& other = current.units.rows[row];
      if (other.type.value >= config_.types.size()) {
        continue;
      }
      const float other_radius = config_.types[other.type.value].plot_radius_m;
      if (other_radius <= 0.0F) {
        continue;
      }
      const float reach = radius + other_radius;
      if (DistanceSquared(place, other.position) < reach * reach) {
        return true;
      }
    }
    return false;
  }

  static bool HerdStandsAt(const WorldState& current, UnitId unit) {
    for (const HerdRow& herd : current.herds.rows) {
      if (herd.unit.value == unit.value) {
        return true;
      }
    }
    return false;
  }

  /// The demolition's first step: the buffers go to the stores, and what has
  /// nowhere to go is lost — the design says so and says the player is
  /// warned (unit rules §14). The instant-delivery stub again.
  void MoveStockOut(WorldState& current, std::uint32_t row) {
    ResourceAmounts stock = current.units.rows[row].stock;
    current.units.rows[row].stock.clear();
    for (std::size_t index = 0; index < stock.size(); ++index) {
      if (stock[index] <= 0) {
        continue;
      }
      const ResourceId resource{static_cast<std::uint16_t>(index)};
      for (std::uint32_t target = 0; target < current.units.rows.size(); ++target) {
        if (target == row || current.units.rows[target].level == 0) {
          continue;
        }
        AddTo(current.units.rows[target].stock, resource, stock[index]);
        break;
      }
    }
  }

  /// Takes up to `wanted` grams of `resource` from the standing units, in
  /// row order. Sites and unbuilt rows never give: a level-0 unit stores
  /// nothing for anybody (71-construction.md §2).
  static Grams TakeFromStores(WorldState& current,
                              std::uint32_t site_row,
                              ResourceId resource,
                              Grams wanted) {
    Grams taken = 0;
    for (std::uint32_t row = 0; row < current.units.rows.size() && taken < wanted; ++row) {
      if (row == site_row || current.units.rows[row].level == 0) {
        continue;
      }
      const Grams have = AmountAt(current.units.rows[row].stock, resource);
      if (have <= 0) {
        continue;
      }
      const Grams give = have < wanted - taken ? have : wanted - taken;
      AddTo(current.units.rows[row].stock, resource, -give);
      taken += give;
    }
    return taken;
  }

  static Grams AmountAt(const ResourceAmounts& amounts, ResourceId resource) {
    return resource.value < amounts.size() ? amounts[resource.value] : 0;
  }

  static void AddTo(ResourceAmounts& amounts, ResourceId resource, Grams delta) {
    if (delta == 0 || resource.value == kInvalidDefIdValue) {
      return;
    }
    if (resource.value >= amounts.size()) {
      amounts.resize(static_cast<std::size_t>(resource.value) + 1, 0);
    }
    amounts[resource.value] += delta;
    if (amounts[resource.value] < 0) {
      amounts[resource.value] = 0;
    }
  }

  const BuildLevel* LevelOf(UnitTypeId type, std::uint8_t level) const {
    if (type.value >= config_.types.size() || level == 0) {
      return nullptr;
    }
    const std::vector<BuildLevel>& ladder = config_.types[type.value].levels;
    return level > ladder.size() ? nullptr : &ladder[level - 1];
  }

  float LevelLaborDays(UnitTypeId type, std::uint8_t level) const {
    const BuildLevel* const step = LevelOf(type, level);
    return step == nullptr ? 0.0F : step->labor_days;
  }

  std::uint8_t LevelCrew(UnitTypeId type, std::uint8_t level) const {
    const BuildLevel* const step = LevelOf(type, level);
    return step == nullptr ? 0 : step->max_crew;
  }

  ConstructionConfig config_;
};

}  // namespace

std::unique_ptr<IConstructionSystem> CreateConstructionSystem(const ITableSet& tables) {
  ConstructionConfig config;
  std::string error;
  if (!ParseConstructionConfig(tables, config, error)) {
    LogError("construction: " + error);
    return nullptr;
  }
  return std::make_unique<ConstructionSystem>(std::move(config));
}

}  // namespace core
