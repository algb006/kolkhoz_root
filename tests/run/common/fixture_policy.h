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
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/stub_tables.h"
#include "core_tables/tables.h"
#include "core_world/world.h"
#include "start_gate.h"
#include "store_parent.h"

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
  explicit FixturePolicy(const core::ITableSet& tables) {
    granary_ = TypeByKey(tables, "granary");
    cattle_ = TypeByKey(tables, "cattle_yard");
    granary_yard_ = ParentTypeOf(tables, "granary");
    food_store_ = TypeByKey(tables, "food_store");
    church_ = TypeByKey(tables, "church_store");
    std::string error;
    core::LoadDefinitions(tables, core::StubTables::kAllowed, definitions_, error);
    ReadRoofedFood(tables);
  }

  /// @brief The question asked before every start (start_gate.h).
  void SetStartGate(StartGate gate) { start_gate_ = std::move(gate); }

  /// @brief One day of the chairman's attention. Call once a day.
  void RunDay(core::ISimulation& simulation) {
    CountWaitStreaks(simulation.CompletedState());
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
      }
      marked_type_ = core::UnitTypeId{};
    }
    core::OrderRow order;
    if (!NextOrder(world, order)) {
      return;
    }
    if (order.kind == core::OrderKind::kBuildUnit) {
      marked_type_ = order.unit_type;
      rows_before_mark_ = Rows(world, order.unit_type);
    }
    simulation.StageOrders(std::span<const core::OrderRow>(&order, 1), {});
    cooldown_ = kCooldownDays;
  }

  /// @brief Whether today the farm's own building must go before any house
  /// (boss, parcel 298: the chairman builds what the farm lacks MOST). True
  /// while a site of a store for the harvest is marked and short of its recipe
  /// and the harvest has nowhere to go, or a cattle-yard site is and animals
  /// stand without a roof. A site already started holds nothing back.
  bool HoldsHousesBack(const core::ISimulation& simulation) const {
    const core::WorldState& world = simulation.CompletedState();
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
                 "run has nobody to decide on them (boss, 2026-09-03)\n";
  }

  /// @brief What the fixture actually did, for the run to print at the end.
  void Report(const core::WorldState& world) const {
    std::cout << "thirty_years: the run's chairman ordered " << ordered_ << " fixture buildings; "
              << Built(world, granary_) << " granaries, " << Built(world, food_store_)
              << " food stores and " << Built(world, cattle_)
              << " cattle yards stand; the stores stand as modules of "
              << Built(world, granary_yard_) << " food yards (" << yards_ordered_
              << " yard orders); marks refused: cattle yards " << cattle_refused_
              << ", granaries and yards " << other_refused_ << "\n";
  }

 private:
  static constexpr std::uint32_t kCooldownDays = 4;

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

  /// @brief Whether the kolkhoz animals have a roof over all of them. The
  /// billeted count is the herd system's own answer to that question, so the
  /// fixture asks it rather than guessing at capacities.
  static bool RoofWasShort(const core::WorldState& world) {
    for (const core::HerdRow& herd : world.herds.rows) {
      if (herd.household_owned != 0) {
        continue;
      }
      const std::uint32_t heads =
          static_cast<std::uint32_t>(herd.adult_count) + herd.juvenile_count + herd.newborn_count;
      if (heads > herd.billeted_count) {
        return true;
      }
    }
    return false;
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
    return resource.value < roofed_resource_.size() && roofed_resource_[resource.value] != 0;
  }

  bool NextOrder(const core::WorldState& world, core::OrderRow& order) {
    // A site of any of its kinds already going up: nothing new until it stands.
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      const bool ours = unit.type.value == granary_.value || unit.type.value == cattle_.value ||
                        unit.type.value == granary_yard_.value ||
                        unit.type.value == food_store_.value;
      if (!ours || unit.level != 0) {
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
    const bool wants_granary =
        Wants(world, granary_, Built(world, granary_) == 0 || RoomWasShort(world));
    const bool wants_cattle =
        Wants(world, cattle_, Built(world, cattle_) == 0 || RoofWasShort(world));
    // A ROOF FOR THIS YEAR'S POTATOES AND VEGETABLES (boss, parcels 401 and
    // 408). Since the stores take only their homes, these two go into the
    // church and a food store and nowhere else, and what finds no roof is lost
    // to the snow. The chairman sees this year's crops in the fields' chains,
    // so he puts up food stores until their room and the church's hold this
    // year's harvest — THE CEILING, and no more than it — taking turns with
    // the granary and the cattle yard when those are wanted too.
    // NOT BEFORE THE STABLE, as the school (school_policy.h): the food store's
    // logs and boards go to the chairman's yard first. Measured on three seeds
    // once the two real knots of seed 1929 were cut (the demolished build yard
    // and the saw deaf to the stable): year 30 stood at 183, 366 and 304 with
    // the stable first and 180, 261 and 234 without it (2026-09-15).
    const bool wants_food_store =
        world.chairman.horses_stabled != 0 && food_store_.value != core::kInvalidDefIdValue &&
        RoofedHarvestGrams(world) > RoofedRoomGrams(world) && Rows(world, food_store_) < kMaxOfEach;
    if (wants_food_store && !(last_ordered_food_store_ && (wants_granary || wants_cattle))) {
      last_ordered_food_store_ = true;
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
    if ((type.value == granary_.value || type.value == food_store_.value) &&
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
      order.position =
          core::FreePlot(world.units, definitions_.Plots(), Centre(world), radii[type.value]);
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
      order.position = core::FreePlot(world.units, definitions_.Plots(), Centre(world), radius);
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

  static core::Vec2 Centre(const core::WorldState& world) {
    core::Vec2 sum{.x = 0.0F, .y = 0.0F};
    std::uint32_t seen = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      sum.x += unit.position.x;
      sum.y += unit.position.y;
      ++seen;
    }
    if (seen == 0) {
      return core::Vec2{.x = 150.0F, .y = 150.0F};
    }
    return core::Vec2{.x = sum.x / static_cast<float>(seen), .y = sum.y / static_cast<float>(seen)};
  }

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

  /// kg per hectare by crop row, for the crops whose produce the food store
  /// keeps (resource_stores.csv storage `food_store`); 0 for every other crop.
  std::vector<float> roofed_yield_kg_per_ha_;

  /// 1 per ResourceId the food store keeps.
  std::vector<std::uint8_t> roofed_resource_;

  /// Level-1 capacity, grams, of the food store and the church.
  core::Grams food_store_grams_ = 0;

  core::Grams church_grams_ = 0;

  bool last_ordered_food_store_ = false;

  void ReadRoofedFood(const core::ITableSet& tables) {
    const core::ITable* const stores = tables.FindTable("resource_stores");
    const core::ITable* const crops = tables.FindTable("crops");
    const core::ITable* const levels = tables.FindTable("unit_levels");
    if (stores == nullptr || crops == nullptr || levels == nullptr) {
      return;
    }
    roofed_yield_kg_per_ha_.assign(crops->RowCount(), 0.0F);
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      roofed_resource_.assign(resources->RowCount(), 0);
      for (std::uint32_t row = 0; row < stores->RowCount(); ++row) {
        const std::uint32_t resource =
            resources->FindRowByKey(stores->CellText(row, stores->FindColumn("resource")));
        if (resource < roofed_resource_.size() &&
            stores->CellText(row, stores->FindColumn("storage")) == "food_store") {
          roofed_resource_[resource] = 1;
        }
      }
    }
    for (std::uint32_t crop = 0; crop < crops->RowCount(); ++crop) {
      const std::string_view produce = crops->CellText(crop, crops->FindColumn("resource"));
      bool roofed = false;
      for (std::uint32_t row = 0; row < stores->RowCount(); ++row) {
        roofed = roofed || (stores->CellText(row, stores->FindColumn("resource")) == produce &&
                            stores->CellText(row, stores->FindColumn("storage")) == "food_store");
      }
      if (roofed) {
        roofed_yield_kg_per_ha_[crop] = std::strtof(
            std::string(crops->CellText(crop, crops->FindColumn("yield_kg_per_ha"))).c_str(),
            nullptr);
      }
    }
    for (std::uint32_t row = 0; row < levels->RowCount(); ++row) {
      const std::string_view unit = levels->CellText(row, levels->FindColumn("unit"));
      if (levels->CellText(row, levels->FindColumn("level")) != "1") {
        continue;
      }
      const core::Grams grams = core::GramsFromKilograms(
          1000.0F *
          std::strtof(
              std::string(levels->CellText(row, levels->FindColumn("storage_capacity_t"))).c_str(),
              nullptr));
      food_store_grams_ = unit == "food_store" ? grams : food_store_grams_;
      church_grams_ = unit == "church_store" ? grams : church_grams_;
    }
  }

  /// This year's potatoes and vegetables by the fields' chains at a normal
  /// yield: what needs a roof this autumn.
  core::Grams RoofedHarvestGrams(const core::WorldState& world) const {
    double kilograms = 0.0;
    for (const core::FieldRow& field : world.fields.rows) {
      const std::uint16_t crop = field.rotation_year0.value;
      if (field.kind == core::LandKind::kArable && crop < roofed_yield_kg_per_ha_.size()) {
        kilograms +=
            static_cast<double>(roofed_yield_kg_per_ha_[crop]) * static_cast<double>(field.area_ga);
      }
    }
    return core::GramsFromKilograms(static_cast<float>(kilograms));
  }

  /// The room of the church and of every food store, standing or going up.
  core::Grams RoofedRoomGrams(const core::WorldState& world) const {
    core::Grams room = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      room += unit.type.value == food_store_.value ? food_store_grams_ : 0;
      room += unit.type.value == church_.value && unit.level > 0 ? church_grams_ : 0;
    }
    return room;
  }

  /// The plot radii and the map, for FreePlot.
  core::Definitions definitions_;

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
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_FIXTURE_POLICY_H_
