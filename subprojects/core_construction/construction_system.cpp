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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "construction_config.h"
#include "core_common/calendar.h"
#include "core_common/emit_event.h"
#include "core_common/event_state.h"
#include "core_common/ids.h"
#include "core_common/order_state.h"
#include "core_common/plot.h"
#include "core_common/state_table_ops.h"
#include "core_log/log.h"
#include "core_tables/required_tables.h"
#include "unit_decay.h"

namespace core {
namespace {

/// More parts than any repair can ask for: the cast below is undefined
/// above the destination's range, and the figure comes off a table.
constexpr float kMaxRepairPieces = 1e9F;

/// @brief This module's shorthand: every one of its events is about a unit.
/// The general helper is core_common/emit_event.h, and it is the one a check
/// walks to ask which kinds have an emitter (scripts/event_sites.py) — so
/// this wrapper forwards to it rather than building an event of its own.
void Emit(WorldState& current, EventKind kind, EventSeverity severity, UnitId unit) {
  EmitEvent(current, kind, severity).unit = unit;
}

/// @brief How many residents are assigned to this site right now.
///
/// The assignment is the fact, and the order that made it is not: an order
/// given a year ago says nothing about who is on the site this morning.
std::uint32_t CrewOnSite(const WorldState& completed, UnitId site) {
  std::uint32_t crew = 0;
  for (const ResidentRow& resident : completed.residents.rows) {
    if (resident.work.kind == WorkKind::kConstruction && resident.work.unit.value == site.value) {
      ++crew;
    }
  }
  return crew;
}

class ConstructionSystem final : public IConstructionSystem {
 public:
  explicit ConstructionSystem(ConstructionConfig config) : config_(std::move(config)) {}

  /// What the settlement holds of a resource outside the site itself —
  /// the same reach the delivery stub draws on: built units only, because a
  /// level-0 row is another site and its stock belongs to its own building.
  static Grams HeldEverywhere(const WorldState& world, ResourceId resource) {
    Grams total = 0;
    for (const UnitRow& unit : world.units.rows) {
      if (unit.level == 0) {
        continue;
      }
      total += AmountAt(unit.stock, resource);
    }
    return total;
  }

  /// THE ANSWERS THEMSELVES LIVE IN unit_decay.h. Wear, collapse and the
  /// stink happen to a unit with nobody ordering them; this class is the
  /// half that acts on the chairman's word, and every one of its other
  /// entry points begins with an OrderRow.
  Deadline WearDeadline(const WorldState& completed, UnitId unit) const override {
    return core::WearDeadline(config_, completed, unit);
  }

  StinkStrength StinkFullAt(const WorldState& completed, Vec2 point) const override {
    return core::StinkFullAt(config_, completed, point);
  }

  StinkStrength StinkNowAt(const WorldState& completed, Vec2 point) const override {
    return core::StinkNowAt(config_, completed, point);
  }

  Grams StorageCapacityGrams(UnitTypeId type, std::uint8_t level) const override {
    if (type.value == kInvalidDefIdValue || type.value >= config_.types.size() || level == 0) {
      return -1;
    }
    const BuildType& built = config_.types[type.value];
    // The outline the player draws answers before the ladder is consulted:
    // such a type may still carry rungs (a heap is levelled and gravelled),
    // and none of them means a number to be full against.
    if (built.capacity_by_plot != 0) {
      return -1;
    }
    const std::size_t rung = static_cast<std::size_t>(level) - 1;
    if (rung >= built.levels.size()) {
      return -1;
    }
    return built.levels[rung].storage_capacity_grams;
  }

  /// kSiteWithoutMaterials: a site is delivering and the settlement cannot
  /// complete its recipe. The instant-delivery stub brings whatever there
  /// is (DeliverMaterials), so without this the site would wait for ever in
  /// silence — the player has no other way to learn that the barn is short
  /// four tonnes of boards.
  ///
  /// kSiteUnreachable: twice the road from the NEAREST dwelling does not fit
  /// in the daylight window, so nobody can get there and back in a day. It
  /// was a declared stub until 2026-09-05 — "the core has no roads" — and it
  /// came alive without moving: the question is not roads but HOURS.
  void CollectAlarms(const WorldState& completed, std::vector<Alarm>& alarms) const override {
    for (std::uint32_t row = 0; row < completed.units.rows.size(); ++row) {
      const UnitRow& site = completed.units.rows[row];
      // A site with its materials and nobody on it (alarm_state.h,
      // kSiteWithoutCrew). Counted from the assignments themselves and not
      // from an order that was once given: yesterday's hands are in the
      // fields today.
      if (site.construction.phase == ConstructionPhase::kBuilding &&
          CrewOnSite(completed, completed.units.row_ids[row]) == 0) {
        Alarm alarm;
        alarm.kind = AlarmKind::kSiteWithoutCrew;
        alarm.unit = completed.units.row_ids[row];
        alarm.amount = static_cast<std::int64_t>(site.construction.labor_days_remaining);
        alarms.push_back(alarm);
      }
      // OUT OF REACH: twice the road from the nearest house does not fit in
      // the day. Only where work is actually open — a marked contour is not
      // waiting for anybody yet, and saying it is unreachable before it is
      // started would be a warning about nothing.
      if (site.construction.phase == ConstructionPhase::kDelivering ||
          site.construction.phase == ConstructionPhase::kBuilding) {
        const float road = core::NearestDwellingHours(config_, completed, site.position);
        if (road > 0.0F && 2.0F * road >= completed.weather.daylight_hours) {
          Alarm alarm;
          alarm.kind = AlarmKind::kSiteUnreachable;
          alarm.unit = completed.units.row_ids[row];
          alarm.amount = static_cast<std::int64_t>(road);
          alarms.push_back(alarm);
        }
      }
      if (site.construction.phase != ConstructionPhase::kDelivering) {
        continue;
      }
      const BuildLevel* const step = LevelOf(site.type, site.construction.target_level);
      if (step == nullptr) {
        continue;
      }
      // The FIRST material short, in recipe order: one alarm names one
      // shortfall, and the recipe's own order is the deterministic choice.
      for (const BuildMaterial& material : step->recipe) {
        const Grams on_site = AmountAt(site.stock, material.resource);
        if (on_site >= material.grams) {
          continue;
        }
        const Grams lacking = material.grams - on_site;
        const Grams available = HeldEverywhere(completed, material.resource);
        if (available >= lacking) {
          continue;  // the stores can still cover it; the stub will bring it
        }
        Alarm alarm;
        alarm.kind = AlarmKind::kSiteWithoutMaterials;
        alarm.unit = completed.units.row_ids[row];
        alarm.resource = material.resource;
        alarm.amount = lacking - available;
        alarms.push_back(alarm);
        break;
      }
    }
  }

  void RunConstructionDecisions(const WorldState& /*previous*/, WorldState& current) override {
    ConsumeOrders(current);
    if (HourFromTick(current.calendar.tick) == 0) {
      // Wear BEFORE the day's deliveries and before anything finishes: a
      // repair ordered today is priced on the wear the unit woke up with,
      // and a house that reaches the top of the scale falls before the
      // deliveries walk the rows it is no longer in (task A5).
      AgeUnits(current);
      core::MoveStinkZones(config_, current);
      DeliverMaterials(current);
    }
    FinishSites(current);
  }

 private:
  // -- orders ---------------------------------------------------------------

  /// @brief Wear of a day (task A5, manual/73-wear-and-repair.md §2).
  ///
  /// The paragraph that opened this block belongs to ConsumeOrders and has
  /// gone back to it: two descriptions had been welded into one with no
  /// break between them, so the published brief of this function was a rule
  /// about orders, which it neither reads nor settles.
  ///
  /// Every BUILT
  /// unit whose type has a building ages by the amortization of the level it
  /// STANDS at — a whole scale in `wear_years_idle` years standing empty, in
  /// `wear_years_in_use` years while a household lives there or somebody
  /// works there today — or something is simply LYING in it. Clamped at 100:
  /// a ruin still works and never vanishes.
  ///
  /// IN USE IS THE SHORTER TERM: "works — wears faster; stands — hardly ages
  /// at all" (unit rules §15). The tables carry both figures; nothing here
  /// decides which is bigger, and nothing should. The start's old houses are the exception the
  /// canon names — they run on their own term and collapse at the top (start design §4).
  void AgeUnits(WorldState& current) {
    std::vector<UnitId> collapsed;
    for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
      UnitRow& unit = current.units.rows[row];
      if (unit.level == 0 || unit.type.value >= config_.types.size()) {
        continue;  // a site is not a building yet
      }
      const BuildType& type = config_.types[unit.type.value];
      if (type.has_wear == 0) {
        continue;  // a heap, a stack, a trench: nothing to wear
      }
      if (unit.paused != 0) {
        // A STOPPED UNIT DOES NOT WEAR OUT (unit rules §15, and §5 lists it
        // among what a pause changes). This is the one effect of a pause the
        // slice can actually show: the rest of §5 needs unit work cycles,
        // and the core has none. Task A8.
        continue;
      }
      const bool is_old_house = core::TypeIsOldHouse(config_, unit.type);
      const float years = is_old_house
                              ? config_.old_house_collapse_years
                              : core::WearYears(type, unit.level, core::InUse(current, row));
      if (!(years > 0.0F)) {
        continue;  // the ladder names no term for this level: it does not wear
      }
      // The class gives the base term; the nature of the unit and what it
      // stands on both correct it (WearPace). A faster pace is a SHORTER
      // life, hence the multiply on the daily share.
      const float pace = core::WearPace(type, unit.level);
      unit.wear += kWearScale * pace / (years * static_cast<float>(kDaysPerYear));
      if (unit.wear < kWearScale) {
        continue;
      }
      unit.wear = kWearScale;
      if (is_old_house) {
        collapsed.push_back(current.units.row_ids[row]);
      }
    }
    for (const UnitId unit : collapsed) {
      Collapse(current, unit);
    }
  }

  /// An old house at the top of the scale falls (start design §4, housing
  /// design §10) — the ONE unit in the game that vanishes from wear. The
  /// household it sheltered is left without a house on purpose: rehousing is
  /// the demography sub-step's job the next day, by the same path a newly
  /// wed couple takes. Clearing the family's `house` is the one field of
  /// another module's row this subsystem writes, and the contract says so.
  void Collapse(WorldState& current, UnitId unit) {
    const std::uint32_t row = FindRow(current.units, unit);
    if (row == kNoRow) {
      return;
    }
    const FamilyId household = current.units.rows[row].household;
    MoveStockOut(current, row);
    const std::uint32_t family_row = FindRow(current.families, household);
    if (family_row != kNoRow) {
      current.families.rows[family_row].house = UnitId{};
    }
    Emit(current, EventKind::kUnitCollapsed, EventSeverity::kNotable, unit);
    RemoveRow(current.units, unit);
  }

  /// kRepairUnit (task A5; construction design §11): a site on a STANDING
  /// unit, exactly like an upgrade — deliver the parts, then invest the
  /// labour, and the unit works all the while. The price is frozen at the
  /// order, scaled by the wear it was ordered at: a neglected repair is
  /// dearer in man-days and in parts alike.
  OrderRefusal StartRepair(WorldState& current, UnitId unit) {
    const std::uint32_t row = FindRow(current.units, unit);
    if (row == kNoRow) {
      return OrderRefusal::kNoSuchSubject;
    }
    UnitRow& site = current.units.rows[row];
    if (site.level == 0 || site.construction.phase != ConstructionPhase::kNone) {
      return OrderRefusal::kRuleForbids;  // a site is one thing at a time
    }
    if (site.type.value >= config_.types.size() || config_.types[site.type.value].has_wear == 0) {
      return OrderRefusal::kRuleForbids;  // nothing to wear, nothing to mend
    }
    if (core::TypeIsOldHouse(config_, site.type)) {
      // "They are to be replaced, not improved" (housing design §10).
      return OrderRefusal::kRuleForbids;
    }
    if (!(site.wear > 0.0F)) {
      return OrderRefusal::kRuleForbids;  // nothing worn
    }
    if (config_.spare_part_resource.value == kInvalidDefIdValue) {
      return OrderRefusal::kRuleForbids;  // the tables carry no spare part
    }
    const float norm = LevelLaborDays(site.type, site.level);
    const float days = norm * config_.repair_labor_share * (site.wear / kWearScale);
    site.construction.phase = ConstructionPhase::kDelivering;
    // The level does not move, and target_level says so out loud: every
    // reader of the site block sees "this unit stays where it is".
    site.construction.target_level = site.level;
    site.construction.labor_days_total = days;
    site.construction.labor_days_remaining = 0.0F;  // set when the parts are in
    site.construction.max_crew = LevelCrew(site.type, site.level);
    return OrderRefusal::kNone;
  }

  /// Spare parts a repair of this many man-days eats, in grams. Parts are
  /// counted in PIECES by the design and in grams by every store, and the
  /// recipe's own rule converts between them — one place, one formula
  /// (construction design §2; BuildMaterial in construction_config.h).
  Grams RepairPartsGrams(float labor_days) const {
    const float pieces = labor_days * config_.repair_spare_parts_per_labor_day;
    if (!(pieces > 0.0F) || pieces > kMaxRepairPieces) {
      return 0;
    }
    return static_cast<Grams>(pieces) * config_.spare_part_grams;
  }

  /// @brief Every construction order is decided in the step it is read:
  /// kDone or kRefused, never kAccepted or kActive (71-construction.md §3).
  /// The word is the chairman's; the work that follows is the unit's own
  /// state.
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
        case OrderKind::kRepairUnit:
          Settle(order, StartRepair(current, order.unit));
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
    if (config_.definitions.map_side_m > 0.0F &&
        !(order.position.x >= 0.0F && order.position.x <= config_.definitions.map_side_m &&
          order.position.y >= 0.0F && order.position.y <= config_.definitions.map_side_m)) {
      return OrderRefusal::kRuleForbids;
    }
    // The plot where the type has one, the BODY where it does not: a well
    // and a lamp post cannot stand in the same metre either, and until
    // 2026-09-05 they took no part in this rule at all.
    const float radius = type_row < config_.definitions.units.keep_out_radius_m.size()
                             ? config_.definitions.units.keep_out_radius_m[type_row]
                             : 0.0F;
    if (PlotOverlaps(current, order.position, radius, UnitId{})) {
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

  /// @brief The delivery half of a repair: bring the spare parts the frozen norm
  /// calls for, and move to kRepairing when they are all on site. The same
  /// shape as a build's delivery — one resource instead of a recipe.
  void DeliverRepairParts(WorldState& current, std::uint32_t row) {
    UnitRow& site = current.units.rows[row];
    const ResourceId parts = config_.spare_part_resource;
    const Grams need = RepairPartsGrams(site.construction.labor_days_total);
    const Grams have = AmountAt(site.stock, parts);
    if (have < need) {
      const Grams taken = TakeFromStores(current, row, parts, need - have);
      AddTo(current.units.rows[row].stock, parts, taken);
    }
    if (AmountAt(current.units.rows[row].stock, parts) < need) {
      return;  // still short; kSiteWithoutMaterials speaks for it (task A3)
    }
    UnitRow& ready = current.units.rows[row];
    ready.construction.phase = ConstructionPhase::kRepairing;
    ready.construction.labor_days_remaining = ready.construction.labor_days_total;
    if (ready.construction.labor_days_remaining <= 0.0F) {
      CompleteRepair(current, current.units.row_ids[row], ready);
    }
  }

  /// A repair that has had its labour: the parts are used up, the wear is
  /// gone, the level never moved. "An upgrade repairs on its way" is the
  /// other half of the same rule and lives in CompleteBuild.
  void CompleteRepair(WorldState& current, UnitId unit, UnitRow& site) {
    const Grams used = RepairPartsGrams(site.construction.labor_days_total);
    AddTo(site.stock, config_.spare_part_resource, -used);
    site.wear = 0.0F;
    // AND THIS IS THE "UNTIL IT IS RESTORED" of the dead byte (unit_state.h).
    // The start's wrecked mill comes back into service here and nowhere
    // else — a state nothing clears is not a state, it is a verdict.
    site.dead = 0;
    site.construction = ConstructionState{};
    Emit(current, EventKind::kUnitRepaired, EventSeverity::kNotable, unit);
  }

  /// @brief STUB of project phase 1's logistics: whatever the recipe still
  /// lacks is taken from the stores in row order, distance ignored. Task A4
  /// replaces this with routing behind the same seam — the site's own stock.
  ///
  /// It had been standing over DeliverRepairParts, which has no recipe at
  /// all — and that function's own next sentence said so, so one block
  /// contradicted itself across two paragraphs.
  void DeliverMaterials(WorldState& current) {
    for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
      if (current.units.rows[row].construction.phase != ConstructionPhase::kDelivering) {
        continue;
      }
      // A REPAIR asks for spare parts and nothing else (construction design
      // §2), so its "recipe" is one line computed from the frozen norm — the
      // level's own recipe would rebuild the barn instead of mending it.
      if (current.units.rows[row].construction.target_level == current.units.rows[row].level &&
          current.units.rows[row].level > 0) {
        DeliverRepairParts(current, row);
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
      } else if (site.construction.phase == ConstructionPhase::kRepairing) {
        CompleteRepair(current, current.units.row_ids[row], site);
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
    // "Any level upgrade repairs the unit entirely" (unit rules §11): the
    // amortization term starts again, and that is why repairing before an
    // upgrade is pointless rather than merely wasteful.
    site.wear = 0.0F;
    // An upgrade repairs on its way, and a rebuild is the fullest repair
    // there is: it revives as surely as CompleteRepair does.
    site.dead = 0;
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
  /// §9). The rule itself lives in core_common/plot.h, where the wedding
  /// stub can reach it too: it used to be private to this class, and every
  /// unit row that appeared WITHOUT an order — the houses the residents
  /// module appends — was outside it (boss, 2026-09-04).
  bool PlotOverlaps(const WorldState& current, const Vec2& place, float radius, UnitId ignore) {
    return core::PlotOverlaps(current.units, config_.definitions.Plots(), place, radius, ignore);
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
  /// What a demolished unit was holding goes out through the store door —
  /// none of the receivers above its ceiling (task A3,
  /// manual/72-storage-and-alarms.md §2). What no store has room for is
  /// GONE, and booked to the year's lost_no_room: demolishing a full barn with
  /// nowhere to put its contents is the player's decision, and the cost of
  /// it belongs in the book rather than in silence.
  void MoveStockOut(WorldState& current, std::uint32_t row) {
    ResourceAmounts stock = current.units.rows[row].stock;
    current.units.rows[row].stock.clear();
    for (std::size_t index = 0; index < stock.size(); ++index) {
      if (stock[index] <= 0) {
        continue;
      }
      const ResourceId resource = DefIdFromIndex<ResourceIdTag>(index);
      const Grams placed = DeliverOut(current, row, resource, stock[index]);
      AddLedgerAmount(current.ledger.current.lost_no_room, resource, stock[index] - placed);
    }
  }

  /// Free room of a built store in grams, or 0 when it is not a store the
  /// core can measure. An outline the player drew (a heap, a stack) has no
  /// number and is never full: it takes whatever is offered.
  Grams FreeRoomOf(const UnitRow& unit) const {
    if (unit.level == 0 || unit.type.value >= config_.types.size()) {
      return 0;
    }
    const BuildType& type = config_.types[unit.type.value];
    if (type.capacity_by_plot != 0) {
      return std::numeric_limits<Grams>::max();
    }
    // The step the unit STANDS at, and nowhere else: the type row's own
    // figure was a copy of level 1 and has been taken out of the config.
    const std::size_t index = static_cast<std::size_t>(unit.level) - 1;
    const Grams capacity =
        index < type.levels.size() ? type.levels[index].storage_capacity_grams : 0;
    if (capacity <= 0) {
      return 0;
    }
    Grams held = 0;
    for (const Grams amount : unit.stock) {
      if (amount > 0) {
        held += amount;
      }
    }
    return held >= capacity ? 0 : capacity - held;
  }

  /// Puts `amount` into the built, numbered stores other than `from`, none
  /// above its capacity; returns what went in. The construction module has
  /// its own copy of the rule rather than a dependency on core_production:
  /// two subject-tier modules do not call each other (CLAUDE.md §7), and
  /// what it needs is one number per unit type, which its own config holds.
  Grams DeliverOut(WorldState& current, std::uint32_t from, ResourceId resource, Grams amount) {
    Grams placed = 0;
    for (std::uint32_t target = 0; target < current.units.rows.size() && placed < amount;
         ++target) {
      if (target == from) {
        continue;
      }
      UnitRow& unit = current.units.rows[target];
      const Grams room = FreeRoomOf(unit);
      if (room <= 0) {
        continue;
      }
      const Grams left = amount - placed;
      const Grams take = room < left ? room : left;
      AddTo(unit.stock, resource, take);
      placed += take;
    }
    return placed;
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

std::unique_ptr<IConstructionSystem> CreateConstructionSystem(const ITableSet& tables,
                                                              StubTables stubs) {
  // THE DEFAULTS ARE LEGITIMATE AND THEIR SILENCE WAS NOT
  // (core_tables/stub_tables.h). A caller that has not said it wants
  // this module's documented defaults is refused by name, so that a
  // table set which is merely INCOMPLETE cannot pass for one that is
  // as its author meant it.
  //
  // THE LIST IS THE WHOLE READ SET (core_tables/required_tables.h). The two
  // at the end joined it on 2026-09-08: the cost table and the transport
  // table are read by ParseConstructionConfig and defaulted in silence.
  if (!RequireTables(tables,
                     stubs,
                     "construction",
                     {"construction",
                      "unit_types",
                      "unit_levels",
                      "resources",
                      "unit_level_cost",
                      "transport"},
                     nullptr)) {
    return nullptr;
  }

  ConstructionConfig config;
  std::string error;
  if (!ParseConstructionConfig(tables, stubs, config, error)) {
    LogError("construction: " + error);
    return nullptr;
  }
  return std::make_unique<ConstructionSystem>(std::move(config));
}

}  // namespace core
