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

#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>

#include "core_common/herd_state.h"
#include "core_common/land_state.h"
#include "core_common/ledger_state.h"
#include "core_common/order_state.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

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
  }

  /// @brief One day of the chairman's attention. Call once a day.
  void RunDay(core::ISimulation& simulation) {
    if (cooldown_ > 0) {
      --cooldown_;
      return;
    }
    const core::WorldState& world = simulation.CompletedState();
    core::OrderRow order;
    if (!NextOrder(world, order)) {
      return;
    }
    simulation.StageOrders(std::span<const core::OrderRow>(&order, 1), {});
    cooldown_ = kCooldownDays;
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
              << Built(world, granary_) << " granaries and " << Built(world, cattle_)
              << " cattle yards stand\n";
  }

 private:
  static constexpr std::uint32_t kCooldownDays = 4;

  static constexpr float kStepAside = 60.0F;

  static core::UnitTypeId TypeByKey(const core::ITableSet& tables, std::string_view key) {
    const core::ITable* types = tables.FindTable("unit_types");
    const std::uint32_t row = types == nullptr ? core::kNoTableRow : types->FindRowByKey(key);
    return row == core::kNoTableRow ? core::UnitTypeId{}
                                    : core::UnitTypeId{static_cast<std::uint16_t>(row)};
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
  static bool RoomWasShort(const core::WorldState& world) {
    for (const core::FieldRow& field : world.fields.rows) {
      // A LOAD BEING CARRIED IS NOT A SHORTAGE. What says "nowhere to put
      // it" is a load with NO CARRYING DEMAND against it: production sizes
      // that demand by the room in the stores, so a zero demand under a
      // standing load means the doors are shut. The first version of this
      // read any load at all as a shortage, and since carrying takes days
      // that was nearly always true — the chairman ordered thirty-nine
      // buildings in thirty years and took the hands to raise them off the
      // fields.
      if (field.reaped_grams > 0 && field.haul_days_remaining <= 0.0F) {
        return true;
      }
    }
    for (const core::Grams lost : world.ledger.closed.lost_no_room) {
      if (lost > 0) {
        return true;
      }
    }
    return false;
  }

  bool NextOrder(const core::WorldState& world, core::OrderRow& order) {
    // A site of either kind already going up: nothing new until it stands.
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      const bool ours = unit.type.value == granary_.value || unit.type.value == cattle_.value;
      if (!ours || unit.level != 0) {
        continue;
      }
      if (unit.construction.labor_days_remaining > 0.0F) {
        return false;
      }
      order.kind = core::OrderKind::kStartBuild;
      order.unit = world.units.row_ids[row];
      return true;
    }
    const bool wants_granary =
        Wants(world, granary_, Built(world, granary_) == 0 || RoomWasShort(world));
    const bool wants_cattle =
        Wants(world, cattle_, Built(world, cattle_) == 0 || RoofWasShort(world));
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
    const core::Vec2 centre = Centre(world);
    order.position = core::Vec2{.x = centre.x + (static_cast<float>(attempts_) * kStepAside),
                                .y = centre.y + kStepAside};
    ++attempts_;
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

  std::uint32_t cooldown_ = 0;

  std::uint32_t attempts_ = 0;

  std::uint32_t ordered_ = 0;

  bool last_ordered_cattle_ = false;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_FIXTURE_POLICY_H_
