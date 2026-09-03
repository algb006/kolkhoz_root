/// @file
/// @brief The player, for the one decision the start cannot do without: the
/// kolkhoz yard and the groom who fills it.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THIS EXISTS AND WHY IT IS HERE. The core used to raise the yard
/// itself, in a stub at the turn of the first year (core_world/world.cpp,
/// RaiseKolkhozYard), because phase 1 had no construction and nobody could
/// build the first building of the campaign — and without it the start team
/// dies of old age by the sixth year and the farm stops ploughing for ever.
/// Task A7 removed that stub whole. The rule it stood in for was never a
/// rule: it was the PLAYER, and a run is the only thing in this repository
/// that plays him. So the yard is built here, by orders through the book,
/// exactly as a chairman would build it (manual/74-posts.md §8).
///
/// It watches the WORLD and not the event stream: what it wants to know is
/// "is there a yard yet, does it have a groom yet", and both are readings of
/// state. An order that is refused therefore costs it one cooldown and a
/// different guess, which is also how a player would find out.

#ifndef TESTS_RUN_COMMON_YARD_POLICY_H_
#define TESTS_RUN_COMMON_YARD_POLICY_H_

#include <cstdint>
#include <span>
#include <string_view>

#include "core_common/order_state.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace run {

/// @brief Builds the kolkhoz yard, takes it up to the stable step and
/// appoints a groom — then stops, for good, the day the team comes in.
class YardPolicy {
 public:
  /// @param tables The run's own table set. A set without a horse_yard type
  ///        or a groom post leaves the policy inert: it has nothing to ask
  ///        for, and says so by doing nothing.
  explicit YardPolicy(const core::ITableSet& tables) {
    yard_type_ = TypeByKey(tables, "unit_types", "horse_yard");
    groom_post_ = PostByKey(tables, "groom");
    adult_age_years_ = Knob(tables, "life", "adult_age_years", 16.0F);
    life_speedup_ = Knob(tables, "life", "life_speedup", 4.0F);
  }

  /// @brief Whether the policy has anything left to do. False once the team
  /// is stabled — the milestone it exists for.
  bool Watching() const { return watching_; }

  /// @brief One day's worth of the chairman's attention. Call once a day,
  /// after the day's steps: it stages at most one order and then waits for
  /// the world to answer.
  void RunDay(core::ISimulation& simulation) {
    if (!watching_ || yard_type_.value == core::kInvalidDefIdValue) {
      return;
    }
    const core::WorldState& world = simulation.CompletedState();
    if (world.chairman.horses_stabled != 0) {
      watching_ = false;  // the horses are in: nothing here matters any more
      return;
    }
    if (cooldown_ > 0) {
      --cooldown_;
      return;
    }
    core::OrderRow order;
    if (!NextOrder(world, order)) {
      return;
    }
    simulation.StageOrders(std::span<const core::OrderRow>(&order, 1), {});
    cooldown_ = kCooldownDays;
  }

 private:
  /// Days to wait for an order to land before guessing again. Two, because
  /// an appointment is applied at the day's CLOSE and shows in the world
  /// only on the day after the one it was issued on.
  static constexpr std::uint32_t kCooldownDays = 2;

  /// How far to step aside when a plot is refused for being too close to
  /// somebody else's, in metres.
  static constexpr float kStepAside = 40.0F;

  static core::UnitTypeId TypeByKey(const core::ITableSet& tables,
                                    std::string_view table_name,
                                    std::string_view key) {
    const core::ITable* table = tables.FindTable(table_name);
    const std::uint32_t row = table == nullptr ? core::kNoTableRow : table->FindRowByKey(key);
    return row == core::kNoTableRow ? core::UnitTypeId{}
                                    : core::UnitTypeId{static_cast<std::uint16_t>(row)};
  }

  static core::ProfessionId PostByKey(const core::ITableSet& tables, std::string_view key) {
    const core::ITable* table = tables.FindTable("professions");
    const std::uint32_t row = table == nullptr ? core::kNoTableRow : table->FindRowByKey(key);
    return row == core::kNoTableRow ? core::ProfessionId{}
                                    : core::ProfessionId{static_cast<std::uint16_t>(row)};
  }

  static float Knob(const core::ITableSet& tables,
                    std::string_view table_name,
                    std::string_view key,
                    float fallback) {
    const core::ITable* table = tables.FindTable(table_name);
    if (table == nullptr) {
      return fallback;
    }
    const std::uint32_t row = table->FindRowByKey(key);
    const std::uint32_t column = table->FindColumn("value");
    if (row == core::kNoTableRow || column == core::kNoTableColumn) {
      return fallback;
    }
    const std::optional<float> cell = table->CellReal(row, column);
    return cell && *cell > 0.0F ? *cell : fallback;
  }

  /// @brief The village's middle: where a chairman would put the yard. The
  /// mean of what is already standing, which is the settlement wherever the
  /// map generator decided to put it.
  static core::Vec2 VillageCentre(const core::WorldState& world) {
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

  std::uint32_t FindYard(const core::WorldState& world) const {
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      if (world.units.rows[row].type.value == yard_type_.value) {
        return row;
      }
    }
    return core::kNoRow;
  }

  bool HasGroom(const core::WorldState& world, core::UnitId yard) const {
    for (const core::ResidentRow& resident : world.residents.rows) {
      if (resident.post.profession.value == groom_post_.value &&
          resident.post.unit.value == yard.value) {
        return true;
      }
    }
    return false;
  }

  /// @brief The chairman's next move, or false when he is waiting.
  bool NextOrder(const core::WorldState& world, core::OrderRow& order) {
    const std::uint32_t yard_row = FindYard(world);
    if (yard_row == core::kNoRow) {
      // Nothing marked yet. Each attempt steps a little further out, so a
      // refusal for crowding is answered rather than repeated.
      const core::Vec2 centre = VillageCentre(world);
      order.kind = core::OrderKind::kBuildUnit;
      order.unit_type = yard_type_;
      order.position =
          core::Vec2{.x = centre.x + (static_cast<float>(attempts_) * kStepAside), .y = centre.y};
      ++attempts_;
      return true;
    }
    const core::UnitRow& yard = world.units.rows[yard_row];
    const core::UnitId id = world.units.row_ids[yard_row];
    if (yard.level == 0) {
      if (yard.construction.labor_days_remaining > 0.0F) {
        return false;  // the brigade is on it; a second kStartBuild is noise
      }
      order.kind = core::OrderKind::kStartBuild;
      order.unit = id;
      return true;
    }
    // The stable is the SECOND step, and foals come only under its roof
    // (livestock design §5) — so the run takes the ladder up before it
    // worries about the groom.
    if (yard.level < kStableLevel) {
      if (yard.construction.phase != core::ConstructionPhase::kNone) {
        return false;
      }
      order.kind = core::OrderKind::kUpgradeUnit;
      order.unit = id;
      return true;
    }
    if (HasGroom(world, id)) {
      return false;  // appointed; the horses come in on the herd day
    }
    const std::uint32_t groom = NextCandidate(world);
    if (groom == core::kNoRow || groom_post_.value == core::kInvalidDefIdValue) {
      return false;
    }
    order.kind = core::OrderKind::kAppoint;
    order.resident = world.residents.row_ids[groom];
    order.unit = id;
    order.profession = groom_post_;
    return true;
  }

  /// @brief An adult with no post, taken in row order — and a different one
  /// each time the last guess did not take, so an ineligible candidate
  /// costs a cooldown rather than the campaign.
  std::uint32_t NextCandidate(const core::WorldState& world) {
    const std::uint32_t skip = candidate_++;
    std::uint32_t seen = 0;
    for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
      const core::ResidentRow& resident = world.residents.rows[row];
      if (resident.post.profession.value != core::kInvalidDefIdValue) {
        continue;
      }
      // Campaign days are unsigned and a birth day is SIGNED: the starting
      // generation was born before day 0 (resident_state.h), so the
      // subtraction is done in the signed type or the old-timers come out
      // four billion days old.
      const auto lived = static_cast<std::int64_t>(world.calendar.day) -
                         static_cast<std::int64_t>(resident.birth_day);
      const float age =
          static_cast<float>(lived) * life_speedup_ / static_cast<float>(core::kDaysPerYear);
      if (age < adult_age_years_) {
        continue;
      }
      if (seen++ < skip % (world.residents.rows.size() + 1U)) {
        continue;
      }
      return row;
    }
    candidate_ = 0;  // ran off the end: start over from the top
    return core::kNoRow;
  }

  /// The ladder step that is the stable (livestock design §5).
  static constexpr std::uint8_t kStableLevel = 2;

  core::UnitTypeId yard_type_;

  core::ProfessionId groom_post_;

  float adult_age_years_ = 16.0F;

  float life_speedup_ = 4.0F;

  std::uint32_t cooldown_ = 0;

  std::uint32_t attempts_ = 0;

  std::uint32_t candidate_ = 0;

  bool watching_ = true;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_YARD_POLICY_H_
