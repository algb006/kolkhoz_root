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
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "construction_config.h"
#include "core_common/calendar.h"
#include "core_common/emit_event.h"
#include "core_common/event_state.h"
#include "core_common/ids.h"
#include "core_common/module_rules.h"
#include "core_common/order_state.h"
#include "core_common/plot.h"
#include "core_common/state_table_ops.h"
#include "core_log/log.h"
#include "core_tables/required_tables.h"
#include "field_camp.h"
#include "insulation.h"
#include "site_supply.h"
#include "unit_decay.h"

namespace core {
namespace {

/// More parts than any repair can ask for: the cast below is undefined
/// above the destination's range, and the figure comes off a table.
constexpr float kMaxRepairPieces = 1e9F;

/// The random stream a fire is drawn from. Its own id, so a daily roll per
/// building never shifts the draws of any other system of the world — the
/// same reason the night pasture's camp has one (night_pasture.cpp).
constexpr std::uint64_t kFireStream = 0x4649524553ULL;  // "FIRES"

/// What a fire adds to a building's wear, on the 0..100 scale. A third of the
/// scale: enough that a repair is worth ordering and the loss is felt, far
/// short of destroying anything.
///
/// A CHOSEN NUMBER AND NOT A MEASURED ONE, and it has no knob on purpose:
/// the design prices a fire by how long it burned before it was put out, and
/// that is the extinguishing model this stub does not have (polish P32a2).
/// A knob would offer a dial for a quantity nobody has decided how to derive,
/// which is worse than a constant that says it is a stub.
constexpr float kFireWearScar = 33.0F;

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
      total += UnreservedOf(unit, resource);  // another works' recipe is not in hand
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

  /// The works a start would open: a marked site's own target level, or a
  /// standing unit's next rung (construction design §6). Anything already
  /// under way, a repair among it, has its materials reserved and answers
  /// nothing short.
  std::vector<MaterialShortfall> MaterialsShortFor(const WorldState& completed,
                                                   UnitId unit) const override {
    const std::uint32_t row = FindRow(completed.units, unit);
    if (row == kNoRow) {
      return {};
    }
    const UnitRow& site = completed.units.rows[row];
    if (site.construction.phase == ConstructionPhase::kMarked) {
      return ShortfallOf(completed, row, site.construction.target_level);
    }
    if (site.construction.phase == ConstructionPhase::kNone && site.level > 0 &&
        site.type.value < config_.types.size() &&
        static_cast<std::size_t>(site.level) < config_.types[site.type.value].levels.size()) {
      return ShortfallOf(completed, row, static_cast<std::uint8_t>(site.level + 1U));
    }
    return {};
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
        // THE ACCOUNTANT'S QUESTION AND NOT A GENEROUS ONE (2026-09-13): the
        // road is too long for him past travel_limit_hours, or when the road
        // there and back leaves less than min_usable_hours of the day
        // (core_labor/assignment.cpp, ConsiderCandidate). The alarm asked only
        // whether the round trip fits in daylight — a summer day of sixteen
        // hours let it stay silent up to eight hours of road while the
        // accountant stopped at four, and on seed 1933 a granary two and a
        // half kilometres out stood crewless all year with 300 hands idle and
        // no word of why but "no crew".
        const bool too_long = road > config_.travel_limit_hours;
        const bool no_day_left =
            completed.weather.daylight_hours - (2.0F * road) < config_.min_usable_hours;
        if (road > 0.0F && (too_long || no_day_left)) {
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
      RunFires(current);
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

  /// @brief One day's fires (fire design; quest_e1_13 «Огонь и вода»).
  ///
  /// STUB — A FIRE THAT IS ALWAYS PUT OUT. The design's own promise is that
  /// «у людей всегда есть шанс потушить до разрушения» and that «обычно
  /// успевают», with the outcome decided by who runs and how fast. None of
  /// that exists here: the gathering radius and the rate of extinguishing are
  /// in the polish backlog (P32a2). So this stub sits ABOVE the promise
  /// rather than against it — the fire scars the building and never destroys
  /// it — and what is missing is the CHANCE of losing one, not the fighting
  /// of it. A fire that could not be fought would be the opposite of what the
  /// design guarantees, which is why the narrow form was refused (boss,
  /// parcel 110).
  ///
  /// WHAT BURNS IS `has_wear`, which is not a new column but the one that
  /// already separates a building from a heap: a stack, a pile and a trench
  /// have no wear and no fire. It gives the design's one exception for free —
  /// the police post is the only heated building that neither wears nor
  /// burns, and the table already says has_wear = 0 for it. THE HAY STACK IS
  /// THE STUB'S OWN EDGE: the design has it burning though it is not a
  /// building, and it has no wear for a fire to take, so it does not burn
  /// here. Said out loud rather than left to be discovered.
  ///
  /// THE SAME CHANCE FOR EVERY TYPE. `fire_risk` in the registry is prose and
  /// the per-type multiplier is polish, so a granary and a bathhouse burn
  /// alike today — named here so the next reader does not go looking for the
  /// reason.
  ///
  /// THE DRAW COMES FROM ITS OWN RNG STREAM, like the night pasture's camp.
  /// A daily roll per building against the world's sequential generator would
  /// shift every later draw in the world, and every run would diverge from
  /// the reshuffling rather than from the fires.
  void RunFires(WorldState& current) {
    // The grace period of difficulty design §3 — «в начале партии их нет».
    if (static_cast<std::uint32_t>(current.calendar.day) < config_.fire_grace_days) {
      return;
    }
    const bool frost = current.weather.air_temperature_celsius < 0.0F;
    const float chance = config_.fire_base_chance_per_unit_year *
                         (frost ? config_.fire_frost_factor : 1.0F) /
                         static_cast<float>(kDaysPerYear);
    if (!(chance > 0.0F)) {
      return;
    }
    // THE DAY IS IN THE SEED, and the acceptance is what found that it had to
    // be. Seeded from the world's state alone, a DAILY draw repeats itself
    // exactly whenever nothing else has moved the generator — the same
    // buildings catch every morning, or none ever do. The night pasture's
    // camp gets away without it because it is drawn ONCE in a campaign; a
    // rule that rolls every day may not lean on other systems happening to
    // advance the stream for it. Same fold as the district's delivery delay,
    // for the same reason.
    RngState rng = SeedRngState(current.rng.state ^ current.calendar.day, kFireStream);
    for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
      UnitRow& unit = current.units.rows[row];
      if (unit.level == 0 || unit.type.value >= config_.types.size()) {
        continue;  // a site has nothing to burn
      }
      if (config_.types[unit.type.value].has_wear == 0) {
        continue;  // a heap, a stack, a trench — and the police post
      }
      if (unit.paused != 0) {
        continue;  // nobody stokes a stopped building, as nothing wears it
      }
      if (NextRandomUnitFloat(rng) >= chance) {
        continue;
      }
      // THE SCAR NEVER REACHES THE TOP OF THE SCALE. AgeUnits collapses a
      // unit that gets there, and a fire that destroyed a building would be
      // exactly what this stub's form forbids. So the damage is taken up to
      // one step below the end and no further: the building is hurt, the
      // repair is worth ordering, and nothing is lost.
      //
      // AND THE CEILING IS TAKEN ONLY WHERE IT RAISES. Written as a plain
      // clamp this was the one writer in the tree that LOWERED wear: a unit
      // AgeUnits had left at exactly 100 came out of a fire at 99, so the
      // fire healed the ruin it was supposed to scar. «100 — a ruin still
      // works and never vanishes» is the field's own contract, and a fire
      // may not walk it back.
      const float scarred = unit.wear + kFireWearScar;
      const float ceiling = kWearScale - 1.0F;
      unit.wear = std::max(unit.wear, std::min(scarred, ceiling));
      SimEvent& fire = EmitEvent(current, EventKind::kFireBroke, EventSeverity::kInterrupting);
      fire.unit = current.units.row_ids[row];
    }
  }

  /// An old house at the top of the scale falls (start design §4, housing
  /// design §10) — the ONE unit in the game that vanishes from wear. The
  /// household it sheltered is left without a house on purpose: the roof is
  /// the demography sub-step's job the next day (housing design §20: a free
  /// house, or a tent on the old plot in the warm season, or leaving).
  /// Clearing the family's `house` and writing where it stood are the two
  /// fields of another module's row this subsystem writes, and the contract
  /// says so (family_state.h, lost_house_position).
  void Collapse(WorldState& current, UnitId unit) {
    const std::uint32_t row = FindRow(current.units, unit);
    if (row == kNoRow) {
      return;
    }
    const FamilyId household = current.units.rows[row].household;
    const Vec2 stood_at = current.units.rows[row].position;
    MoveStockOut(current, row);
    const std::uint32_t family_row = FindRow(current.families, household);
    if (family_row != kNoRow) {
      current.families.rows[family_row].house = UnitId{};
      current.families.rows[family_row].lost_house_position = stood_at;
    }
    SimEvent& fell = EmitEvent(current, EventKind::kUnitCollapsed, EventSeverity::kNotable);
    fell.unit = unit;
    if (family_row != kNoRow) {
      fell.family = household;
      std::int64_t lived_in = 0;
      for (const ResidentRow& resident : current.residents.rows) {
        lived_in += resident.family.value == household.value ? 1 : 0;
      }
      fell.amount = lived_in;
    }
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
        case OrderKind::kInsulateUnit:
          Settle(order, StartInsulation(config_, current, order.unit));
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
    const float radius = KeepOutRadius(type_row);
    // A MODULE stands on its parent's plot, and only while the parent stands
    // sound (unit rules §11, "Модули"; boss, 2026-09-13). Its own parent and
    // the parent's other modules are the plots it may come inside; every
    // other plot keeps the rule.
    UnitId parent;
    if (IsModuleType(type_row)) {
      parent = ParentForModule(current, config_.definitions.units.parent[type_row], order.position);
      if (parent.value == kInvalidEntityIdValue) {
        return OrderRefusal::kNoParent;
      }
    }
    if (PlotOverlaps(current, order.position, radius, UnitId{}, parent)) {
      return OrderRefusal::kTooClose;
    }
    // The camp's own ground rule, after the plot: a camp on another's plot is
    // too close before it is anywhere at all.
    if (order.unit_type.value == config_.field_camp_type.value) {
      const OrderRefusal camp = FieldCampPlacementRefusal(config_, current, order.position);
      if (camp != OrderRefusal::kNone) {
        return camp;
      }
    }

    UnitRow site;
    site.type = order.unit_type;
    site.parent = parent;
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
    if (!ModuleParentSound(current, site)) {
      return OrderRefusal::kNoParent;
    }
    if (!ShortfallOf(current, row, site.construction.target_level).empty()) {
      return OrderRefusal::kMaterialsShort;
    }
    OpenWorks(current, unit, site, site.construction.target_level);
    ReserveRecipe(current, row);
    return OrderRefusal::kNone;
  }

  /// @brief What the village lacks of the recipe of `level` for the site in
  /// `row`: held = the site's own stock + what every other built unit may
  /// give (the stores and the heaps, the reach TakeFromStores draws on, less
  /// what their own works hold back). Lines where held < needed only
  /// (construction design §6).
  std::vector<MaterialShortfall> ShortfallOf(const WorldState& world,
                                             std::uint32_t row,
                                             std::uint8_t level) const {
    std::vector<MaterialShortfall> short_lines;
    const BuildLevel* const step = LevelOf(world.units.rows[row].type, level);
    if (step == nullptr || step->is_marking != 0) {
      return short_lines;
    }
    for (const BuildMaterial& material : step->recipe) {
      Grams held = AmountAt(world.units.rows[row].stock, material.resource);
      for (std::uint32_t other = 0; other < world.units.rows.size(); ++other) {
        if (other != row && world.units.rows[other].level > 0) {
          held += UnreservedOf(world.units.rows[other], material.resource);
        }
      }
      if (held < material.grams) {
        short_lines.push_back(MaterialShortfall{
            .resource = material.resource, .needed = material.grams, .held = held});
      }
    }
    return short_lines;
  }

  /// @brief THE MATERIALS BECOME THE SITE'S AT THE START (construction design
  /// §6; boss's reading, parcel 288): the checked recipe is carried onto the
  /// site the same tick, so no saw, no other building and no issue takes it —
  /// a level-0 row stores nothing for anybody, and TakeFromStores never draws
  /// on a site. Without it the check would guarantee nothing a day later.
  void ReserveRecipe(WorldState& current, std::uint32_t row) {
    if (current.units.rows[row].construction.phase == ConstructionPhase::kDelivering) {
      DeliverSite(current, row);
    }
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
    if (!ModuleParentSound(current, site)) {
      return OrderRefusal::kNoParent;
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
    if (!ShortfallOf(current, row, static_cast<std::uint8_t>(next)).empty()) {
      return OrderRefusal::kMaterialsShort;
    }
    OpenWorks(current, unit, site, static_cast<std::uint8_t>(next));
    ReserveRecipe(current, row);
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
    // An upgrade stopped by the demolition: its recipe went out with the
    // rest of the stock, and nothing is held back for works that are gone.
    site.construction.reserved.clear();
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
    const BuildMaterial line{.resource = parts, .grams = need};
    HoldBack(current.units.rows[row], std::span<const BuildMaterial>(&line, 1));
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
    AddLedgerAmount(current.ledger.current.built_in, config_.spare_part_resource, used);
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
      const ConstructionPhase phase = current.units.rows[row].construction.phase;
      if (phase != ConstructionPhase::kDelivering && phase != ConstructionPhase::kInsulating) {
        continue;
      }
      // A module is not built while its parent does not stand sound (unit
      // rules §11): nothing is carried to it either, so no material is locked
      // in a site that cannot move. And nothing is carried to a PAUSED
      // building (construction design §6) — an insulation job included, which
      // is an upgrade by unit rules §16 (boss, parcel 376).
      if (!ModuleParentSound(current, current.units.rows[row]) ||
          current.units.rows[row].paused != 0) {
        continue;
      }
      if (phase == ConstructionPhase::kInsulating) {
        DeliverInsulation(config_, current, row);
      } else {
        DeliverSite(current, row);
      }
    }
  }

  /// @brief One site's delivery: what its recipe (or a repair's parts) still
  /// lacks is taken from the stores, and at the full recipe the labour opens.
  void DeliverSite(WorldState& current, std::uint32_t row) {
    {
      // A REPAIR asks for spare parts and nothing else (construction design
      // §2), so its "recipe" is one line computed from the frozen norm — the
      // level's own recipe would rebuild the barn instead of mending it.
      if (current.units.rows[row].construction.target_level == current.units.rows[row].level &&
          current.units.rows[row].level > 0) {
        DeliverRepairParts(current, row);
        return;
      }
      const UnitTypeId type = current.units.rows[row].type;
      const std::uint8_t level = current.units.rows[row].construction.target_level;
      const BuildLevel* const step = LevelOf(type, level);
      if (step == nullptr) {
        return;
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
      HoldBack(current.units.rows[row], step->recipe);
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
      } else if (InsulationDone(config_, current, row)) {
        CompleteInsulation(config_, current, row);
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
        // Into the walls, with a line (ledger_state.h, built_in).
        AddLedgerAmount(current.ledger.current.built_in, material.resource, material.grams);
      }
    }
    site.level = site.construction.target_level;
    // "The third level rebuilds the walls and the roof": the straw comes off
    // with them, and only there (unit rules §16).
    if (static_cast<float>(site.level) == config_.insulation_reset_level) {
      site.insulated = 0;
    }
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
  bool PlotOverlaps(const WorldState& current,
                    const Vec2& place,
                    float radius,
                    UnitId ignore,
                    UnitId module_parent = UnitId{}) {
    return core::PlotOverlaps(
        current.units, config_.definitions.Plots(), place, radius, ignore, module_parent);
  }

  float KeepOutRadius(std::uint32_t type_row) const {
    const std::vector<float>& radii = config_.definitions.units.keep_out_radius_m;
    return type_row < radii.size() ? radii[type_row] : 0.0F;
  }

  bool IsModuleType(std::uint32_t type_row) const {
    const std::vector<UnitTypeId>& parents = config_.definitions.units.parent;
    return type_row < parents.size() && parents[type_row].value != kInvalidDefIdValue;
  }

  /// @brief The unit a module marked at `place` belongs to: the first unit, in
  /// row order, of `parent_type` that stands sound and whose plot holds the
  /// module's CENTRE. Invalid when none does — no such unit, not built yet,
  /// dead, paused, or the centre lies outside every yard.
  ///
  /// THE CENTRE, NOT THE WHOLE PLOT (boss, 2026-09-13, parcel 206). The first
  /// version asked for the module's whole circle inside the yard's, and by the
  /// shipped radii a food yard of 45 m then held one 25-metre module and no
  /// second: a module's radius is its patch in the view, not a claim on land.
  /// Where its edge reaches over the fence onto a neighbour, the overlap rule
  /// still refuses it with kTooClose.
  UnitId ParentForModule(const WorldState& current,
                         UnitTypeId parent_type,
                         const Vec2& place) const {
    const float reach = KeepOutRadius(parent_type.value);
    for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
      const UnitRow& unit = current.units.rows[row];
      if (unit.type.value != parent_type.value || !StandsSoundAsParent(unit)) {
        continue;
      }
      const float dx = unit.position.x - place.x;
      const float dy = unit.position.y - place.y;
      if ((dx * dx) + (dy * dy) <= reach * reach) {
        return current.units.row_ids[row];
      }
    }
    return UnitId{};
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

  /// @brief Holds back for a STANDING unit's works what its stock covers of
  /// each line: reserved = min(stock, line), recomputed after every delivery
  /// (ConstructionState::reserved; boss, parcel 294). A level-0 site is left
  /// alone — nobody takes from a site, so it needs no such line.
  static void HoldBack(UnitRow& unit, std::span<const BuildMaterial> lines) {
    if (unit.level == 0) {
      return;
    }
    for (const BuildMaterial& line : lines) {
      const Grams held = AmountAt(unit.stock, line.resource);
      const Grams keep = held < line.grams ? held : line.grams;
      AddTo(unit.construction.reserved,
            line.resource,
            keep - AmountAt(unit.construction.reserved, line.resource));
    }
  }

  static Grams AmountAt(const ResourceAmounts& amounts, ResourceId resource) {
    return resource.value < amounts.size() ? amounts[resource.value] : 0;
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
