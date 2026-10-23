/// @file
/// @brief THE FALLOW PLOUGHED AS THE EATERS GROW (#725; econ,
///        fields-grow-with-eaters-2026-10-12 §2; boss,
///        core-boss-725-fallow-2026-10-16 [2]): the run's chairman opens the
///        derelict ground block by block when next year's food per eater
///        falls under the village's need.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THE RUN ORDERS THIS. The canon sowed the same 70 ha for fifteen years
/// while its people grew fivefold; the 90 ha of derelict ground were never
/// ploughed, because no policy ever told a field to grow. The design expects
/// the fields to grow («к третьей эпохе разворачивается на всю карту»), and by
/// the human's 2 October word the bots play well.
///
/// THE RULE, once a year on the year's first day (the winter, before the
/// spring's seed is set aside):
///   * next year's food kcal: for every crop the fields' coming slot sows, its
///     area x its mean yield over the last three closed years (the policy's
///     own book; a crop with none yet reads the table's yield) NET of the
///     closed year's losses (rot, the heap's, the snow's, over the harvest;
///     boss [3] (a): a gross forecast read plenty in a hungry village), less
///     the plan's due the coming year will carry (the largest position named
///     last year x the worked ha the turn now prices on ÷ the ha it was priced
///     on: PlanPositionGrams is linear in the worked ha; econ 7f20b3f, Reading
///     A of core-boss-725 [8] - the honest forecast, not a brake), less its seed
///     (sowing norm x area). A crop counts by what the residents EAT of it:
///     a resource out of the issue bundle (food.csv issue_kg_per_trudoden
///     blank: oat and pea, which the horses eat) counts nought, and stays in
///     the chain as feed (econ's condition, 16 October 2026);
///   * the need: the core's own village need (NeedUntilHarvest's
///     village_need_kcal_per_day, by age as the meal counts it) x 48 days x
///     (1 + last year's net growth);
///   * under it, the next block's chains for spring: barley -> rye_winter ->
///     oat, a GRAIN chain, no potato (econ's (4), bc22032: every red of the
///     first pair, the snow, the potato's failed delivery and the hauling out
///     of the building, passed through the block's potato; boss [9]). The
///     opening year's three fields start on the SPRING slots only, barley,
///     oat, barley (kOpeningSlot; econ 2f7c7d7): a field set to winter rye
///     stood bare its first summer while its hectares already raised the rye
///     position - the smoke village 1929 failed year 5 on rye so. The rye
///     follows barley's August harvest from year 2. At most one block a year,
///     never taken back. A block is three derelict fields; the first is the
///     three nearest a standing grain store (granary or church store), the
///     second the rest.

#ifndef TESTS_RUN_COMMON_FALLOW_POLICY_H_
#define TESTS_RUN_COMMON_FALLOW_POLICY_H_

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_catalog/seed_norm_catalog.h"
#include "core_common/calendar.h"
#include "core_common/fund_ladder.h"
#include "core_common/land_state.h"
#include "core_common/logistics_state.h"
#include "core_common/order_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"
#include "fallow_seed_play.h"

namespace run {

/// @brief Opens a derelict block a year while food per eater runs short.
class FallowPolicy {
 public:
  explicit FallowPolicy(const core::ITableSet& tables) { ReadTables(tables); }

  /// @brief The run says so before it measures.
  static void Declare(std::string_view run_name) {
    std::cout << run_name
              << ": FIXTURE DIFFERS FROM THE START CANON — the run's chairman ploughs the "
                 "derelict ground three fields (45 ha) at a time, one block a year at most, when "
                 "next year's food per eater falls under the village's need (#725; econ "
                 "fields-grow-with-eaters); chain barley -> rye_winter -> oat, the opening "
                 "year's three fields on barley, oat, barley, no potato; the "
                 "forecast takes off the due at the worked ha the turn prices on; oat and pea "
                 "count nought as food (the meal does not eat them)\n";
  }

  /// @brief One day of the chairman's attention. Call once a day.
  void RunDay(core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    if (derelict_.empty() && !derelict_read_) {
      ReadDerelict(world);
    }
    WatchTheDue(world);
    const std::uint32_t day_of_year = world.calendar.day % core::kDaysPerYear;
    HouseSuppliesOverFields(simulation, world, day_of_year);
    // (f), the seed play: decided from the year's second day, when the turn
    // below has booked last year's yields (day 0's turn runs after this).
    const bool read_book = day_of_year >= 1 && day_of_year < 3 * core::kDaysPerMonth;
    seed_play_.RunDay(simulation,
                      world,
                      opened_blocks_ > 0,
                      read_book ? SeedBook(world) : std::vector<SeedPlayPosition>{});
    if (day_of_year == kFirstOfDecember) {
      CountPotatoOnTheBlocks(world);
    }
    if (day_of_year == core::kDaysPerYear - 1U) {
      PrintTheEvesSeed(world);
    }
    if (world.calendar.day == 0 || day_of_year != 0 || world.calendar.day == last_turn_day_) {
      return;
    }
    last_turn_day_ = world.calendar.day;
    BookLastYear(world);
    PrintLossShares(world);
    PrintPotatoBook(world);
    const double forecast = ForecastFoodKcal(world);
    const double need = NeedKcal(simulation, world);
    const std::uint32_t year = world.ledger.closed.year + 1U;
    std::cout << "fallow_policy: year " << year << " forecast "
              << std::llround(forecast / kKcalPerGcal) << " Gcal of food against a need of "
              << std::llround(need / kKcalPerGcal) << " Gcal";
    if (!(need > 0.0) || forecast >= need || opened_blocks_ >= kBlocks) {
      std::cout << (opened_blocks_ >= kBlocks ? " - every block open\n" : " - enough\n");
      return;
    }
    std::cout << " - SHORT\n";
    if (kPlanGuardOn && !BlockKeepsThePlan(world, NextBlockFields(world))) {
      std::cout << "fallow_policy: year " << year << " block " << opened_blocks_ + 1U
                << " HELD by the plan guard\n";
      return;
    }
    OpenNextBlock(simulation, world);
    std::cout << "fallow_policy: year " << year << " block " << opened_blocks_
              << " opened for this spring\n";
    opened_in_year_.push_back(year);
  }

  /// @brief The pair's prints: the blocks by year, the block's potato seed,
  ///        its potato on the field on 1 December, its fertility now.
  void Report(const core::WorldState& world) const {
    std::cout << "fallow_policy: " << opened_blocks_ << " of " << kBlocks
              << " blocks opened, in years:";
    for (const std::uint32_t year : opened_in_year_) {
      std::cout << ' ' << year;
    }
    std::cout << (opened_in_year_.empty() ? " none" : "") << '\n';
    for (const auto& [year, tonnes] : block_potato_seed_t_) {
      std::cout << "fallow_policy: year " << year << " block potato seed taken " << tonnes
                << " t\n";
    }
    for (const auto& [year, tonnes] : block_potato_on_field_t_) {
      std::cout << "fallow_policy: year " << year << " block potato on the field on 1 December "
                << tonnes << " t\n";
    }
    for (const core::FieldId field : opened_fields_) {
      const std::uint32_t row = core::FindRow(world.fields, field);
      if (row != core::kNoRow) {
        std::cout << "fallow_policy: block field " << field.value << " fertility "
                  << world.fields.rows[row].fertility << " at year " << world.ledger.closed.year
                  << "'s end\n";
      }
    }
    std::cout << "fallow_policy: house supplies set over the fields in the blocks' spring: "
              << houses_over_fields_ << '\n';
    seed_play_.Report();
  }

  /// @brief (f)'s district-hour entry (fallow_seed_play.h): the run calls it
  ///        at an hour the chairman may be in the district - noon - every
  ///        day; it stages only the plan trade the day entry decided.
  void RunDistrictHour(core::ISimulation& simulation) { seed_play_.RunDistrictHour(simulation); }

  /// @brief The fields opened so far, for a run's own prints.
  const std::vector<core::FieldId>& OpenedFields() const { return opened_fields_; }

 private:
  /// THE BLOCKS' SPRING DOES NOT TAKE THE HOUSES' TEAMS (boss, core-boss-725-
  /// fallow [34]; econ [32]'s spring form, chosen by the one-day print [33]:
  /// her condition would open a block in 7 of 45 village-years). The binder
  /// print h725 [36] found the cut in TEAMS, not hands: in April-May of y4-y8
  /// the arm's carts to sites fell 44 % / 27 % and off the stands 83 % / 70 %,
  /// sites in delivery rose 44 % / 72 % with their logs awaiting +67 % / +92 %,
  /// «no horse» idling rose, and 10-15 thousand asked residents stood idle in
  /// both arms. So once a block is open, in April-June every house site's
  /// supply is set «важнее полевых работ» (kSetLogisticsLevel with the switch,
  /// the door supply_policy.h uses after a week on the floor): its loads stand
  /// ahead of every windowed job, the blocks' ploughing among them, and the
  /// blocks take the teams the houses' carts leave. The switch is the task's
  /// own and goes out with the works.
  void HouseSuppliesOverFields(core::ISimulation& simulation,
                               const core::WorldState& world,
                               std::uint32_t day_of_year) {
    const std::uint32_t month = day_of_year / core::kDaysPerMonth;
    if (opened_blocks_ == 0 || month < kBlockSpringFromMonth || month > kBlockSpringToMonth) {
      return;
    }
    std::vector<std::uint16_t> house_types;
    for (const core::FamilyRow& family : world.families.rows) {
      const std::uint32_t house = core::FindRow(world.units, family.house);
      if (house != core::kNoRow) {
        house_types.push_back(world.units.rows[house].type.value);
      }
    }
    std::vector<core::OrderRow> orders;
    for (std::uint32_t row = 0; row < world.logistics_tasks.rows.size(); ++row) {
      const core::LogisticsTaskRow& task = world.logistics_tasks.rows[row];
      if (task.load_kind != core::LogisticsLoadKind::kSiteSupply || task.paused ||
          task.over_fields) {
        continue;
      }
      const std::uint32_t unit_row = core::FindRow(world.units, task.unit);
      if (unit_row == core::kNoRow) {
        continue;
      }
      const core::UnitRow& unit = world.units.rows[unit_row];
      if (std::ranges::find(house_types, unit.type.value) == house_types.end() ||
          !core::AnythingAwaits(unit.construction)) {
        continue;
      }
      core::OrderRow order;
      order.kind = core::OrderKind::kSetLogisticsLevel;
      order.logistics_task = world.logistics_tasks.row_ids[row];
      order.logistics_level = task.base_level;
      order.enable = 1;
      orders.push_back(order);
    }
    if (!orders.empty()) {
      simulation.StageOrders(std::span<const core::OrderRow>(orders), {});
      houses_over_fields_ += static_cast<std::uint32_t>(orders.size());
    }
  }

  /// The blocks' spring, 0-based months: April to June, where the binder
  /// print measured the teams taken (core-boss-725-fallow [36]).
  static constexpr std::uint32_t kBlockSpringFromMonth = 3;
  static constexpr std::uint32_t kBlockSpringToMonth = 5;
  std::uint32_t houses_over_fields_ = 0;

  /// (f) THE PROJECTED BOOK (econ, fields-grow-with-eaters «24 Oct ~03:00»):
  /// for every plan position a field crop yields, grams - stock now, less this
  /// spring's seed (spring crops in this year's slot), plus this year's
  /// harvest (hectares x the three closed years' yield, net of last year's
  /// loss share: §2's own forecast, YieldKgPerHa), less last year's issue,
  /// feed, rot and theft scaled to this year's eaters (1 + last year's net
  /// growth, as NeedKcal); against the due plus next year's seed of every
  /// field with the crop in next year's slot, the block included.
  std::vector<SeedPlayPosition> SeedBook(const core::WorldState& world) const {
    std::vector<SeedPlayPosition> out;
    const core::YearLedger& closed = world.ledger.closed;
    const auto residents = static_cast<double>(world.residents.rows.size());
    const double net =
        static_cast<double>(closed.births) + closed.arrivals - closed.deaths - closed.departures;
    const double eaters = 1.0 + (residents > 0.0 ? std::max(0.0, net / residents) : 0.0);
    const auto at = [](const core::ResourceAmounts& amounts, std::size_t resource) {
      return resource < amounts.size() ? static_cast<double>(amounts[resource]) : 0.0;
    };
    for (std::size_t resource = 0; resource < world.plan.due.size(); ++resource) {
      if (world.plan.due[resource] <= 0 || resource >= crop_resource_.size() ||
          !crop_resource_[resource]) {
        continue;
      }
      SeedPlayPosition position;
      position.resource = core::ResourceId{static_cast<std::uint16_t>(resource)};
      position.key = resource_keys_[resource];
      double stock = 0.0;
      for (const core::UnitRow& unit : world.units.rows) {
        stock += at(unit.stock, resource);
      }
      double spring_seed = 0.0;
      double harvest = 0.0;
      double next_seed = 0.0;
      for (const core::FieldRow& field : world.fields.rows) {
        if (field.kind != core::LandKind::kArable) {
          continue;
        }
        const double area = static_cast<double>(field.area_ga);
        const core::CropId now = field.rotation_year0;
        if (now.value < crops_.size() && crops_[now.value].resource == resource) {
          const double kept = resource < loss_share_.size() ? 1.0 - loss_share_[resource] : 1.0;
          harvest += YieldKgPerHa(now) * kept * area * kGramsPerKilogram;
          if (field.sown_day == core::kNeverSownDay ||
              field.sown_day / core::kDaysPerYear != world.calendar.day / core::kDaysPerYear) {
            spring_seed += crops_[now.value].seed_kg_per_ha * area * kGramsPerKilogram *
                           (crops_[now.value].winter ? 0.0 : 1.0);
          }
          position.crop = now;
        }
        const core::CropId next = field.rotation_year1;
        if (next.value < crops_.size() && crops_[next.value].resource == resource) {
          next_seed += crops_[next.value].seed_kg_per_ha * area * kGramsPerKilogram;
          position.crop = position.crop.value == core::kInvalidDefIdValue ? next : position.crop;
        }
      }
      const double used = (at(closed.issued, resource) + at(closed.feed, resource) +
                           at(closed.spoiled, resource) + at(closed.stolen, resource)) *
                          eaters;
      position.projected_g = stock - spring_seed + harvest - used;
      position.need_g = static_cast<double>(world.plan.due[resource]) + next_seed;
      out.push_back(position);
    }
    return out;
  }

  SeedPlay seed_play_;

  static constexpr std::uint32_t kFieldsPerBlock = 3;
  static constexpr std::uint32_t kBlocks = 2;
  /// Where in chain_ (barley, rye_winter, oat) each of a block's three fields
  /// starts: barley, oat, barley - spring crops only in the opening year.
  static constexpr std::array<std::size_t, 3> kOpeningSlot = {0, 2, 0};
  /// STUB 1.10 (econ 21508ba): the plan guard's margin over the due.
  static constexpr double kPlanGuardMargin = 1.10;
  /// THE PLAN GUARD IS OFF (boss, core-boss-725-fallow [22]; econ [21]):
  /// pairs 3 and 4 showed the guards (b) and (b′) never lowered the due -
  /// holding a block only withheld its opening year's oat - and those pairs
  /// ran before the plan was split by the year's sown mix. Pair 2's arm,
  /// blocks open with no guard, is re-measured on 0.37.320. The guard and
  /// its eve print stay in the tree until that pair reads.
  static constexpr bool kPlanGuardOn = false;
  static constexpr std::uint32_t kHistoryYears = 3;
  static constexpr std::uint32_t kFirstOfDecember =
      static_cast<std::uint32_t>(core::Month::kDecember) * core::kDaysPerMonth;
  static constexpr double kKcalPerGcal = 1e6;

  struct CropRow {
    std::uint32_t resource = core::kNoTableRow;
    double table_yield_kg_per_ha = 0.0;
    double seed_kg_per_ha = 0.0;
    bool winter = false;  ///< crops.csv is_winter: sown in the autumn, not from the spring's seed.
  };

  void ReadTables(const core::ITableSet& tables) {
    const core::ITable* const crops = tables.FindTable("crops");
    const core::ITable* const resources = tables.FindTable("resources");
    const core::ITable* const food = tables.FindTable("food");
    const core::ITable* const types = tables.FindTable("unit_types");
    if (crops == nullptr || resources == nullptr || food == nullptr || types == nullptr) {
      return;
    }
    const std::uint32_t resource_col = crops->FindColumn("resource");
    const std::uint32_t yield_col = crops->FindColumn("yield_kg_per_ha");
    const std::uint32_t seed_col = crops->FindColumn("sowing_norm_kg_per_ha");
    crops_.resize(crops->RowCount());
    for (std::uint32_t row = 0; row < crops->RowCount(); ++row) {
      CropRow& crop = crops_[row];
      crop.resource = resources->FindRowByKey(crops->CellText(row, resource_col));
      crop.table_yield_kg_per_ha =
          static_cast<double>(crops->CellReal(row, yield_col).value_or(0.0F));
      crop.seed_kg_per_ha = static_cast<double>(crops->CellReal(row, seed_col).value_or(0.0F));
      crop.winter = crops->CellReal(row, crops->FindColumn("is_winter")).value_or(0.0F) > 0.0F;
    }
    // EATEN: a resource in the issue bundle, at its kilocalories. Oat and pea
    // carry kilocalories and no issue norm: the families never get them.
    const std::uint32_t kcal_col = food->FindColumn("kcal_per_gram");
    const std::uint32_t issue_col = food->FindColumn("issue_kg_per_trudoden");
    eaten_kcal_per_gram_.assign(resources->RowCount(), 0.0F);
    for (std::uint32_t row = 0; row < food->RowCount(); ++row) {
      const std::uint32_t resource = resources->FindRowByKey(food->CellText(row, 0));
      const float issue = food->CellReal(row, issue_col).value_or(0.0F);
      if (resource < eaten_kcal_per_gram_.size() && issue > 0.0F) {
        eaten_kcal_per_gram_[resource] = food->CellReal(row, kcal_col).value_or(0.0F);
      }
    }
    for (const std::string_view key : {"granary", "church_store"}) {
      const std::uint32_t row = types->FindRowByKey(key);
      if (row != core::kNoTableRow) {
        grain_stores_.push_back(row);
      }
    }
    const std::array<std::string_view, 3> chain_keys = {"barley", "rye_winter", "oat"};
    for (std::size_t slot = 0; slot < chain_keys.size(); ++slot) {
      const std::uint32_t row = crops->FindRowByKey(chain_keys[slot]);
      chain_[slot] =
          row == core::kNoTableRow ? core::CropId{} : core::CropId{static_cast<std::uint16_t>(row)};
    }
    potato_resource_ = resources->FindRowByKey("potato");
    crop_resource_.assign(resources->RowCount(), false);
    for (const CropRow& crop : crops_) {
      if (crop.resource < crop_resource_.size()) {
        crop_resource_[crop.resource] = true;
      }
    }
    std::string error;
    if (!core::ReadSeedNorms(*crops, resources, seed_norms_, error)) {
      std::cout << "fallow_policy: REFUSED: seed norms - " << error << '\n';
      seed_norms_.clear();
    }
    history_.assign(resources->RowCount(), {});
    for (std::uint32_t row = 0; row < resources->RowCount(); ++row) {
      resource_keys_.emplace_back(resources->CellText(row, 0));
    }
  }

  /// The derelict ground of the genesis: overgrown, and never told to grow.
  void ReadDerelict(const core::WorldState& world) {
    derelict_read_ = true;
    for (std::uint32_t row = 0; row < world.fields.rows.size(); ++row) {
      const core::FieldRow& field = world.fields.rows[row];
      const bool no_chain = field.rotation_year0.value == core::kInvalidDefIdValue &&
                            field.rotation_year1.value == core::kInvalidDefIdValue &&
                            field.rotation_year2.value == core::kInvalidDefIdValue;
      if (field.kind == core::LandKind::kArable && field.overgrown != 0 && no_chain) {
        derelict_.push_back(world.fields.row_ids[row]);
      }
    }
  }

  /// The largest position of each resource named over the year that closes,
  /// and the worked ha it was priced on.
  void WatchTheDue(const core::WorldState& world) {
    if (world.calendar.day % core::kDaysPerYear == 0 && world.calendar.day != watched_turn_day_) {
      watched_turn_day_ = world.calendar.day;
      due_last_year_ = due_this_year_;
      due_this_year_.assign(due_this_year_.size(), 0);
      priced_ha_last_year_ = priced_ha_this_year_;
      priced_ha_this_year_ = 0.0;
    }
    const auto grow = [](std::vector<core::Grams>& into, const core::ResourceAmounts& from) {
      if (into.size() < from.size()) {
        into.resize(from.size(), 0);
      }
      for (std::size_t index = 0; index < from.size(); ++index) {
        into[index] = std::max(into[index], from[index]);
      }
    };
    grow(due_this_year_, world.plan.due);
    // The ha the year's figures were priced on: the ratchet as it stood while
    // the district named them (PlanState::worked_ha_last_year).
    if (world.plan.announced != 0) {
      priced_ha_this_year_ = static_cast<double>(world.plan.worked_ha_last_year);
    }
  }

  /// The due the coming year will carry, by resource: last year's largest
  /// position x the worked ha the turn now prices on ÷ the ha it was priced
  /// on (Reading A). 1 before a year has been priced.
  double DueScale(const core::WorldState& world) const {
    const auto now_ha = static_cast<double>(world.plan.worked_ha_last_year);
    return priced_ha_last_year_ > 0.0 && now_ha > 0.0 ? now_ha / priced_ha_last_year_ : 1.0;
  }

  /// THE POTATO'S BOOK BY YEAR (boss [9]): the position named, shipped, the
  /// harvest, and the coming year's due as the forecast takes it. The blocks
  /// sow no potato, so the harvest is the old ground's; the block's potato ha
  /// are printed to show it stays nought.
  void PrintPotatoBook(const core::WorldState& world) const {
    if (potato_resource_ == core::kNoTableRow) {
      return;
    }
    const auto at = [this](const std::vector<core::Grams>& amounts) {
      return potato_resource_ < amounts.size()
                 ? static_cast<double>(amounts[potato_resource_]) / 1e6
                 : 0.0;
    };
    double block_potato_ha = 0.0;
    for (const core::FieldId id : opened_fields_) {
      const std::uint32_t row = core::FindRow(world.fields, id);
      if (row != core::kNoRow && world.fields.rows[row].last_crop.value < crops_.size() &&
          crops_[world.fields.rows[row].last_crop.value].resource == potato_resource_) {
        block_potato_ha += static_cast<double>(world.fields.rows[row].area_ga);
      }
    }
    // The judged pair from the closed year's book (YearLedger plan_due /
    // plan_delivered, booked at the turn): a daily watch of PlanState never
    // sees the delivery, which the turn ships and clears in one step.
    std::cout << "fallow_policy: year " << world.ledger.closed.year << " potato due "
              << at(world.ledger.closed.plan_due) << " t, delivered "
              << at(world.ledger.closed.plan_delivered) << " t, harvested "
              << at(world.ledger.closed.harvest) << " t (block potato " << block_potato_ha
              << " ha); priced on " << priced_ha_last_year_ << " ha, the turn prices on "
              << world.plan.worked_ha_last_year << " ha: the coming due x" << DueScale(world)
              << " = " << at(due_last_year_) * DueScale(world) << " t\n";
    // EVERY POSITION, short ones named: the grain block raises the rye and
    // the oat positions too (the district prices every position off the total
    // worked ha), and the smoke village 1929 failed year 5 on rye, not potato.
    const core::YearLedger& closed = world.ledger.closed;
    std::cout << "fallow_policy: year " << closed.year << " plan by position (t):";
    for (std::size_t resource = 0; resource < closed.plan_due.size(); ++resource) {
      const core::Grams due = closed.plan_due[resource];
      if (due <= 0) {
        continue;
      }
      const core::Grams shipped =
          resource < closed.plan_delivered.size() ? closed.plan_delivered[resource] : 0;
      std::cout << ' ' << resource_keys_[resource] << ' ' << static_cast<double>(shipped) / 1e6
                << '/' << static_cast<double>(due) / 1e6 << (shipped < due ? " SHORT" : "");
    }
    std::cout << '\n';
  }

  /// The closed year's yield of each resource, kg/ha: its harvest over the
  /// area that reaped it (the fields' last crop).
  void BookLastYear(const core::WorldState& world) {
    std::vector<double> hectares(history_.size(), 0.0);
    for (const core::FieldRow& field : world.fields.rows) {
      if (field.last_crop.value < crops_.size()) {
        const std::uint32_t resource = crops_[field.last_crop.value].resource;
        if (resource < hectares.size()) {
          hectares[resource] += static_cast<double>(field.area_ga);
        }
      }
    }
    const core::YearLedger& closed = world.ledger.closed;
    const core::ResourceAmounts& harvest = closed.harvest;
    loss_share_.assign(history_.size(), 0.0);
    for (std::size_t resource = 0; resource < history_.size(); ++resource) {
      const core::Grams grams = resource < harvest.size() ? harvest[resource] : 0;
      // WHAT DID NOT REACH THE MOUTH (#725, boss [3] (a)): the closed year's
      // rot in the stores, the heap's loss and the snow's, over its harvest.
      const auto lost = [resource](const core::ResourceAmounts& amounts) {
        return resource < amounts.size() ? static_cast<double>(amounts[resource]) : 0.0;
      };
      if (grams > 0) {
        loss_share_[resource] = std::clamp(
            (lost(closed.spoiled) + lost(closed.lost_no_room) + lost(closed.lost_to_snow)) /
                static_cast<double>(grams),
            0.0,
            1.0);
      }
      if (hectares[resource] > 0.0 && grams > 0) {
        std::vector<double>& years = history_[resource];
        years.push_back(static_cast<double>(grams) / kGramsPerKilogram / hectares[resource]);
        if (years.size() > kHistoryYears) {
          years.erase(years.begin());
        }
      }
    }
    // The block's potato seed of the closed year, as the pair prints it.
    if (potato_resource_ < history_.size()) {
      double seed_kg = 0.0;
      for (const core::FieldId id : opened_fields_) {
        const std::uint32_t row = core::FindRow(world.fields, id);
        if (row == core::kNoRow) {
          continue;
        }
        const core::FieldRow& field = world.fields.rows[row];
        if (field.last_crop.value < crops_.size() &&
            crops_[field.last_crop.value].resource == potato_resource_) {
          seed_kg +=
              crops_[field.last_crop.value].seed_kg_per_ha * static_cast<double>(field.area_ga);
        }
      }
      if (!opened_fields_.empty()) {
        block_potato_seed_t_.emplace_back(world.ledger.closed.year, seed_kg / 1000.0);
      }
    }
  }

  /// The loss share the forecast takes off, by eaten resource (boss [4]): a
  /// block opened for losses the year-1 clamp now cuts reads as such here.
  void PrintLossShares(const core::WorldState& world) const {
    std::cout << "fallow_policy: year " << world.ledger.closed.year << " loss share";
    for (std::size_t resource = 0; resource < loss_share_.size(); ++resource) {
      if (eaten_kcal_per_gram_[resource] > 0.0F && !history_[resource].empty()) {
        std::cout << ' ' << resource_keys_[resource] << '='
                  << std::lround(loss_share_[resource] * 100.0) << '%';
      }
    }
    std::cout << '\n';
  }

  double YieldKgPerHa(core::CropId crop) const {
    const CropRow& row = crops_[crop.value];
    if (row.resource < history_.size() && !history_[row.resource].empty()) {
      double sum = 0.0;
      for (const double kg : history_[row.resource]) {
        sum += kg;
      }
      return sum / static_cast<double>(history_[row.resource].size());
    }
    return row.table_yield_kg_per_ha;
  }

  /// Next year's eaten kilocalories off the fields' coming slot.
  double ForecastFoodKcal(const core::WorldState& world) const {
    const std::vector<double> grams = ForecastGrams(world);
    double kcal = 0.0;
    const double scale = DueScale(world);
    for (std::size_t resource = 0; resource < grams.size(); ++resource) {
      const double due = resource < due_last_year_.size()
                             ? static_cast<double>(due_last_year_[resource]) * scale
                             : 0.0;
      const double left = std::max(0.0, grams[resource] - due);
      kcal += left * static_cast<double>(eaten_kcal_per_gram_[resource]);
    }
    return kcal;
  }

  /// Next year's grams of every crop's resource off the fields' coming slot,
  /// net of last year's loss share, less the seed.
  std::vector<double> ForecastGrams(const core::WorldState& world) const {
    std::vector<double> grams(eaten_kcal_per_gram_.size(), 0.0);
    for (const core::FieldRow& field : world.fields.rows) {
      const core::CropId crop = field.rotation_year0;
      if (field.kind != core::LandKind::kArable || crop.value >= crops_.size()) {
        continue;
      }
      const CropRow& row = crops_[crop.value];
      if (row.resource >= grams.size()) {
        continue;
      }
      const double kept = row.resource < loss_share_.size() ? 1.0 - loss_share_[row.resource] : 1.0;
      grams[row.resource] += ((YieldKgPerHa(crop) * kept) - row.seed_kg_per_ha) *
                             static_cast<double>(field.area_ga) * kGramsPerKilogram;
    }
    return grams;
  }

  /// The village's need for a year, by the core's own count, with last
  /// year's net growth on top.
  double NeedKcal(core::ISimulation& simulation, const core::WorldState& world) const {
    double per_day = 0.0;
    for (std::size_t resource = 0; resource < eaten_kcal_per_gram_.size() && per_day <= 0.0;
         ++resource) {
      if (eaten_kcal_per_gram_[resource] <= 0.0F) {
        continue;
      }
      const std::optional<core::HarvestNeed> need =
          simulation.NeedUntilHarvest(core::ResourceId{static_cast<std::uint16_t>(resource)});
      per_day = need.has_value() ? static_cast<double>(need->village_need_kcal_per_day) : 0.0;
    }
    const auto residents = static_cast<double>(world.residents.rows.size());
    const core::YearLedger& closed = world.ledger.closed;
    const double net =
        static_cast<double>(closed.births) + closed.arrivals - closed.deaths - closed.departures;
    const double growth = residents > 0.0 ? std::max(0.0, net / residents) : 0.0;
    return per_day * static_cast<double>(core::kDaysPerYear) * (1.0 + growth);
  }

  /// THE PLAN'S GUARD ON A BLOCK (#725 pair 3; econ 913ed56, (b)): the block
  /// is opened only if, for every position the district prices off the
  /// worked ha, the due at the ha WITH the block stays under what the old
  /// ground's fields will give of it, net of loss and less seed. A position
  /// with no crop behind it (milk) is not priced by ha and is not asked.
  /// Prints every position it read, and which one held the block.
  ///
  /// (b′), pair 4 (econ 21508ba; boss): the harvest side is the coming year's
  /// NET harvest of every field WITH the block (yield × (1 − last year's loss
  /// share) × area, the block on its opening slots), less NEXT spring's seed of
  /// every field with the block (each field's second slot, the seed the eve
  /// holds - pair 3's smoke found every failed position SEED-BOUND on its
  /// eve), and the due at the new ha takes kPlanGuardMargin on top.
  bool BlockKeepsThePlan(const core::WorldState& world,
                         const std::vector<core::FieldId>& block) const {
    const double block_ha = AreaOf(world, block);
    std::vector<double> harvest(eaten_kcal_per_gram_.size(), 0.0);
    std::vector<double> seed_next(eaten_kcal_per_gram_.size(), 0.0);
    const auto add = [&](core::CropId now, core::CropId next, double area) {
      if (now.value < crops_.size()) {
        const CropRow& row = crops_[now.value];
        if (row.resource < harvest.size()) {
          const double kept =
              row.resource < loss_share_.size() ? 1.0 - loss_share_[row.resource] : 1.0;
          harvest[row.resource] += YieldKgPerHa(now) * kept * area * kGramsPerKilogram;
        }
      }
      if (next.value < crops_.size()) {
        const CropRow& row = crops_[next.value];
        if (row.resource < seed_next.size()) {
          seed_next[row.resource] += row.seed_kg_per_ha * area * kGramsPerKilogram;
        }
      }
    };
    for (std::uint32_t row = 0; row < world.fields.rows.size(); ++row) {
      const core::FieldRow& field = world.fields.rows[row];
      if (field.kind != core::LandKind::kArable ||
          std::ranges::find(block, world.fields.row_ids[row]) != block.end()) {
        continue;
      }
      add(field.rotation_year0, field.rotation_year1, static_cast<double>(field.area_ga));
    }
    for (std::size_t k = 0; k < block.size(); ++k) {
      const std::uint32_t row = core::FindRow(world.fields, block[k]);
      if (row == core::kNoRow) {
        continue;
      }
      const std::size_t start = kOpeningSlot[k % kOpeningSlot.size()];
      add(chain_[start % 3],
          chain_[(start + 1) % 3],
          static_cast<double>(world.fields.rows[row].area_ga));
    }
    const auto now_ha = static_cast<double>(world.plan.worked_ha_last_year);
    const double scale =
        priced_ha_last_year_ > 0.0 ? (now_ha + block_ha) / priced_ha_last_year_ : 1.0;
    bool keeps = true;
    std::cout << "fallow_policy: year " << world.ledger.closed.year + 1U << " plan guard at +"
              << block_ha << " ha (x" << scale << ", margin " << kPlanGuardMargin
              << "; t due x margin / net harvest - next seed):";
    for (std::size_t resource = 0; resource < due_last_year_.size(); ++resource) {
      if (due_last_year_[resource] <= 0 || resource >= crop_resource_.size() ||
          !crop_resource_[resource]) {
        continue;
      }
      const double due_t =
          static_cast<double>(due_last_year_[resource]) * scale * kPlanGuardMargin / 1e6;
      const double left_t =
          resource < harvest.size() ? (harvest[resource] - seed_next[resource]) / 1e6 : 0.0;
      const bool over = due_t > left_t;
      keeps = keeps && !over;
      std::cout << ' ' << resource_keys_[resource] << ' ' << due_t << '/' << left_t
                << (over ? " OVER" : "");
    }
    std::cout << (keeps ? " - keeps\n" : " - HOLDS the block\n");
    return keeps;
  }

  /// The derelict fields the next block would take: the nearest a standing
  /// grain store first, kFieldsPerBlock of them.
  std::vector<core::FieldId> NextBlockFields(const core::WorldState& world) const {
    std::vector<std::pair<double, core::FieldId>> by_distance;
    for (const core::FieldId id : derelict_) {
      if (std::ranges::find(opened_fields_, id) != opened_fields_.end()) {
        continue;
      }
      const std::uint32_t row = core::FindRow(world.fields, id);
      if (row == core::kNoRow) {
        continue;
      }
      double nearest = std::numeric_limits<double>::max();
      for (const core::UnitRow& unit : world.units.rows) {
        if (unit.level > 0 &&
            std::ranges::find(grain_stores_, unit.type.value) != grain_stores_.end()) {
          const auto dx = static_cast<double>(unit.position.x - world.fields.rows[row].center.x);
          const auto dy = static_cast<double>(unit.position.y - world.fields.rows[row].center.y);
          nearest = std::min(nearest, std::sqrt((dx * dx) + (dy * dy)));
        }
      }
      by_distance.emplace_back(nearest, id);
    }
    // Nearest first; equal distances keep the genesis order.
    std::ranges::stable_sort(by_distance, {}, &std::pair<double, core::FieldId>::first);
    std::vector<core::FieldId> fields;
    for (std::size_t k = 0; k < by_distance.size() && k < kFieldsPerBlock; ++k) {
      fields.push_back(by_distance[k].second);
    }
    return fields;
  }

  double AreaOf(const core::WorldState& world, const std::vector<core::FieldId>& fields) const {
    double area = 0.0;
    for (const core::FieldId id : fields) {
      const std::uint32_t row = core::FindRow(world.fields, id);
      area += row == core::kNoRow ? 0.0 : static_cast<double>(world.fields.rows[row].area_ga);
    }
    return area;
  }

  /// The next block: NextBlockFields, each on its opening slot.
  void OpenNextBlock(core::ISimulation& simulation, const core::WorldState& world) {
    const std::vector<core::FieldId> fields = NextBlockFields(world);
    std::vector<core::OrderRow> orders;
    for (std::size_t k = 0; k < fields.size(); ++k) {
      core::OrderRow order;
      order.kind = core::OrderKind::kSetRotation;
      order.field = fields[k];
      const std::size_t start = kOpeningSlot[k % kOpeningSlot.size()];
      order.rotation_year0 = chain_[start % 3];
      order.rotation_year1 = chain_[(start + 1) % 3];
      order.rotation_year2 = chain_[(start + 2) % 3];
      orders.push_back(order);
      opened_fields_.push_back(fields[k]);
    }
    if (!orders.empty()) {
      simulation.StageOrders(std::span<const core::OrderRow>(orders), {});
      ++opened_blocks_;
    }
  }

  /// THE EVE'S SEED (#725 pair 3; boss, on seed 1935 year 11, whose oat
  /// shipped 0 of 5.16 t with 9.48 t in store): on the year's last day, every
  /// crop position's stock against the seed the core's seed rung holds of it
  /// (core_common fund_ladder.h SeedRungLeft, as of the eve; the turn's
  /// delivery reads it as of the closing day too, DeliverableAboveSeed), and
  /// what is left above the seed for the plan.
  void PrintTheEvesSeed(const core::WorldState& world) const {
    if (seed_norms_.empty()) {
      return;
    }
    const core::ResourceAmounts held =
        core::SeedRungLeft(world, seed_norms_, resource_keys_.size());
    std::cout << "fallow_policy: year " << world.ledger.closed.year + 1U
              << " eve (t, stored / held as seed / above seed / owed):";
    for (std::size_t resource = 0; resource < world.plan.due.size(); ++resource) {
      if (world.plan.due[resource] <= 0 || resource >= crop_resource_.size() ||
          !crop_resource_[resource]) {
        continue;
      }
      core::Grams stored = 0;
      for (const core::UnitRow& unit : world.units.rows) {
        stored += resource < unit.stock.size() ? unit.stock[resource] : 0;
      }
      const core::Grams seed = resource < held.size() ? held[resource] : 0;
      const core::Grams delivered =
          resource < world.plan.delivered.size() ? world.plan.delivered[resource] : 0;
      const core::Grams owed = std::max<core::Grams>(0, world.plan.due[resource] - delivered);
      const core::Grams above = std::max<core::Grams>(0, stored - seed);
      std::cout << ' ' << resource_keys_[resource] << ' ' << static_cast<double>(stored) / 1e6
                << '/' << static_cast<double>(seed) / 1e6 << '/' << static_cast<double>(above) / 1e6
                << '/' << static_cast<double>(owed) / 1e6 << (above < owed ? " SEED-BOUND" : "");
    }
    std::cout << '\n';
  }

  /// The block's potato lying on its fields on 1 December, apart from the
  /// old 70 ha.
  void CountPotatoOnTheBlocks(const core::WorldState& world) {
    if (opened_fields_.empty() || potato_resource_ == core::kNoTableRow) {
      return;
    }
    // THE BLOCK APART FROM THE OLD GROUND (boss, 16 October 2026): what lies
    // on each, over the hectares that grew potato this year, so a block that
    // loses more reads as storage and hands short, not land. The stores' rot
    // pools every field's potato and cannot be split by field: not printed.
    core::Grams block_grams = 0;
    core::Grams old_grams = 0;
    double block_ha = 0.0;
    double old_ha = 0.0;
    for (std::uint32_t row = 0; row < world.fields.rows.size(); ++row) {
      const core::FieldRow& field = world.fields.rows[row];
      if (field.kind != core::LandKind::kArable) {
        continue;
      }
      const bool block =
          std::ranges::find(opened_fields_, world.fields.row_ids[row]) != opened_fields_.end();
      const bool potato_lies = field.reaped_resource.value == potato_resource_;
      const bool potato_grew = field.last_crop.value < crops_.size() &&
                               crops_[field.last_crop.value].resource == potato_resource_;
      (block ? block_grams : old_grams) += potato_lies ? field.reaped_grams : 0;
      (block ? block_ha : old_ha) += potato_grew ? static_cast<double>(field.area_ga) : 0.0;
    }
    const std::uint32_t year =
        world.ledger.current.year == 0 ? world.ledger.closed.year + 1U : world.ledger.current.year;
    block_potato_on_field_t_.emplace_back(year, static_cast<double>(block_grams) / 1e6);
    std::cout << "fallow_policy: year " << year << " potato on the field on 1 December: block "
              << static_cast<double>(block_grams) / 1e6 << " t over " << block_ha
              << " ha of potato, old ground " << static_cast<double>(old_grams) / 1e6 << " t over "
              << old_ha << " ha\n";
  }

  static constexpr double kGramsPerKilogram = 1000.0;

  std::vector<CropRow> crops_;
  std::vector<float> eaten_kcal_per_gram_;
  std::vector<std::uint32_t> grain_stores_;
  std::array<core::CropId, 3> chain_{};
  std::uint32_t potato_resource_ = core::kNoTableRow;
  /// By resource: some crop of the table yields it (a position priced by ha).
  std::vector<bool> crop_resource_;
  std::vector<core::SeedNorm> seed_norms_;
  std::vector<std::vector<double>> history_;
  std::vector<std::string> resource_keys_;
  /// The closed year's loss over its harvest, by resource, 0..1.
  std::vector<double> loss_share_;
  std::vector<core::Grams> due_this_year_;
  std::vector<core::Grams> due_last_year_;
  double priced_ha_this_year_ = 0.0;
  double priced_ha_last_year_ = 0.0;
  core::SimDay watched_turn_day_ = 0;

  std::vector<core::FieldId> derelict_;
  bool derelict_read_ = false;
  std::vector<core::FieldId> opened_fields_;
  std::uint32_t opened_blocks_ = 0;
  std::vector<std::uint32_t> opened_in_year_;
  core::SimDay last_turn_day_ = 0;

  std::vector<std::pair<std::uint32_t, double>> block_potato_seed_t_;
  std::vector<std::pair<std::uint32_t, double>> block_potato_on_field_t_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_FALLOW_POLICY_H_
