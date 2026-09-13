/// @file
/// @brief The run's chairman puts up the utility yard and its sawmill,
/// appoints the yard's craftsman, and lets him saw only when boards are short
/// and logs are not (timber design §8б, "Протез").
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THE RUN SAWS. The felling measurement of 2026-09-13 found nothing
/// felled or used after the tenth year: building stalled on BOARDS, which the
/// village had only from the start stock (build yard and manor ruins, 82 m3)
/// and no channel to make. The sawmill is that channel, and a run has no
/// chairman to build it, staff it and decide when it saws. So the run plays
/// him, as it plays the felling chairman (felling_policy.h).
///
/// THE SIGNAL IS BOSS'S, WORD FOR WORD (parcel 196): boards fewer than the
/// nearest construction needs, AND logs more than the log construction of the
/// queue needs — "otherwise you saw up a log house". The saw is stopped with
/// the pause (§5), which is the mechanism a chairman has. The queue is every
/// site marked or delivering, in row order; the nearest need is the first site
/// still short of that material.
///
/// ONE CRAFTSMAN, NOT TWO. The sawmill has two places, and the policy fills
/// one: a holder is out of the accountant's pool for good, and on a day the
/// saw stands he stands too (the core models no other work of the post). Two
/// idle craftsmen a year is a cost the chairman pays for a rate the queue does
/// not ask for — a man-day of sawing is 7 logs into a cubic metre of boards,
/// and a granary takes eight.

#ifndef TESTS_RUN_COMMON_SAWMILL_POLICY_H_
#define TESTS_RUN_COMMON_SAWMILL_POLICY_H_

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_catalog/timber_catalog.h"
#include "core_common/calendar.h"
#include "core_common/labor_state.h"
#include "core_common/order_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace run {

class SawmillPolicy {
 public:
  explicit SawmillPolicy(const core::ITableSet& tables) {
    std::string error;
    const bool parsed = core::ParseTimberCatalog(tables, catalog_, error);
    yard_type_ = RowId<core::UnitTypeIdTag>(tables, "unit_types", "utility_yard");
    craftsman_post_ = RowId<core::ProfessionIdTag>(tables, "professions", "farm_craftsman");
    adult_age_years_ = Knob(tables, "life", "adult_age_years", 16.0F);
    life_speedup_ = Knob(tables, "life", "life_speedup", 4.0F);
    ReadCosts(tables);
    ready_ = parsed && yard_type_.value != core::kInvalidDefIdValue &&
             catalog_.sawmill_type.value != core::kInvalidDefIdValue &&
             craftsman_post_.value != core::kInvalidDefIdValue &&
             catalog_.board_resource.value != core::kInvalidDefIdValue &&
             catalog_.log_resource.value != core::kInvalidDefIdValue;
  }

  /// @brief The fixture difference, in words, for the run to print BEFORE it
  /// measures anything.
  static void Declare(const char* run) {
    std::cout << run
              << ": FIXTURE DIFFERS FROM THE START CANON — the run's chairman puts up the UTILITY "
                 "YARD and its SAWMILL at once, appoints ONE craftsman, and lets him saw only "
                 "while boards are fewer than the nearest site needs and logs more than the log "
                 "site of the queue needs; otherwise the sawmill is paused (timber design §8б; "
                 "boss, 2026-09-13)\n";
  }

  /// @brief One day of the chairman's attention. Call once a day.
  void RunDay(core::ISimulation& simulation) {
    if (!ready_) {
      return;
    }
    const core::WorldState& world = simulation.CompletedState();
    Observe(world);
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

  /// @brief Days the sawmill stood unpaused with a craftsman, and days it was
  /// wanted, for the run's own lines.
  std::uint32_t DaysSawing() const { return days_sawing_; }

  /// @brief What the policy did, for the run to print at the end.
  void Report(const char* run) const {
    std::cout << run << ": the run's chairman put up the sawmill on day "
              << (sawmill_built_day_ == kNever ? std::string("never")
                                               : std::to_string(sawmill_built_day_))
              << ", paused it " << pauses_ << " times and resumed it " << resumes_
              << " times; it stood open " << days_sawing_ << " days\n";
  }

  /// @brief Grams of `resource` in built units — what the stores hold.
  static core::Grams Held(const core::WorldState& world, core::ResourceId resource) {
    core::Grams held = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.level > 0 && resource.value < unit.stock.size()) {
        held += unit.stock[resource.value];
      }
    }
    return held;
  }

  /// @brief What the nearest site of the queue still lacks of `resource`, in
  /// grams: the first site marked or delivering, in row order, short of it.
  core::Grams NearestNeed(const core::WorldState& world, core::ResourceId resource) const {
    for (const core::UnitRow& unit : world.units.rows) {
      const bool queued = unit.construction.phase == core::ConstructionPhase::kMarked ||
                          unit.construction.phase == core::ConstructionPhase::kDelivering;
      // A repair asks for spare parts only (construction_system.cpp).
      const bool repair = unit.level > 0 && unit.construction.target_level == unit.level;
      if (!queued || repair) {
        continue;
      }
      const core::Grams cost = CostGrams(unit.type, unit.construction.target_level, resource);
      const core::Grams on_site =
          resource.value < unit.stock.size() ? unit.stock[resource.value] : 0;
      if (cost > on_site) {
        return cost - on_site;
      }
    }
    return 0;
  }

  /// @brief Whether a built sawmill stands unpaused today.
  bool SawmillOpen(const core::WorldState& world) const {
    const std::uint32_t mill = FindOfType(world, catalog_.sawmill_type);
    return mill != core::kNoRow && world.units.rows[mill].level > 0 &&
           world.units.rows[mill].paused == 0;
  }

  /// @brief Boss's condition, the one the pause follows.
  bool WantsSawing(const core::WorldState& world) const {
    const core::Grams board_need = NearestNeed(world, catalog_.board_resource);
    return board_need > 0 && Held(world, catalog_.board_resource) < board_need &&
           Held(world, catalog_.log_resource) > NearestNeed(world, catalog_.log_resource);
  }

 private:
  static constexpr std::uint32_t kCooldownDays = 1;
  static constexpr std::uint32_t kNever = 0xFFFFFFFFU;
  static constexpr float kStepAside = 80.0F;

  struct Cost {
    core::UnitTypeId type;
    std::uint8_t level = 0;
    core::ResourceId resource;
    core::Grams grams = 0;
  };

  template <typename Tag>
  static core::DefId<Tag> RowId(const core::ITableSet& tables,
                                std::string_view table_name,
                                std::string_view key) {
    const core::ITable* const table = tables.FindTable(table_name);
    const std::uint32_t row = table == nullptr ? core::kNoTableRow : table->FindRowByKey(key);
    return row == core::kNoTableRow ? core::DefId<Tag>{}
                                    : core::DefId<Tag>{static_cast<std::uint16_t>(row)};
  }

  static float Knob(const core::ITableSet& tables,
                    std::string_view table_name,
                    std::string_view key,
                    float fallback) {
    const core::ITable* const table = tables.FindTable(table_name);
    if (table == nullptr) {
      return fallback;
    }
    const std::uint32_t row = table->FindRowByKey(key);
    const std::optional<float> cell = table->CellReal(row, table->FindColumn("value"));
    return cell && *cell > 0.0F ? *cell : fallback;
  }

  /// Board and log lines of unit_level_cost.csv, in grams (the board is
  /// counted in m3, the log in pieces: kg_per_unit converts both).
  void ReadCosts(const core::ITableSet& tables) {
    const core::ITable* const costs = tables.FindTable("unit_level_cost");
    const core::ITable* const types = tables.FindTable("unit_types");
    if (costs == nullptr || types == nullptr) {
      return;
    }
    const std::uint32_t unit_col = costs->FindColumn("unit");
    const std::uint32_t level_col = costs->FindColumn("level");
    const std::uint32_t resource_col = costs->FindColumn("resource");
    const std::uint32_t amount_col = costs->FindColumn("amount");
    for (std::uint32_t row = 0; row < costs->RowCount(); ++row) {
      const std::string_view resource = costs->CellText(row, resource_col);
      const bool board = resource == "board";
      if (!board && resource != "log") {
        continue;
      }
      const std::uint32_t type_row = types->FindRowByKey(costs->CellText(row, unit_col));
      const std::optional<std::int64_t> level = costs->CellInteger(row, level_col);
      const std::optional<float> amount = costs->CellReal(row, amount_col);
      if (type_row == core::kNoTableRow || !level || !amount) {
        continue;
      }
      const double grams =
          static_cast<double>(*amount) *
          static_cast<double>(board ? catalog_.board_grams_per_m3 : catalog_.log_grams);
      costs_.push_back(Cost{.type = core::UnitTypeId{static_cast<std::uint16_t>(type_row)},
                            .level = static_cast<std::uint8_t>(*level),
                            .resource = board ? catalog_.board_resource : catalog_.log_resource,
                            .grams = static_cast<core::Grams>(std::llround(grams))});
    }
  }

  core::Grams CostGrams(core::UnitTypeId type,
                        std::uint8_t level,
                        core::ResourceId resource) const {
    for (const Cost& cost : costs_) {
      if (cost.type.value == type.value && cost.level == level &&
          cost.resource.value == resource.value) {
        return cost.grams;
      }
    }
    return 0;
  }

  static std::uint32_t FindOfType(const core::WorldState& world, core::UnitTypeId type) {
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      if (world.units.rows[row].type.value == type.value) {
        return row;
      }
    }
    return core::kNoRow;
  }

  bool HasCraftsman(const core::WorldState& world, core::UnitId yard) const {
    for (const core::ResidentRow& resident : world.residents.rows) {
      if (resident.post.profession.value == craftsman_post_.value &&
          resident.post.unit.value == yard.value) {
        return true;
      }
    }
    return false;
  }

  void Observe(const core::WorldState& world) {
    const std::uint32_t mill = FindOfType(world, catalog_.sawmill_type);
    if (mill == core::kNoRow || world.units.rows[mill].level == 0) {
      return;
    }
    if (sawmill_built_day_ == kNever) {
      sawmill_built_day_ = static_cast<std::uint32_t>(world.calendar.day);
    }
    days_sawing_ += world.units.rows[mill].paused == 0 ? 1U : 0U;
  }

  static core::Vec2 Centre(const core::WorldState& world) {
    core::Vec2 sum{.x = 0.0F, .y = 0.0F};
    for (const core::UnitRow& unit : world.units.rows) {
      sum.x += unit.position.x;
      sum.y += unit.position.y;
    }
    const auto count = static_cast<float>(world.units.rows.size());
    return count > 0.0F ? core::Vec2{.x = sum.x / count, .y = sum.y / count} : sum;
  }

  static bool Mark(core::UnitTypeId type, core::Vec2 place, core::OrderRow& order) {
    order.kind = core::OrderKind::kBuildUnit;
    order.unit_type = type;
    order.position = place;
    return true;
  }

  static bool Start(const core::WorldState& world, std::uint32_t row, core::OrderRow& order) {
    const core::UnitRow& unit = world.units.rows[row];
    if (unit.construction.phase != core::ConstructionPhase::kMarked) {
      return false;  // delivering or building: the site is going
    }
    order.kind = core::OrderKind::kStartBuild;
    order.unit = world.units.row_ids[row];
    return true;
  }

  bool NextOrder(const core::WorldState& world, core::OrderRow& order) {
    const std::uint32_t yard = FindOfType(world, yard_type_);
    if (yard == core::kNoRow) {
      // Rings around the centre, a place further each time the last was refused.
      static constexpr std::array<std::array<float, 2>, 8> kRing = {{{1.0F, 0.0F},
                                                                     {0.0F, 1.0F},
                                                                     {-1.0F, 0.0F},
                                                                     {0.0F, -1.0F},
                                                                     {1.0F, 1.0F},
                                                                     {-1.0F, 1.0F},
                                                                     {-1.0F, -1.0F},
                                                                     {1.0F, -1.0F}}};
      const core::Vec2 centre = Centre(world);
      const auto ring = static_cast<float>(2U + (yard_attempts_ / kRing.size()));
      const std::array<float, 2>& heading = kRing[yard_attempts_ % kRing.size()];
      ++yard_attempts_;
      return Mark(yard_type_,
                  core::Vec2{.x = centre.x + (heading[0] * ring * kStepAside),
                             .y = centre.y + (heading[1] * ring * kStepAside)},
                  order);
    }
    if (world.units.rows[yard].level == 0) {
      return Start(world, yard, order);
    }
    const core::UnitId yard_id = world.units.row_ids[yard];
    const std::uint32_t mill = FindOfType(world, catalog_.sawmill_type);
    if (mill == core::kNoRow) {
      return Mark(catalog_.sawmill_type, world.units.rows[yard].position, order);
    }
    if (world.units.rows[mill].level == 0) {
      return Start(world, mill, order);
    }
    if (!HasCraftsman(world, yard_id)) {
      const std::uint32_t candidate = NextCandidate(world);
      if (candidate == core::kNoRow) {
        return false;
      }
      order.kind = core::OrderKind::kAppoint;
      order.resident = world.residents.row_ids[candidate];
      order.unit = yard_id;
      order.profession = craftsman_post_;
      return true;
    }
    const bool paused = world.units.rows[mill].paused != 0;
    const bool wanted = WantsSawing(world);
    if (wanted == !paused) {
      return false;
    }
    order.kind = wanted ? core::OrderKind::kResumeUnit : core::OrderKind::kPauseUnit;
    order.unit = world.units.row_ids[mill];
    (wanted ? resumes_ : pauses_) += 1;
    return true;
  }

  /// An adult with no post, a different one each time the last did not take.
  std::uint32_t NextCandidate(const core::WorldState& world) {
    const std::uint32_t skip = candidate_++;
    std::uint32_t seen = 0;
    for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
      const core::ResidentRow& resident = world.residents.rows[row];
      if (resident.post.profession.value != core::kInvalidDefIdValue) {
        continue;
      }
      // Signed: the start generation was born before day 0.
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
    candidate_ = 0;
    return core::kNoRow;
  }

  core::TimberCatalog catalog_;
  core::UnitTypeId yard_type_;
  core::ProfessionId craftsman_post_;
  float adult_age_years_ = 16.0F;
  float life_speedup_ = 4.0F;
  std::vector<Cost> costs_;
  bool ready_ = false;
  std::uint32_t cooldown_ = 0;
  std::uint32_t yard_attempts_ = 0;
  std::uint32_t candidate_ = 0;
  std::uint32_t pauses_ = 0;
  std::uint32_t resumes_ = 0;
  std::uint32_t days_sawing_ = 0;
  std::uint32_t sawmill_built_day_ = kNever;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_SAWMILL_POLICY_H_
