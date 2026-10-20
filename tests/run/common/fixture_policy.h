/// @file
/// @brief The buildings the run's chairman puts up, because a run has no
/// chairman and the start canon gives the village neither.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THE RUN BUILDS THIS. The start canon gives the village no granary at
/// all — the grain lies in the church, sixty tonnes of it, and putting up a
/// granary is one of the first things a chairman does (production units
/// design, "the elevator — when the granary is not enough"). That poverty is
/// the GAME, not a defect in the tables, and it is not to be tabled away.
///
/// But a thirty-year run has no chairman, so until task A4 it measured a
/// village forbidden to make its first real decision — and did not show it,
/// because delivery was free and instant and hid the shortage behind a price
/// of zero. The moment carrying cost something, the harvest sat in the field
/// until the snow took it: nine tonnes of rye in the fourth year alone.
///
/// So the run plays the player here exactly as it does for the horse yard
/// (yard_policy.h, task A7). THE FIXTURE MAY DIFFER FROM THE CANON, AND
/// EVERY DIFFERENCE IS SAID OUT LOUD — a difference nobody mentions turns a
/// measurement into an argument (boss, 2026-09-03).

#ifndef TESTS_RUN_COMMON_FIXTURE_POLICY_H_
#define TESTS_RUN_COMMON_FIXTURE_POLICY_H_

#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_catalog/definitions.h"
#include "core_common/herd_state.h"
#include "core_common/land_state.h"
#include "core_common/ledger_state.h"
#include "core_common/order_state.h"
#include "core_common/plot.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/stub_tables.h"
#include "core_tables/tables.h"
#include "core_world/world.h"
#include "crop_roof.h"
#include "start_gate.h"
#include "store_parent.h"
#include "suggested_place.h"
#include "village_middle.h"

namespace run {

/// @brief Builds granaries and cattle yards — one more of either whenever
/// last year showed it was short — then says how many it built and why.
///
/// TWO BUILDINGS, ONE REASON. The canon hands the village a church to keep
/// its grain in and no roof for its animals; both are the chairman's to put
/// up, and a run has no chairman. The granary was added when the harvest
/// began rotting in the field for want of anywhere to go; the cattle yard
/// when the thirty-year run ended with ZERO kolkhoz animals and 4949 in
/// private yards — not starved (half a percent of hungry head-days) but
/// pushed out by the canon's own rule that stock above the roof goes to the
/// yards rather than under the knife.
class FixturePolicy {
 public:
  explicit FixturePolicy(const core::ITableSet& tables)
      : roof_(ReadCropRoof(tables)),
        yard_suggested_(tables, "food_yard"),
        clamp_suggested_(tables, "clamp"),
        store_suggested_(tables, roof_.store_key),
        rick_suggested_(tables, "hay_stack") {
    rick_ = TypeByKey(tables, "hay_stack");
    granary_ = TypeByKey(tables, "granary");
    cattle_ = TypeByKey(tables, "cattle_yard");
    pen_suggestion_ = SuggestedPoint(tables, "cattle_yard", has_pen_suggestion_);
    ReadCattleRoof(tables);
    ReadEntryDay(tables);
    granary_yard_ = ParentTypeOf(tables, "granary");
    food_store_ = roof_.store;
    store_on_yard_ = granary_yard_.value != core::kInvalidDefIdValue &&
                     ParentTypeOf(tables, roof_.store_key).value == granary_yard_.value;
    clamp_ = TypeByKey(tables, "clamp");
    church_ = TypeByKey(tables, "church_store");
    std::string error;
    core::LoadDefinitions(tables, core::StubTables::kAllowed, definitions_, error);
  }

  /// @brief The question asked before every start (start_gate.h).
  void SetStartGate(StartGate gate) { start_gate_ = std::move(gate); }

  /// @brief One day of the chairman's attention. Call once a day.
  void RunDay(core::ISimulation& simulation) {
    PauseSitesForThePen(simulation);
    CountWaitStreaks(simulation.CompletedState());
    NoteStoreDays(simulation.CompletedState());
    if (cooldown_ > 0) {
      --cooldown_;
      return;
    }
    const core::WorldState& world = simulation.CompletedState();
    // A MARK THAT LEFT NO ROW WAS REFUSED — crowding, most often. Counted per
    // type so a run can say whether its buildings wait on materials or on room.
    if (marked_type_.value != core::kInvalidDefIdValue) {
      if (Rows(world, marked_type_) <= rows_before_mark_) {
        ++(marked_type_.value == cattle_.value ? cattle_refused_ : other_refused_);
        // The elder's point refused once is refused for good: the next pen
        // goes to the nearest free place, as every other roof does.
        pen_suggestion_refused_ = pen_suggestion_refused_ || marked_on_suggestion_;
      }
      marked_type_ = core::UnitTypeId{};
      marked_on_suggestion_ = false;
    }
    // THE WARM BARN, THE DAY ITS RECIPE IS IN THE VILLAGE (Livestock design,
    // the cattle yard's ladder: rung 1 is the open pen, a COLD place; rung 2
    // the wattle-and-clay barn, a warm one — «к зиме утеплить»). The core
    // refuses a step whose materials are short (MaterialsShortFor), so the
    // chairman asks it first rather than spending an order a day on a no.
    const std::uint32_t pen = PenToWarm(world);
    warm_barn_in_hand_ =
        pen != core::kNoRow && simulation.MaterialsShortFor(world.units.row_ids[pen]).empty() &&
        GateOpen(start_gate_, world, cattle_, static_cast<std::uint8_t>(kWarmBarnLevel));
    // AND THE LONG STORE'S NEXT LEVEL the same way (#756).
    const std::uint32_t store = StoreToRaise(world);
    store_raise_in_hand_ = store != core::kNoRow &&
                           simulation.MaterialsShortFor(world.units.row_ids[store]).empty() &&
                           GateOpen(start_gate_,
                                    world,
                                    food_store_,
                                    static_cast<std::uint8_t>(world.units.rows[store].level + 1U));
    core::OrderRow order;
    if (!NextOrder(world, order)) {
      return;
    }
    if (order.kind == core::OrderKind::kBuildUnit) {
      marked_type_ = order.unit_type;
      rows_before_mark_ = Rows(world, order.unit_type);
    }
    const std::uint32_t raised = order.kind == core::OrderKind::kUpgradeUnit
                                     ? core::FindRow(world.units, order.unit)
                                     : core::kNoRow;
    const bool store_raised =
        raised != core::kNoRow && world.units.rows[raised].type.value == food_store_.value;
    stores_raised_ += store_raised ? 1U : 0U;
    warms_ordered_ += order.kind == core::OrderKind::kUpgradeUnit && !store_raised ? 1U : 0U;
    simulation.StageOrders(std::span<const core::OrderRow>(&order, 1), {});
    cooldown_ = kCooldownDays;
  }

  /// @brief The first cattle yard's pen waiting for its warm while cows stand
  ///        on the farm: PenToWarm when a kolkhoz head of a cattle-yard kind
  ///        stands, else kNoRow. The felling and the digging keep its warm's
  ///        logs and clay ahead (BuildingChairman wires it as a rise watch).
  std::uint32_t PenWaitingForWarm(const core::WorldState& world) const {
    bool cows = false;
    for (const core::HerdRow& herd : world.herds.rows) {
      cows = cows || (herd.household_owned == 0 && herd.kind.value < cattle_home_.size() &&
                      cattle_home_[herd.kind.value] != 0 &&
                      herd.adult_count + herd.juvenile_count + herd.newborn_count > 0);
    }
    return cows ? PenToWarm(world) : core::kNoRow;
  }

  /// @brief Whether a site of the farm's own kinds (granary, granary yard,
  ///        cattle yard, food store, clamp) stands marked or going up — the
  ///        set NextOrder waits on before it marks the next («nothing new
  ///        until it stands»). A site that starves holds the farm's whole
  ///        queue, the first pen included (BarrackPolicy asks this).
  bool FarmSiteOpen(const core::WorldState& world) const {
    for (const core::UnitRow& unit : world.units.rows) {
      const bool ours = unit.type.value == granary_.value || unit.type.value == cattle_.value ||
                        unit.type.value == granary_yard_.value ||
                        unit.type.value == food_store_.value || unit.type.value == clamp_.value;
      if (ours && unit.level == 0 && unit.dead == 0) {
        return true;
      }
    }
    return false;
  }

  /// @brief THE FIRST PEN'S WARM BEFORE THE WINTER (econ, males-work-pair-
  ///        ruling-2026-10-10 §2, §4; the warm pair's second form): from
  ///        August to November, while the FIRST pen's warm is under works,
  ///        every other open construction site is paused (kPauseUnit) so the
  ///        builders go to the pen; they are resumed when the pen stands warm
  ///        or the window closes.
  /// WHY HANDS AND NOT MATERIALS: seed 1931's warm was ordered on day 31 with
  /// its whole recipe on the site that day, and its works stood open to day
  /// 57 beside two to five other sites — seven cows froze from day 49 (the
  /// pen probe, the thread core-boss-c2-site-supply-2026-10-09 [97]). With
  /// the pause: nought frozen in nine villages, 1931's warm on day 43.
  void PauseSitesForThePen(core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    const std::uint32_t month =
        (world.calendar.day % core::kDaysPerYear) / core::kDaysPerMonth + 1U;
    std::uint32_t pen = core::kNoRow;
    bool warm_stands = false;
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      if (unit.type.value != cattle_.value) {
        continue;
      }
      warm_stands = warm_stands || unit.level >= kWarmBarnLevel;
      if (pen == core::kNoRow && unit.level == kWarmBarnLevel - 1 &&
          unit.construction.phase != core::ConstructionPhase::kNone &&
          unit.construction.target_level == kWarmBarnLevel) {
        pen = row;
      }
    }
    const bool hold = pen != core::kNoRow && !warm_stands && month >= kPenWindowFirstMonth &&
                      month <= kPenWindowLastMonth;
    std::vector<core::OrderRow> orders;
    if (hold) {
      for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
        const core::UnitRow& unit = world.units.rows[row];
        const bool works = unit.construction.phase == core::ConstructionPhase::kDelivering ||
                           unit.construction.phase == core::ConstructionPhase::kBuilding;
        if (row == pen || !works || unit.paused != 0) {
          continue;
        }
        core::OrderRow order;
        order.kind = core::OrderKind::kPauseUnit;
        order.unit = world.units.row_ids[row];
        orders.push_back(order);
        paused_for_pen_.push_back(order.unit);
      }
    } else if (!paused_for_pen_.empty()) {
      for (const core::UnitId unit : paused_for_pen_) {
        core::OrderRow order;
        order.kind = core::OrderKind::kResumeUnit;
        order.unit = unit;
        orders.push_back(order);
      }
      paused_for_pen_.clear();
    }
    if (!orders.empty()) {
      simulation.StageOrders(std::span<const core::OrderRow>(orders.data(), orders.size()), {});
    }
  }

  /// @brief Whether today the farm's own building must go before any house
  /// (boss, parcel 298: the chairman builds what the farm lacks MOST). True
  /// while a site of a store for the harvest is marked and short of its recipe
  /// and the harvest has nowhere to go, or a cattle-yard site is and animals
  /// stand without a roof. A site already started holds nothing back.
  /// AND FROM AUGUST TO NOVEMBER WHILE THE FIRST PEN WITH COWS IS NOT WARM
  /// (the cattle yard's winter warm; econ, males-work-pair-ruling-2026-10-10
  /// §2 and §4): the pen's warm recipe is taken by no other site first.
  bool HoldsHousesBack(const core::ISimulation& simulation) const {
    const core::WorldState& world = simulation.CompletedState();
    const std::uint32_t month =
        (world.calendar.day % core::kDaysPerYear) / core::kDaysPerMonth + 1U;
    if (month >= kPenWindowFirstMonth && month <= kPenWindowLastMonth &&
        PenWaitingForWarm(world) != core::kNoRow) {
      return true;
    }
    const bool room_short = RoomWasShort(world);
    const bool roof_short = RoofWasShort(world);
    if (!room_short && !roof_short) {
      return false;
    }
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      if (unit.level != 0 || unit.construction.phase != core::ConstructionPhase::kMarked) {
        continue;
      }
      const bool store =
          unit.type.value == granary_.value || unit.type.value == granary_yard_.value;
      const bool roof = unit.type.value == cattle_.value;
      if (((store && room_short) || (roof && roof_short)) &&
          !simulation.MaterialsShortFor(world.units.row_ids[row]).empty()) {
        return true;
      }
    }
    return false;
  }

  /// @brief Whether a reaped load has lain in a field with no carrying demand
  /// against it for MORE THAN kRoomWaitDays days running: the doors of the
  /// stores are shut (the first of RoomWasShort's signals). Reads the streaks
  /// RunDay counts, so it answers for the last day RunDay saw.
  ///
  /// A STREAK AND NOT A DAY (boss, parcel 300). A load waits a day for its
  /// carrying demand to be written after every reaping, and counting that day
  /// made the signal burn in 21 to 30 years of 30 on every arm — with all
  /// fourteen granaries standing too — so "the farm first" became "the
  /// granary always first" (parcel 299).
  bool HarvestWaitsForRoom() const {
    for (const std::uint32_t streak : wait_streak_) {
      if (streak > kRoomWaitDays) {
        return true;
      }
    }
    return false;
  }

  /// @brief The fixture difference, in words, for the run to print BEFORE it
  /// measures anything.
  static void Declare() {
    std::cout << "thirty_years: FIXTURE DIFFERS FROM THE START CANON — the run's chairman "
                 "builds GRANARIES and CATTLE YARDS, because the canon gives neither and a "
                 "run has nobody to decide on them (boss, 2026-09-03); and a CLAMP by the "
                 "fields before the first reaping, with the LONG STORE of the clamp's crops - "
                 "the vegetable store, raised to its second level before another is staked - "
                 "under this year's potatoes and vegetables (boss, parcel 419; #756)\n";
  }

  /// @brief What the fixture actually did, for the run to print at the end.
  void Report(const core::WorldState& world) const {
    std::cout << "thirty_years: the run's chairman ordered " << ordered_ << " fixture buildings; "
              << Built(world, granary_) << " granaries, " << Built(world, food_store_)
              << " long stores of the clamp's crops ("
              << (roof_.store_key.empty() ? std::string("NO SUCH TYPE IN THE TABLES")
                                          : roof_.store_key)
              << ", " << stores_raised_ << " upgrade(s) ordered) and " << Built(world, cattle_)
              << " cattle yards stand; the stores stand as modules of "
              << Built(world, granary_yard_) << " food yards (" << yards_ordered_
              << " yard orders); marks refused: cattle yards " << cattle_refused_
              << ", granaries and yards " << other_refused_ << "\n";
    std::cout << "thirty_years: the pen on the elder's point: "
              << (has_pen_suggestion_ ? "" : "NO suggest row for cattle_yard, ")
              << pens_on_suggestion_ << " mark(s) there"
              << (pen_suggestion_refused_ ? ", REFUSED" : "") << "; warm-barn upgrades ordered "
              << warms_ordered_ << "\n";
    std::cout << "thirty_years: " << Built(world, clamp_) << " clamp(s) stand, the first up on day "
              << (clamp_days_.empty() ? std::string("never") : std::to_string(clamp_days_.front()))
              << "\n";
    std::cout << "thirty_years: the horses were stabled "
              << (stable_seen_ ? "on day " + std::to_string(stable_day_) : std::string("never"))
              << "; long stores of " << StoreRoomAt(1) / core::kGramsPerTonne
              << " t at the first level stood up on days";
    for (const core::SimDay day : food_store_days_) {
      std::cout << ' ' << day << " (year " << day / core::kDaysPerYear + 1 << ')';
    }
    std::cout << (food_store_days_.empty() ? " none" : "") << "\n";
    // THE ROOF AS READ (#756): a set of nought is the defect of 0.37.217-239,
    // when the rules below it could not fire and nothing said so.
    std::cout << "thirty_years: the roof of potatoes and vegetables — " << roof_.crops
              << " crop(s) the clamp keeps"
              << (roof_.crops == 0 ? " — NONE: THE CLAMP'S AND THE STORE'S RULES CANNOT FIRE" : "")
              << "; the clamp is UNBOUNDED in the core (no contour: econ's sizing rule has no "
                 "door, #759), so a roof that exists never binds\n";
    yard_suggested_.Report("thirty_years");
    clamp_suggested_.Report("thirty_years");
    store_suggested_.Report("thirty_years");
    std::cout << "thirty_years: the second rick was "
              << (rick_marked_day_ == kNoRickDay
                      ? std::string("NOT MARKED (no free point of hay_stack in suggestions.csv, "
                                    "or none before September of year 1)")
                      : "marked on day " + std::to_string(rick_marked_day_))
              << ", stood up "
              << (rick_days_.empty() ? std::string("never")
                                     : "on day " + std::to_string(rick_days_.front()))
              << "\n";
    rick_suggested_.Report("thirty_years");
  }

  /// @brief THE GUARD OF #756: the stores the layout suggests that have no
  ///        row in `world` (crop_roof.h, SuggestedStoresNeverMarked), for the
  ///        run to assert on at a year's end.
  static std::vector<std::string> SuggestedStoresNeverMarked(const core::ITableSet& tables,
                                                             const core::WorldState& world) {
    return run::SuggestedStoresNeverMarked(tables, world);
  }

  /// @brief How many crop rows the roof was read for: 0 means the clamp's and
  ///        the store's rules cannot fire.
  std::uint32_t RoofedCrops() const { return roof_.crops; }

  /// @brief Whether `key` is a store THIS policy builds: the clamp or the
  ///        long store of its crops. The guard is hard on these; a suggested
  ///        store of another policy's is that policy's to answer for.
  bool BuildsStore(std::string_view key) const {
    return key == "clamp" || (!roof_.store_key.empty() && key == roof_.store_key);
  }

 private:
  static constexpr std::uint32_t kCooldownDays = 4;

  void NoteStoreDays(const core::WorldState& world) {
    if (!stable_seen_ && world.chairman.horses_stabled != 0) {
      stable_seen_ = true;
      stable_day_ = world.calendar.day;
    }
    const std::uint32_t standing = Built(world, food_store_);
    while (food_store_days_.size() < standing) {
      food_store_days_.push_back(world.calendar.day);
    }
    const std::uint32_t clamps = Built(world, clamp_);
    while (clamp_days_.size() < clamps) {
      clamp_days_.push_back(world.calendar.day);
    }
    // The ricks beyond the start's own (one, by the old village).
    const std::uint32_t ricks = Built(world, rick_);
    while (rick_days_.size() + 1U < ricks) {
      rick_days_.push_back(world.calendar.day);
    }
  }

  /// Days a reaped load may lie without carrying demand before it means "no
  /// room" rather than the carrying lag after a reaping (boss, parcel 300).
  static constexpr std::uint32_t kRoomWaitDays = 3;

  std::vector<std::uint32_t> wait_streak_;

  StartGate start_gate_;

  static constexpr float kStepAside = 60.0F;

  static core::UnitTypeId TypeByKey(const core::ITableSet& tables, std::string_view key) {
    const core::ITable* types = tables.FindTable("unit_types");
    const std::uint32_t row = types == nullptr ? core::kNoTableRow : types->FindRowByKey(key);
    return row == core::kNoTableRow ? core::UnitTypeId{}
                                    : core::UnitTypeId{static_cast<std::uint16_t>(row)};
  }

  static std::uint32_t Rows(const core::WorldState& world, core::UnitTypeId type) {
    std::uint32_t rows = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      rows += unit.type.value == type.value ? 1U : 0U;
    }
    return rows;
  }

  static std::uint32_t Built(const core::WorldState& world, core::UnitTypeId type) {
    std::uint32_t built = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      built += unit.type.value == type.value && unit.level > 0 ? 1U : 0U;
    }
    return built;
  }

  /// @brief Whether some kolkhoz animals stand without a roof — on billet.
  /// The billeted count is the herd system's own answer to that question, so
  /// the fixture asks it rather than guessing at capacities.
  ///
  /// IT READ THE OTHER WAY ROUND FROM 23cf783 (2026-09-04) TO 0.37.62: «short»
  /// was `heads > billeted_count`, true for every herd with ONE head under a
  /// roof and false for a herd wholly on billet — the question upside down.
  /// With the start's cattle yard standing it was true every day, so the
  /// chairman raised cattle yards to kMaxOfEach whatever the herd needed (the
  /// second yard by year 2 on 27 seeds of 27, herd_system.cpp BilletHerds);
  /// with the start's yard gone it would have been false for the one herd
  /// that had no roof at all. Found reading it for the pen of 0.37.62.
  ///
  /// AND NOT «ANY HEAD ON BILLET» EITHER, the first repair's form: the
  /// horses stand billeted until their own yard — no cattle yard answers
  /// them — and since 0.37.62 a frost month keeps cows on the billet's places
  /// while the pen has room. Either would have raised cattle yards to
  /// kMaxOfEach again. The question is the cattle yard's own: more heads
  /// whose home it is than places in every cattle yard standing.
  bool RoofWasShort(const core::WorldState& world) const {
    float heads = 0.0F;
    for (const core::HerdRow& herd : world.herds.rows) {
      if (herd.household_owned == 0 && herd.kind.value < cattle_home_.size() &&
          cattle_home_[herd.kind.value] != 0) {
        heads += static_cast<float>(herd.newborn_count + herd.juvenile_count + herd.adult_count);
      }
    }
    float places = 0.0F;
    for (const core::UnitRow& unit : world.units.rows) {
      const std::size_t index = static_cast<std::size_t>(unit.level) - 1U;
      if (unit.type.value == cattle_.value && unit.level > 0 && index < cattle_places_.size()) {
        places += cattle_places_[index];
      }
    }
    return heads > places;
  }

  /// world_params `player_entry_day` (12, 1 April), the day the pen may be
  /// marked from.
  void ReadEntryDay(const core::ITableSet& tables) {
    const core::ITable* params = tables.FindTable("world_params");
    const std::uint32_t row =
        params == nullptr ? core::kNoTableRow : params->FindRowByKey("player_entry_day");
    const std::uint32_t column =
        params == nullptr ? core::kNoTableColumn : params->FindColumn("value");
    if (row == core::kNoTableRow || column == core::kNoTableColumn) {
      return;
    }
    const std::optional<float> day = params->CellReal(row, column);
    entry_day_ = day && *day >= 0.0F ? static_cast<core::SimDay>(*day) : entry_day_;
  }

  /// livestock.csv's kinds whose `home_unit` is the cattle yard, and the
  /// cattle yard's places by level (unit_levels.csv livestock_capacity_head).
  void ReadCattleRoof(const core::ITableSet& tables) {
    const core::ITable* livestock = tables.FindTable("livestock");
    const std::uint32_t home =
        livestock == nullptr ? core::kNoTableColumn : livestock->FindColumn("home_unit");
    for (std::uint32_t row = 0; home != core::kNoTableColumn && row < livestock->RowCount();
         ++row) {
      cattle_home_.push_back(livestock->CellText(row, home) == "cattle_yard" ? 1U : 0U);
    }
    const core::ITable* levels = tables.FindTable("unit_levels");
    if (levels == nullptr) {
      return;
    }
    const std::uint32_t unit_column = levels->FindColumn("unit");
    const std::uint32_t level_column = levels->FindColumn("level");
    const std::uint32_t heads_column = levels->FindColumn("livestock_capacity_head");
    for (std::uint32_t row = 0; row < levels->RowCount(); ++row) {
      if (unit_column == core::kNoTableColumn ||
          levels->CellText(row, unit_column) != "cattle_yard") {
        continue;
      }
      const std::optional<float> level = levels->CellReal(row, level_column);
      const std::optional<float> heads = levels->CellReal(row, heads_column);
      if (!level || !(*level >= 1.0F)) {
        continue;
      }
      const auto index = static_cast<std::size_t>(*level) - 1U;
      if (cattle_places_.size() <= index) {
        cattle_places_.resize(index + 1U, 0.0F);
      }
      cattle_places_[index] = heads ? *heads : 0.0F;
    }
  }

  /// @brief Whether any kolkhoz herd has heads on billet today.
  static bool KolkhozHeadsBilleted(const core::WorldState& world) {
    for (const core::HerdRow& herd : world.herds.rows) {
      if (herd.household_owned == 0 && herd.billeted_count > 0) {
        return true;
      }
    }
    return false;
  }

  /// @brief The cattle yard standing at the open pen's rung with nothing
  /// going on it, while no cattle yard stands warm — its row, or kNoRow. A
  /// pen insulated with straw is warm already and asks for nothing.
  std::uint32_t PenToWarm(const core::WorldState& world) const {
    if (cattle_.value == core::kInvalidDefIdValue) {
      return core::kNoRow;
    }
    std::uint32_t pen = core::kNoRow;
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      if (unit.type.value != cattle_.value) {
        continue;
      }
      if (unit.level >= kWarmBarnLevel || unit.insulated != 0) {
        return core::kNoRow;
      }
      if (pen == core::kNoRow && unit.level == kWarmBarnLevel - 1 &&
          unit.construction.phase == core::ConstructionPhase::kNone) {
        pen = row;
      }
    }
    return pen;
  }

  /// @brief The pen's warm (kUpgradeUnit to level 2) when its recipe is in
  ///        hand (warm_barn_in_hand_) and the month is a clay one; false
  ///        otherwise, `order` untouched.
  bool WarmFirst(const core::WorldState& world, core::OrderRow& order) const {
    const auto month_now = static_cast<std::uint32_t>(world.calendar.date.month);
    const bool clay_season = month_now >= kClayWarmFirstMonth && month_now <= kClayWarmLastMonth;
    if (!warm_barn_in_hand_ || !clay_season) {
      return false;
    }
    const std::uint32_t pen = PenToWarm(world);
    if (pen == core::kNoRow) {
      return false;
    }
    order.kind = core::OrderKind::kUpgradeUnit;
    order.unit = world.units.row_ids[pen];
    return true;
  }

  /// @brief Marks the first cattle yard: on the elder's point while it has
  /// not been refused, else at the nearest free place (Mark).
  bool MarkPen(const core::WorldState& world, core::OrderRow& order) {
    if (!has_pen_suggestion_ || pen_suggestion_refused_) {
      return Mark(world, cattle_, order);
    }
    order.kind = core::OrderKind::kBuildUnit;
    order.unit_type = cattle_;
    order.position = pen_suggestion_;
    marked_on_suggestion_ = true;
    ++pens_on_suggestion_;
    ++ordered_;
    return true;
  }

  /// @brief suggestions.csv's point for `unit_type`, the first row that names
  /// it (map-db kind suggestion: metres from the map's south-west corner, the
  /// frame of start_layout.csv and of the units' positions).
  static core::Vec2 SuggestedPoint(const core::ITableSet& tables,
                                   std::string_view unit_type,
                                   bool& found) {
    found = false;
    const core::ITable* table = tables.FindTable("suggestions");
    if (table == nullptr) {
      return core::Vec2{};
    }
    const std::uint32_t type_column = table->FindColumn("unit_type");
    const std::uint32_t x_column = table->FindColumn("x_m");
    const std::uint32_t y_column = table->FindColumn("y_m");
    if (type_column == core::kNoTableColumn || x_column == core::kNoTableColumn ||
        y_column == core::kNoTableColumn) {
      return core::Vec2{};
    }
    for (std::uint32_t row = 0; row < table->RowCount(); ++row) {
      if (table->CellText(row, type_column) != unit_type) {
        continue;
      }
      const std::optional<float> x = table->CellReal(row, x_column);
      const std::optional<float> y = table->CellReal(row, y_column);
      if (x && y) {
        found = true;
        return core::Vec2{.x = *x, .y = *y};
      }
    }
    return core::Vec2{};
  }

  /// @brief Whether the village is short of somewhere to put its harvest.
  ///
  /// TWO SIGNALS, and the first one is the one that matters. Grain LYING IN
  /// THE FIELD is what a chairman actually sees, and he sees it the same
  /// week: since task A4 a loaded field can start nothing — you do not
  /// plough grain into the ground you grew it on — so a load that waits
  /// costs the next sowing, not just the grain. The ledger's `lost_no_room` is
  /// the second signal and a late one: it is only booked when the snow takes
  /// what was still out, by which time the year is lost. Watching only the
  /// late signal is what left the run's chairman a year behind his village.
  /// @brief One day's streaks: a field whose reaped load has no carrying
  /// demand adds a day, any other field starts again from zero. Indexed by
  /// field row; a field removed mid-run shifts the rows once, which costs at
  /// most one streak restarted.
  void CountWaitStreaks(const core::WorldState& world) {
    wait_streak_.resize(world.fields.rows.size(), 0);
    for (std::size_t row = 0; row < world.fields.rows.size(); ++row) {
      const core::FieldRow& field = world.fields.rows[row];
      const bool waits = field.reaped_grams > 0 && field.haul_days_remaining <= 0.0F;
      wait_streak_[row] = waits ? wait_streak_[row] + 1 : 0;
    }
  }

  bool RoomWasShort(const core::WorldState& world) const {
    // A LOAD BEING CARRIED IS NOT A SHORTAGE. What says "nowhere to put it" is
    // a load with NO CARRYING DEMAND against it: production sizes that demand
    // by the room in the stores, so a zero demand under a standing load means
    // the doors are shut. The first version of this read any load at all as a
    // shortage, and since carrying takes days that was nearly always true —
    // the chairman ordered thirty-nine buildings in thirty years and took the
    // hands to raise them off the fields.
    //
    // AND ONLY FOR WHAT A GRANARY TAKES (2026-09-15). Since stores take only
    // their homes, potatoes and vegetables lost for want of a roof lit this
    // signal every year; the chairman answered with granaries that do not
    // take them — eight on seed 1933 — and "the farm first" held every house
    // back while families left for want of one. The roof for those two is
    // the food store's rule below (RoofedHarvestGrams), not this one.
    for (std::size_t row = 0; row < world.fields.rows.size() && row < wait_streak_.size(); ++row) {
      if (wait_streak_[row] > kRoomWaitDays && !Roofed(world.fields.rows[row].reaped_resource)) {
        return true;
      }
    }
    for (std::size_t index = 0; index < world.ledger.closed.lost_no_room.size(); ++index) {
      if (world.ledger.closed.lost_no_room[index] > 0 &&
          !Roofed(core::ResourceId{static_cast<std::uint16_t>(index)})) {
        return true;
      }
    }
    return false;
  }

  /// Whether the food store (not the granary) keeps this resource.
  bool Roofed(core::ResourceId resource) const {
    return resource.value < roof_.resource.size() && roof_.resource[resource.value] != 0;
  }

  bool NextOrder(const core::WorldState& world, core::OrderRow& order) {
    // THE FIRST PEN BESIDE THE FARM'S OWN SITE, NOT BEHIND IT (boss, 11 October
    // 2026, #517 (a)): while the kolkhoz cows stand billeted and no cattle yard
    // stands, the first pen is marked and started whatever other site of the
    // farm is open. Behind the granary it was marked on day 48 instead of 24
    // in seed 1931 and its straw coat stood on day 50-52 in five villages of
    // nine - eleven cows froze in those five and none elsewhere (0.37.222's
    // pair, core-warmprobe WI). Its rung is 0.86 man-days. The bot's rule
    // only: the player marks what he marks.
    const bool first_pen = cattle_.value != core::kInvalidDefIdValue &&
                           Built(world, cattle_) == 0 && world.calendar.day >= entry_day_ &&
                           KolkhozHeadsBilleted(world);
    if (first_pen && Rows(world, cattle_) == 0) {
      last_ordered_cattle_ = true;
      return MarkPen(world, order);
    }
    for (std::uint32_t row = 0; first_pen && row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      if (unit.type.value == cattle_.value && unit.level == 0 &&
          unit.construction.phase == core::ConstructionPhase::kMarked &&
          GateOpen(start_gate_, world, unit.type, unit.construction.target_level)) {
        order.kind = core::OrderKind::kStartBuild;
        order.unit = world.units.row_ids[row];
        return true;
      }
    }
    // THE SECOND RICK ON THE ELDER'S POINT (rick_suggested_ has the reason):
    // marked once and started, from the entry morning and before September of
    // year 1 - BESIDE the farm's other sites, not behind them: an outline of
    // eight earthwork days with no recipe to wait for. Its first form stood
    // after the clamp's branch and named no start: the queue below returns
    // while any site of the farm goes up, so the mark was rarely reached, and
    // a rick is not one of «ours» there, so a marked one was never started -
    // the second rick stood in no village of nine (form (c)'s first pair).
    if (rick_.value != core::kInvalidDefIdValue && world.calendar.day >= entry_day_ &&
        world.calendar.day < kSecondRickBeforeDay) {
      for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
        const core::UnitRow& unit = world.units.rows[row];
        if (unit.type.value == rick_.value && unit.level == 0 &&
            unit.construction.phase == core::ConstructionPhase::kMarked &&
            GateOpen(start_gate_, world, unit.type, unit.construction.target_level)) {
          order.kind = core::OrderKind::kStartBuild;
          order.unit = world.units.row_ids[row];
          return true;
        }
      }
      if (!rick_settled_) {
        rick_settled_ = true;
        const std::vector<float>& radii = definitions_.units.keep_out_radius_m;
        const std::optional<core::Vec2> point = rick_suggested_.Take(
            world, definitions_.Plots(), rick_.value < radii.size() ? radii[rick_.value] : 0.0F);
        if (point) {
          order.kind = core::OrderKind::kBuildUnit;
          order.unit_type = rick_;
          order.position = *point;
          rick_marked_day_ = world.calendar.day;
          ++ordered_;
          return true;
        }
      }
    }
    // THE FIRST PEN'S WARM AHEAD OF THE FARM'S SITES (MR.2's «L2 in the first
    // wave»; boss, the #1056 thread [121]): asked before the wait below. Behind
    // it, a granary or clamp going up held the warm - in 1934 and 1938 of
    // MR.2's pair through the whole clay window with its recipe in hand - and
    // with warm_hold.h holding that site's start for the warm's straw, the two
    // waited on each other.
    if (WarmFirst(world, order)) {
      return true;
    }
    // A site of any of its kinds already going up: nothing new until it stands
    // - the first pen's site aside (above).
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      const bool ours = unit.type.value == granary_.value || unit.type.value == cattle_.value ||
                        unit.type.value == granary_yard_.value ||
                        unit.type.value == food_store_.value || unit.type.value == clamp_.value;
      if (!ours || unit.level != 0 || (first_pen && unit.type.value == cattle_.value)) {
        continue;
      }
      if (unit.construction.labor_days_remaining > 0.0F) {
        return false;
      }
      if (unit.construction.phase == core::ConstructionPhase::kMarked &&
          !GateOpen(start_gate_, world, unit.type, unit.construction.target_level)) {
        return false;  // the boards are the sawmill's until it stands
      }
      order.kind = core::OrderKind::kStartBuild;
      order.unit = world.units.row_ids[row];
      return true;
    }
    // THE PEN IN THE FIRST DAYS, ON THE ELDER'S POINT (the human's word of
    // 2026-09-30 through boss-core-start-no-yards: the start has no cattle
    // yard; «a level-1 cattle yard is a cheap open pen», marked in the first
    // days on suggest_cattle_yard). Until 0.37.62 the first cattle yard waited
    // behind the clamp and the food stores and went to the nearest free place:
    // the start had one, so nobody had asked when the first one comes.
    //
    // THE FIRST DAYS ARE THE PLAYER'S, from the entry morning (start
    // conditions §9: the campaign opens on 1 January, the chairman arrives on
    // `player_entry_day`). A pen marked in January would stand the herd in the
    // cold through the winter no chairman was there for — the frost's toll of
    // a canon, not of a game.
    if (cattle_.value != core::kInvalidDefIdValue && Rows(world, cattle_) == 0 &&
        world.calendar.day >= entry_day_ && KolkhozHeadsBilleted(world)) {
      last_ordered_cattle_ = true;
      return MarkPen(world, order);
    }
    // NO CLAY WARM OPENED FROM OCTOBER (boss, 11 October 2026, form (a1);
    // livestock design «Не успел»): a warm begun in October could not stand
    // by 1 December under the carted supply — 0.37.221's pen reached the
    // winter 0.09 man-days short and stood stopped till March while its cows
    // froze. From October to the end of winter the pen takes the straw coat
    // instead (insulation_policy.h), and the clay warm opens in spring. STUB:
    // «cannot be done by 1 December» is read as «not begun by 1 October».
    // The order itself is asked above, ahead of the farm's sites (WarmFirst).
    const bool wants_granary =
        Wants(world, granary_, Built(world, granary_) == 0 || RoomWasShort(world));
    // From the entry morning, as the pen above: before it no cattle yard is
    // the chairman's, the first one's place included.
    const bool wants_cattle =
        world.calendar.day >= entry_day_ &&
        Wants(world, cattle_, Built(world, cattle_) == 0 || RoofWasShort(world));
    // A ROOF FOR THIS YEAR'S POTATOES AND VEGETABLES (boss, parcels 401 and
    // 408). Since the stores take only their homes, these two go into the
    // church and a food store and nowhere else, and what finds no roof is lost
    // to the snow. The chairman sees this year's crops in the fields' chains,
    // so he puts up food stores until their room and the church's hold this
    // year's harvest — THE CEILING, and no more than it — taking turns with
    // the granary and the cattle yard when those are wanted too.
    //
    // THE CLAMP FIRST, BEFORE THE FIRST REAPING (boss, parcel 419). Until
    // 2026-09-15 no home of potatoes and vegetables stood by the first harvest
    // on any of seeds 1929, 1933 and 1936 — the church was full of the start's
    // grain and the first food store rose in year 2 or 3 — and the snow took
    // 493, 734 and 652 t of them in four years. The clamp is an outline by the
    // fields, so one takes the whole crop; the food store stays the long home,
    // put up when its materials are there. It no longer waits for the stable:
    // that gate was measured on thirty years with no clamp at all.
    if (clamp_.value != core::kInvalidDefIdValue && Rows(world, clamp_) == 0 &&
        RoofedHarvestGrams(world) > 0) {
      return MarkClamp(world, order);
    }
    // THE LONG STORE (crop_roof.h: the vegetable store since 0.37.217), until
    // its room and the church's hold this year's harvest: the store standing
    // is raised to its next level before another is staked (resources design
    // §4, «150 т, затем 300 т»), and nothing more of the kind while one is
    // being raised.
    // A store that can be raised is raised when its recipe is in the village
    // and WAITED FOR when it is not: a second plot would wait for the same
    // boards.
    const std::uint32_t raise = StoreToRaise(world);
    const bool wants_food_store =
        food_store_.value != core::kInvalidDefIdValue &&
        RoofedHarvestGrams(world) > RoofedRoomGrams(world) && !StoreBeingRaised(world) &&
        (raise != core::kNoRow ? store_raise_in_hand_ : Rows(world, food_store_) < kMaxOfEach);
    if (wants_food_store && !(last_ordered_food_store_ && (wants_granary || wants_cattle))) {
      last_ordered_food_store_ = true;
      if (raise != core::kNoRow) {
        order.kind = core::OrderKind::kUpgradeUnit;
        order.unit = world.units.row_ids[raise];
        return true;
      }
      return Mark(world, food_store_, order);
    }
    last_ordered_food_store_ = false;
    // ALTERNATE when both are wanted. Grain used to come first always, and
    // "always" turned out to mean "only": since a loaded field is a standing
    // signal, the granary branch won every single time and the herds never
    // got a roof — the run ended with zero kolkhoz animals while seven
    // granaries stood. A chairman with two needs attends to both.
    if (wants_granary && wants_cattle) {
      const bool granary_turn = last_ordered_cattle_;
      last_ordered_cattle_ = !granary_turn;
      return Mark(world, granary_turn ? granary_ : cattle_, order);
    }
    if (wants_granary) {
      last_ordered_cattle_ = false;
      return Mark(world, granary_, order);
    }
    if (wants_cattle) {
      last_ordered_cattle_ = true;
      return Mark(world, cattle_, order);
    }
    return false;
  }

  bool Wants(const core::WorldState& world, core::UnitTypeId type, bool short_of_it) const {
    return type.value != core::kInvalidDefIdValue && short_of_it && Built(world, type) < kMaxOfEach;
  }

  bool Mark(const core::WorldState& world, core::UnitTypeId type, core::OrderRow& order) {
    order.kind = core::OrderKind::kBuildUnit;
    order.unit_type = type;
    // A module of the food yard goes on the yard: the granary, and the long
    // store when the tables make it one (the food store did; the vegetable
    // store stands by itself - resources design §4, «не модуль продуктового
    // двора»).
    if ((type.value == granary_.value || (type.value == food_store_.value && store_on_yard_)) &&
        granary_yard_.value != core::kInvalidDefIdValue) {
      return MarkOnYard(world, order);
    }
    // THE NEAREST FREE PLACE BY THE CORE'S OWN RULE (plot.h, FreePlot), as for
    // the yards. The rings below stepped 60 m for a 60 m cattle-yard plot, so
    // neighbours on one ring were refused by construction: once the granaries
    // moved onto their yard (0ab4c98) 38 and 41 cattle-yard marks of thirty
    // years were refused on seeds 1930 and 1934 (boss, parcel 226).
    const std::vector<float>& radii = definitions_.units.keep_out_radius_m;
    if (type.value < radii.size() && radii[type.value] > 0.0F) {
      // THE LONG STORE ON THE ELDER'S POINT FIRST (suggested_place.h; #756:
      // suggest_vegetable_store was never taken by any run); the next by the
      // village's middle, as every other roof.
      const std::optional<core::Vec2> point =
          type.value == food_store_.value
              ? store_suggested_.Take(world, definitions_.Plots(), radii[type.value])
              : std::nullopt;
      order.position =
          point
              ? *point
              : core::FreePlot(world.units, definitions_.Plots(), Centre(world), radii[type.value]);
      ++ordered_;
      return true;
    }
    // RINGS AROUND THE CENTRE, NOT A LINE AWAY FROM IT. Until 2026-09-13 every
    // order went kStepAside further east than the last, so by the thirtieth
    // the site stood kilometres out — past the accountant's road limit, where
    // nobody is ever sent. On seed 1933 a granary stood crewless for a year
    // with 300 hands idle, and because this policy starts nothing new while a
    // site of its own is still up, the farm stopped raising stores for good.
    // Eight places a ring, each ring kStepAside further out: thirty orders
    // stay within four rings.
    static constexpr std::array<std::array<float, 2>, 8> kRing = {{{1.0F, 0.0F},
                                                                   {0.0F, 1.0F},
                                                                   {-1.0F, 0.0F},
                                                                   {0.0F, -1.0F},
                                                                   {1.0F, 1.0F},
                                                                   {-1.0F, 1.0F},
                                                                   {-1.0F, -1.0F},
                                                                   {1.0F, -1.0F}}};
    const core::Vec2 centre = Centre(world);
    const auto ring = static_cast<float>(1U + (attempts_ / kRing.size()));
    const std::array<float, 2>& heading = kRing[attempts_ % kRing.size()];
    order.position = core::Vec2{.x = centre.x + (heading[0] * ring * kStepAside),
                                .y = centre.y + (heading[1] * ring * kStepAside)};
    ++attempts_;
    ++ordered_;
    return true;
  }

  /// STORE MODULARITY (boss, parcels 198 and 222; unit rules §11): a granary
  /// is a module of the food yard, so the first granary order puts the yard
  /// up — a plot, nothing spent — at the nearest free place by the core's own
  /// rule, and every granary after it stands on that yard. Modules of one
  /// parent do not refuse each other, so one yard carries them all; they go
  /// round its centre a ring inside its plot rather than on one point, so a
  /// reader of the map can still tell them apart.
  bool MarkOnYard(const core::WorldState& world, core::OrderRow& order) {
    std::uint32_t yard = core::kNoRow;
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      if (world.units.rows[row].type.value == granary_yard_.value) {
        yard = row;
        break;
      }
    }
    if (yard == core::kNoRow) {
      const std::vector<float>& radii = definitions_.units.keep_out_radius_m;
      const float radius = granary_yard_.value < radii.size() ? radii[granary_yard_.value] : 0.0F;
      order.unit_type = granary_yard_;
      // ON THE ELDER'S POINT FIRST (suggested_place.h; 0.37.159).
      const std::optional<core::Vec2> point =
          yard_suggested_.Take(world, definitions_.Plots(), radius);
      order.position =
          point ? *point : core::FreePlot(world.units, definitions_.Plots(), Centre(world), radius);
      ++yards_ordered_;
      return true;
    }
    static constexpr float kOnYard = 20.0F;
    static constexpr std::array<std::array<float, 2>, 8> kRound = {{{1.0F, 0.0F},
                                                                    {0.0F, 1.0F},
                                                                    {-1.0F, 0.0F},
                                                                    {0.0F, -1.0F},
                                                                    {0.7F, 0.7F},
                                                                    {-0.7F, 0.7F},
                                                                    {-0.7F, -0.7F},
                                                                    {0.7F, -0.7F}}};
    const core::Vec2 centre = world.units.rows[yard].position;
    const std::array<float, 2>& heading = kRound[granaries_on_yard_ % kRound.size()];
    order.position =
        core::Vec2{.x = centre.x + (heading[0] * kOnYard), .y = centre.y + (heading[1] * kOnYard)};
    ++granaries_on_yard_;
    ++ordered_;
    return true;
  }

  /// The clamp by the fields it serves: the nearest free place to the
  /// area-weighted middle of this year's potato and vegetable fields.
  bool MarkClamp(const core::WorldState& world, core::OrderRow& order) {
    double weight = 0.0;
    double x = 0.0;
    double y = 0.0;
    for (const core::FieldRow& field : world.fields.rows) {
      const std::uint16_t crop = field.rotation_year0.value;
      if (field.kind != core::LandKind::kArable || crop >= roof_.yield_kg_per_ha.size() ||
          !(roof_.yield_kg_per_ha[crop] > 0.0F)) {
        continue;
      }
      weight += static_cast<double>(field.area_ga);
      x += static_cast<double>(field.center.x) * static_cast<double>(field.area_ga);
      y += static_cast<double>(field.center.y) * static_cast<double>(field.area_ga);
    }
    const core::Vec2 near_fields = weight > 0.0 ? core::Vec2{.x = static_cast<float>(x / weight),
                                                             .y = static_cast<float>(y / weight)}
                                                : Centre(world);
    const std::vector<float>& radii = definitions_.units.keep_out_radius_m;
    const float radius = clamp_.value < radii.size() ? radii[clamp_.value] : 0.0F;
    order.kind = core::OrderKind::kBuildUnit;
    order.unit_type = clamp_;
    // ON THE ELDER'S POINT FIRST (suggested_place.h; 0.37.159); the second
    // clamp and on by the fields, as before.
    const std::optional<core::Vec2> point =
        clamp_suggested_.Take(world, definitions_.Plots(), radius);
    order.position =
        point ? *point : core::FreePlot(world.units, definitions_.Plots(), near_fields, radius);
    ++ordered_;
    return true;
  }

  /// THE VILLAGE'S MIDDLE IS THE MEAN OF THE HOUSES PEOPLE LIVE IN (0.37.111),
  /// as the house, school, office and social policies took it already
  /// (village_middle.h, the one home). It was the mean of EVERY unit
  /// standing, and the five old wells of 0.37.110 — 0.6 to 3 km out, nobody's
  /// neighbours — moved it: the food yard went 270 m south on all nine
  /// villages, and Epoch II opened in years 13..24 for 13..18.
  static core::Vec2 Centre(const core::WorldState& world) { return VillageMiddle(world); }

  /// A SAFETY STOP, not a plan: a run that builds without limit stops
  /// measuring a village and starts measuring a warehouse. It is not what
  /// binds today — at fourteen the chairman still stopped at seven granaries,
  /// because his sites run out of materials before he runs out of permission,
  /// and the run says so with its own "STORAGE STILL BINDS" line.
  static constexpr std::uint32_t kMaxOfEach = 14;

  core::UnitTypeId granary_;

  core::UnitTypeId cattle_;

  /// The granary's parent by unit_types.csv — the food yard.
  core::UnitTypeId granary_yard_;

  /// The food store and the church — the roofs of potatoes and vegetables.
  core::UnitTypeId food_store_;

  core::UnitTypeId church_;

  /// The clamp: the field store of potatoes and vegetables, an outline.
  core::UnitTypeId clamp_;

  /// What needs a roof and what gives it, READ OFF THE STORES THAT KEEP THE
  /// CROP (crop_roof.h; #756). Until 0.37.240 the set was read by the name
  /// `food_store`, which keeps no crop since 0.37.217.
  CropRoof roof_;

  /// Whether the long store is a module of the granary's yard.
  bool store_on_yard_ = false;

  /// Whether the recipe of the standing long store's next level is in the
  /// village today (RunDay asks the core, as for the warm barn).
  bool store_raise_in_hand_ = false;

  /// Upgrades of a long store ordered.
  std::uint32_t stores_raised_ = 0;

  bool last_ordered_food_store_ = false;

  /// This year's potatoes and vegetables by the fields' chains at a normal
  /// yield: what needs a roof this autumn.
  core::Grams RoofedHarvestGrams(const core::WorldState& world) const {
    double kilograms = 0.0;
    for (const core::FieldRow& field : world.fields.rows) {
      const std::uint16_t crop = field.rotation_year0.value;
      if (field.kind == core::LandKind::kArable && crop < roof_.yield_kg_per_ha.size()) {
        kilograms +=
            static_cast<double>(roof_.yield_kg_per_ha[crop]) * static_cast<double>(field.area_ga);
      }
    }
    return core::GramsFromKilograms(static_cast<float>(kilograms));
  }

  /// A long store's room at `level`, grams (crop_roof.h; 0 beyond the table).
  core::Grams StoreRoomAt(std::size_t level) const {
    return level >= 1 && level <= roof_.store_grams.size() ? roof_.store_grams[level - 1] : 0;
  }

  /// The room of the church and of every long store, standing or going up -
  /// at the level it stands at or is being raised to.
  core::Grams RoofedRoomGrams(const core::WorldState& world) const {
    core::Grams room = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.type.value == food_store_.value && unit.dead == 0) {
        const std::size_t level = unit.construction.target_level > unit.level
                                      ? unit.construction.target_level
                                      : unit.level;
        room += StoreRoomAt(level);
      }
      room += unit.type.value == church_.value && unit.level > 0 ? roof_.church_grams : 0;
    }
    return room;
  }

  /// A long store standing below its top level with no works open, or kNoRow:
  /// the next room is its next level before another store's plot
  /// (resources design §4: «150 т, затем 300 т; место под последний уровень с
  /// первого дня»).
  std::uint32_t StoreToRaise(const core::WorldState& world) const {
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      if (unit.type.value == food_store_.value && unit.dead == 0 && unit.level > 0 &&
          unit.level < roof_.store_grams.size() &&
          unit.construction.phase == core::ConstructionPhase::kNone &&
          !unit.suspended_works.has_value()) {
        return row;
      }
    }
    return core::kNoRow;
  }

  /// Whether a long store's upgrade is open: nothing more of its kind until
  /// it stands (the rule every site of the farm's keeps).
  bool StoreBeingRaised(const core::WorldState& world) const {
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.type.value == food_store_.value && unit.dead == 0 && unit.level > 0 &&
          unit.construction.phase != core::ConstructionPhase::kNone) {
        return true;
      }
    }
    return false;
  }

  /// The plot radii and the map, for FreePlot.
  core::Definitions definitions_;

  /// The food yard's and the clamp's points (suggestions.csv).
  SuggestedPlaces yard_suggested_;

  SuggestedPlaces clamp_suggested_;

  /// The long store's point (suggest_vegetable_store today).
  SuggestedPlaces store_suggested_;

  /// THE SECOND RICK (routing stage В3; boss's ruling «(i)», 14 October 2026;
  /// suggestions.csv `suggest_hay_stack`): from the stage a meadow's hay is
  /// carted to the NEAREST rick and a farm is fed from the nearest holder, so
  /// where the ricks stand is the layout's question - and the start has one,
  /// by the old village. The chairman stakes a second on the elder's point,
  /// once, before September of year 1. A table set with no such row is the
  /// layout without it: the mark is not made and the report says so.
  core::UnitTypeId rick_;

  SuggestedPlaces rick_suggested_;

  /// The second rick was marked, or found to have no point: not asked again.
  bool rick_settled_ = false;

  /// The day the second rick was marked; kNoRickDay while it has not been.
  core::SimDay rick_marked_day_ = kNoRickDay;

  /// The days the ricks beyond the start's first stood up.
  std::vector<core::SimDay> rick_days_;

  static constexpr core::SimDay kNoRickDay = static_cast<core::SimDay>(-1);

  /// September of year 1, its first day: the second rick is staked before it.
  static constexpr core::SimDay kSecondRickBeforeDay = 8U * core::kDaysPerMonth;

  std::uint32_t yards_ordered_ = 0;

  core::UnitTypeId marked_type_;

  std::uint32_t rows_before_mark_ = 0;

  std::uint32_t cattle_refused_ = 0;

  std::uint32_t other_refused_ = 0;

  std::uint32_t granaries_on_yard_ = 0;

  std::uint32_t cooldown_ = 0;

  std::uint32_t attempts_ = 0;

  std::uint32_t ordered_ = 0;

  bool last_ordered_cattle_ = false;

  /// The cattle yard's rung that is warm (Livestock design: rung 2, the
  /// wattle-and-clay barn; rung 1 the open pen).
  static constexpr std::uint8_t kWarmBarnLevel = 2;

  /// The months the first pen's warm comes before every other building
  /// (August to November: the winter months begin in December).
  static constexpr std::uint32_t kPenWindowFirstMonth = 8;
  static constexpr std::uint32_t kPenWindowLastMonth = 11;
  /// The clay warm is ordered from March to September, 0-based months of
  /// calendar.date.month (form (a1); STUB, «cannot be done by 1 December»).
  static constexpr std::uint32_t kClayWarmFirstMonth = 2;
  static constexpr std::uint32_t kClayWarmLastMonth = 8;

  /// The sites paused for the first pen's warm (PauseSitesForThePen), to be
  /// resumed when it stands warm or the window closes.
  std::vector<core::UnitId> paused_for_pen_;

  /// suggestions.csv's point for the cattle yard, and whether there is one.
  core::Vec2 pen_suggestion_{};

  bool has_pen_suggestion_ = false;

  bool pen_suggestion_refused_ = false;

  bool marked_on_suggestion_ = false;

  bool warm_barn_in_hand_ = false;

  std::uint32_t pens_on_suggestion_ = 0;

  core::SimDay entry_day_ = 12;

  /// By LivestockKindId: 1 when the kind's home is the cattle yard.
  std::vector<std::uint8_t> cattle_home_;

  /// The cattle yard's places by level, index level - 1.
  std::vector<float> cattle_places_;

  std::uint32_t warms_ordered_ = 0;

  /// The days the food stores stood up, in order, and the day the horses
  /// were first stabled: when the harvest's room came against when the
  /// harvest came (boss, parcel 412).
  std::vector<core::SimDay> food_store_days_;

  std::vector<core::SimDay> clamp_days_;

  core::SimDay stable_day_ = 0;

  bool stable_seen_ = false;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_FIXTURE_POLICY_H_
