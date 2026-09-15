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
#include <functional>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_catalog/definitions.h"
#include "core_catalog/timber_catalog.h"
#include "core_common/calendar.h"
#include "core_common/labor_state.h"
#include "core_common/order_state.h"
#include "core_common/plot.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/stub_tables.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace run {

class SawmillPolicy {
 public:
  explicit SawmillPolicy(const core::ITableSet& tables) {
    std::string error;
    const bool parsed = core::ParseTimberCatalog(tables, catalog_, error);
    yard_type_ = RowId<core::UnitTypeIdTag>(tables, "unit_types", "utility_yard");
    granary_type_ = RowId<core::UnitTypeIdTag>(tables, "unit_types", "granary");
    house_type_ = RowId<core::UnitTypeIdTag>(tables, "unit_types", "wooden_house");
    craftsman_post_ = RowId<core::ProfessionIdTag>(tables, "professions", "farm_craftsman");
    adult_age_years_ = Knob(tables, "life", "adult_age_years", 16.0F);
    life_speedup_ = Knob(tables, "life", "life_speedup", 4.0F);
    ReadCosts(tables);
    std::string definitions_error;
    const bool defined =
        core::LoadDefinitions(tables, core::StubTables::kAllowed, definitions_, definitions_error);
    ready_ = parsed && defined && yard_type_.value != core::kInvalidDefIdValue &&
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
                 "YARD and its SAWMILL once the boards in the stores are fewer than the queue of "
                 "sites lacks, appoints a craftsman — and more, up to the saw's places, while "
                 "boards are short — and lets them saw only "
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

  /// @brief Where the yard and the sawmill stand at the end: level and
  /// construction phase, or that there is none — the difference between
  /// "never wanted" and "wanted and stuck".
  void ReportState(const char* run, const core::WorldState& world) const {
    const auto describe = [&world](std::uint32_t row) {
      if (row == core::kNoRow) {
        return std::string("none");
      }
      const core::UnitRow& unit = world.units.rows[row];
      std::string held;
      for (std::size_t resource = 0; resource < unit.stock.size(); ++resource) {
        if (unit.stock[resource] > 0) {
          held +=
              " r" + std::to_string(resource) + "=" + std::to_string(unit.stock[resource]) + "g";
        }
      }
      return "level " + std::to_string(unit.level) + ", phase " +
             std::to_string(static_cast<int>(unit.construction.phase)) + ", parent " +
             std::to_string(unit.parent.value) + ", holds" + (held.empty() ? " nothing" : held);
    };
    std::cout << run << ": at the end the utility yard is "
              << describe(FindOfType(world, yard_type_)) << "; the sawmill is "
              << describe(FindOfType(world, catalog_.sawmill_type)) << "\n";
  }

  /// @brief What the policy did, for the run to print at the end.
  void Report(const char* run) const {
    std::cout << run << ": the run's chairman put up the sawmill on day "
              << (sawmill_built_day_ == kNever ? std::string("never")
                                               : std::to_string(sawmill_built_day_))
              << ", paused it " << pauses_ << " times and resumed it " << resumes_
              << " times; it stood open " << days_sawing_ << " days; the yard was marked "
              << yard_attempts_ << " times\n";
  }

  /// @brief Grams of `resource` in built units that anybody may still have —
  /// what the stores hold, less what an upgrade's works hold back there. The
  /// stable's twelve tonnes of boards were counted as the village's on seed
  /// 1929 of unit_signals until they were spent, and the gate that was to keep
  /// the sawmill's boards let the houses take them (parcel 305).
  static core::Grams Held(const core::WorldState& world, core::ResourceId resource) {
    core::Grams held = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.level > 0) {
        held += core::UnreservedOf(unit, resource);
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

  /// @brief What every site of the queue still lacks of `resource`, in grams.
  core::Grams QueueNeed(const core::WorldState& world, core::ResourceId resource) const {
    core::Grams need = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      const bool queued = unit.construction.phase == core::ConstructionPhase::kMarked ||
                          unit.construction.phase == core::ConstructionPhase::kDelivering;
      const bool repair = unit.level > 0 && unit.construction.target_level == unit.level;
      if (!queued || repair) {
        continue;
      }
      const core::Grams cost = CostGrams(unit.type, unit.construction.target_level, resource);
      const core::Grams on_site =
          resource.value < unit.stock.size() ? unit.stock[resource.value] : 0;
      need += cost > on_site ? cost - on_site : 0;
    }
    const std::uint32_t rising = rise_watch_ ? rise_watch_(world) : core::kNoRow;
    if (rising < world.units.rows.size()) {
      const core::UnitRow& unit = world.units.rows[rising];
      const core::Grams cost =
          CostGrams(unit.type, static_cast<std::uint8_t>(unit.level + 1U), resource);
      const core::Grams on_site =
          resource.value < unit.stock.size() ? unit.stock[resource.value] : 0;
      need += cost > on_site ? cost - on_site : 0;
    }
    return need;
  }

  /// @brief Which row waits to rise with its step not yet taken, or kNoRow.
  using RiseWatch = std::function<std::uint32_t(const core::WorldState&)>;

  /// @brief Counts the step a unit waits to take into the queue's need.
  ///
  /// A STEP REFUSED IS NOT IN THE QUEUE, AND THE SAW WORKED ONLY FOR THE
  /// QUEUE. An upgrade is taken only with its whole recipe in the village, so
  /// the chairman's yard waiting to rise to its stable left no marked site —
  /// and the saw, working for marked sites only, stood paused. On seed 1929
  /// the stable waited three years 1 t short of its 12 t of boards with 22 t
  /// of logs in the pile, while the yard held every house back (2026-09-15).
  void SetRiseWatch(RiseWatch watch) { rise_watch_ = std::move(watch); }

  /// @brief Whether a built sawmill stands unpaused today.
  bool SawmillOpen(const core::WorldState& world) const {
    const std::uint32_t mill = FindOfType(world, catalog_.sawmill_type);
    return mill != core::kNoRow && world.units.rows[mill].level > 0 &&
           world.units.rows[mill].paused == 0;
  }

  /// @brief Boss's condition, the one the pause follows.
  bool WantsSawing(const core::WorldState& world) const {
    // THE WHOLE QUEUE'S BOARDS, AND NO LOGS KEPT BACK FROM THE SAW (boss,
    // parcel 281). The saw used to stop while the nearest site still wanted
    // logs, so the logs went round in a circle between the site and the saw
    // and both waited: 21 pauses in thirty years on seed 1929 with three house
    // sites short of boards. The logs are the felling's to bring
    // (felling_policy.h), sized to the sites and the saw together.
    const core::Grams board_need = BoardTarget(world);
    return board_need > 0 && Held(world, catalog_.board_resource) < board_need &&
           Held(world, catalog_.log_resource) > 0;
  }

  /// @brief P1, "the saw with a reserve" (boss, parcel 314): the chairman keeps
  /// boards in hand for one granary and three houses by the current recipes,
  /// not only for the sites already marked. Off unless a run asks for it.
  void KeepBoardReserve(bool keep) { keep_reserve_ = keep; }

  /// @brief The boards the saw works towards: the marked queue's need, or with
  /// the reserve on, at least one granary's and three houses' boards — read off
  /// the recipes, so a cheaper house or granary moves the reserve with it.
  core::Grams BoardTarget(const core::WorldState& world) const {
    const core::Grams queue = QueueNeed(world, catalog_.board_resource);
    if (!keep_reserve_) {
      return queue;
    }
    const core::Grams reserve = CostGrams(granary_type_, 1, catalog_.board_resource) +
                                (3 * CostGrams(house_type_, 1, catalog_.board_resource));
    return queue > reserve ? queue : reserve;
  }

  /// @brief Whether a site of `type` may start its `level` without taking the
  /// boards the sawmill itself is built of: true once the sawmill stands, for
  /// the sawmill and its yard themselves, for a step that takes no boards,
  /// and while the stores would still hold the sawmill's boards after it.
  ///
  /// THE SOURCE BEFORE ITS CONSUMERS (parcel 305). Boards come from the
  /// sawmill and nowhere else — the limit sells none — and the sawmill is
  /// built of boards. Once a start needed the whole recipe in the village
  /// (construction design §6), houses, granaries and the stable took the
  /// start's boards first, and on seed 1929 of unit_signals the sawmill's
  /// site stood for thirty years 1.2 t short of its 3 t while the village
  /// dwindled to 21. The rule that had guarded this ("the saw first while
  /// there are no boards") went out with the materials check and is back
  /// here, as a gate every building policy asks.
  bool SparesBoardsFor(const core::WorldState& world,
                       core::UnitTypeId type,
                       std::uint8_t level) const {
    if (!ready_ || type.value == catalog_.sawmill_type.value || type.value == yard_type_.value) {
      return true;
    }
    const std::uint32_t mill = FindOfType(world, catalog_.sawmill_type);
    if (mill != core::kNoRow && world.units.rows[mill].level > 0) {
      return true;
    }
    const core::Grams need = CostGrams(type, level, catalog_.board_resource);
    return need <= 0 || Held(world, catalog_.board_resource) - need >=
                            CostGrams(catalog_.sawmill_type, 1, catalog_.board_resource);
  }

  /// @brief The logs the queue's missing boards would take at the saw, grams:
  /// what the felling must bring for the saw on top of the sites' own logs.
  core::Grams LogsForMissingBoards(const core::WorldState& world) const {
    const core::Grams short_boards = BoardTarget(world) - Held(world, catalog_.board_resource);
    if (short_boards <= 0 || catalog_.board_grams_per_m3 <= 0) {
      return 0;
    }
    const float board_m3 =
        static_cast<float>(short_boards) / static_cast<float>(catalog_.board_grams_per_m3);
    return core::LogGramsForBoardM3(catalog_, board_m3);
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

  std::uint32_t Craftsmen(const core::WorldState& world, core::UnitId yard) const {
    std::uint32_t held = 0;
    for (const core::ResidentRow& resident : world.residents.rows) {
      held += resident.post.profession.value == craftsman_post_.value &&
                      resident.post.unit.value == yard.value
                  ? 1U
                  : 0U;
    }
    return held;
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

  float YardRadius() const {
    const std::vector<float>& radii = definitions_.units.keep_out_radius_m;
    return yard_type_.value < radii.size() ? radii[yard_type_.value] : 0.0F;
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
    // NOT BEFORE THE BOARDS RUN SHORT (boss, parcel 208). The first version
    // put the yard and the saw up on day one and took the craftsman off the
    // fields for good, years before the start's 82 m3 of boards ran out; on
    // seed 1931 that turned three separate failed years into a run of three
    // and the obvious chairman into the dock.
    //
    // AND WHILE THE SAWMILL CAN STILL BE BUILT: it takes boards itself (five
    // cubic metres at level 1). The first "by the boards" version waited for
    // the boards to run short, and then the sawmill's own site waited for the
    // boards it was built to make — never built on any seed (2026-09-13). So
    // the yard goes up once the stores hold fewer boards than the queue lacks
    // AND the sawmill costs.
    //
    // PLUS ONE GRANARY'S BOARDS OF SLACK. Measured the same night: with the
    // sawmill's own cost alone the yard went up on time and the sawmill's
    // site then stood holding its steel for twenty years, because the four
    // days it takes to mark and start the yard and the saw are four days of
    // deliveries to the granary sites marked before it, which come first in
    // row order and took the last boards.
    const core::Grams sawmill_boards = CostGrams(catalog_.sawmill_type, 1, catalog_.board_resource);
    const core::Grams slack = CostGrams(granary_type_, 1, catalog_.board_resource);
    if (yard == core::kNoRow && FindOfType(world, catalog_.sawmill_type) == core::kNoRow &&
        Held(world, catalog_.board_resource) >=
            QueueNeed(world, catalog_.board_resource) + sawmill_boards + slack) {
      return false;
    }
    if (yard == core::kNoRow) {
      // THE NEAREST FREE PLACE BY THE CORE'S OWN RULE (plot.h, FreePlot), not
      // rings guessed around the centre. The rings found a place on day one
      // and none in a grown village: put up late, the yard was marked forty
      // times on seed 1931 and refused for crowding every time (2026-09-13).
      ++yard_attempts_;
      return Mark(yard_type_,
                  core::FreePlot(world.units, definitions_.Plots(), Centre(world), YardRadius()),
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
    // R5, ANYONE SAWS (boss, parcel 316): the two-handed saw is a peasant's
    // winter work, not a trade. No craftsman is appointed; the chairman sends
    // adults to the saw by a standing order, no more than its places and the
    // tools in the stores, and takes them back when the boards are enough.
    if (saw_by_anyone_) {
      return NextAnyoneOrder(world, mill, order);
    }
    // AS MANY CRAFTSMEN AS THE SAW HAS PLACES while the boards are short
    // (2026-09-14). One was the whole policy while granaries were the only
    // sites; once houses stopped coming from nothing, three house sites on
    // seed 1929 stood with their logs, clay and straw in and 0-532 kg of the
    // 2.4 t of boards each, behind a saw one man worked.
    if (!HasCraftsman(world, yard_id) ||
        (Craftsmen(world, yard_id) < static_cast<std::uint32_t>(catalog_.sawyers_max) &&
         WantsSawing(world))) {
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

  /// The standing orders on the sawmill, by the resident each names.
  static std::vector<core::ResidentId> SawOrders(const core::WorldState& world, core::UnitId mill) {
    std::vector<core::ResidentId> sawyers;
    for (const core::OrderRow& standing : world.orders.rows) {
      if (standing.kind == core::OrderKind::kAssignWork &&
          standing.status == core::OrderStatus::kAccepted &&
          standing.work == core::WorkKind::kUnitWork && standing.unit.value == mill.value) {
        sawyers.push_back(standing.resident);
      }
    }
    return sawyers;
  }

  /// R5's day: the saw paused or open as boards are wanted, adults sent up to
  /// the places and the tools while wanted, and sent back when not.
  bool NextAnyoneOrder(const core::WorldState& world, std::uint32_t mill, core::OrderRow& order) {
    const core::UnitId mill_id = world.units.row_ids[mill];
    const bool wanted = WantsSawing(world);
    const bool paused = world.units.rows[mill].paused != 0;
    if (wanted == paused) {
      order.kind = wanted ? core::OrderKind::kResumeUnit : core::OrderKind::kPauseUnit;
      order.unit = mill_id;
      (wanted ? resumes_ : pauses_) += 1;
      return true;
    }
    const std::vector<core::ResidentId> sawyers = SawOrders(world, mill_id);
    if (!wanted) {
      if (sawyers.empty()) {
        return false;
      }
      order.kind = core::OrderKind::kReleaseWork;
      order.resident = sawyers.front();
      return true;
    }
    const core::Grams tool_grams = Held(world, catalog_.tool_resource);
    const auto tools = catalog_.tool_grams > 0
                           ? static_cast<std::uint32_t>(tool_grams / catalog_.tool_grams)
                           : static_cast<std::uint32_t>(catalog_.sawyers_max);
    const auto places = static_cast<std::uint32_t>(catalog_.sawyers_max);
    if (sawyers.size() >= places || sawyers.size() >= tools) {
      return false;
    }
    const std::uint32_t candidate = NextCandidate(world);
    if (candidate == core::kNoRow) {
      return false;
    }
    order.kind = core::OrderKind::kAssignWork;
    order.resident = world.residents.row_ids[candidate];
    order.work = core::WorkKind::kUnitWork;
    order.unit = mill_id;
    return true;
  }

 public:
  /// @brief R5 (boss, parcel 316): adults saw by standing order, no craftsman.
  void SawByAnyone(bool anyone) { saw_by_anyone_ = anyone; }

 private:
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
  /// The plot radii and the map, for FreePlot. Owned here: PlotRules is a
  /// span into it.
  core::Definitions definitions_;
  core::UnitTypeId yard_type_;
  core::UnitTypeId granary_type_;

  core::UnitTypeId house_type_;

  bool keep_reserve_ = false;

  RiseWatch rise_watch_;

  bool saw_by_anyone_ = false;
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
