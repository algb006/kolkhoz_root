/// @file delivery_policy.h
/// @brief The run chairman's plan deliveries and whose transport carries them
/// (routing stage В5; district §4 «Логистика с районом»; econ's rules 1.1 and
/// (c′), econ/manual/audit/stage-v5-acceptance-gate-2026-10-17.md, taken by
/// boss in boss-core-v5-gate-2026-10-17 [1]).
///
/// WHEN (econ's 1.1, every mode): a position is ordered only once that crop's
/// harvest of the year is in the stores - something of it was harvested this
/// year, no field of it is still to be reaped this year, and none of it lies
/// reaped on a field. Until 0.37.269 the canon ordered every position from the
/// year's first day, and the district's January collection took last year's
/// bread above seed: the winter's food. A position no crop yields (milk) is
/// NOT ordered: the district's milk cart carries it daily (milk_cart.cpp), and
/// the pair of 17 October 2026 read +36 % milk over the due when the run also
/// requested it (0.37.269's first tree; the core's half of that hole is
/// LowerPlanRequest). "Still to be reaped this year" is a standing crop of the
/// resource, except a winter crop sown this calendar year (next year's).
///
/// WHOSE TRANSPORT (kCanon, econ's rule (c′)): OWN CARTS only on a day with no
/// arable field in its sowing or harvest window and no ploughing open, and no
/// heap lying reaped on a field of a tonne or more (kHeapHoldsHorsesGrams) or
/// of a crop still in its harvest months; the DISTRICT's cart otherwise. Named
/// readings: a field is in its SOWING window when this year's slot
/// (rotation_year0) is a crop whose sow months hold today and the field is not
/// yet sown this year; in its HARVEST window when it stands (growing) in a crop
/// whose harvest months hold today; ploughing open is a field in kPlowing,
/// kHarrowing or kSowing. The straw lying at a field is not counted as a heap
/// (the straw is feed, carted after the grain).
///
/// THE PAIR's ARMS (a run patch flips kRunDeliveryMode; never two worlds in one
/// tree): kDistrictOnly - every request to the district's calendar (arm D);
/// kOwnOnly - every delivery by own carts (arm O). Arm I is the tree before
/// В5, where no run ordered a delivery and the deadline carried it all.
///
/// Until В5 NO RUN ORDERED kDeliverPlan: the deadline's delivery carried the
/// whole plan at the year's turn. This policy is the first that does.
/// @threading SINGLE_THREADED

#ifndef TESTS_RUN_COMMON_DELIVERY_POLICY_H_
#define TESTS_RUN_COMMON_DELIVERY_POLICY_H_

#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/ids.h"
#include "core_common/land_state.h"
#include "core_common/limit_state.h"
#include "core_common/order_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace run {

/// Whose transport the run chairman sends with the plan's delivery.
enum class RunDeliveryMode : std::uint8_t {
  kCanon,         ///< own carts outside sowing, harvest, ploughing and heaps, else the district
  kDistrictOnly,  ///< always the district's calendar (arm D)
  kOwnOnly,       ///< always own carts (arm O)
};

/// A reaped heap of at least this many grams holds the own carts home, in any
/// month (econ's narrowing of (c′), econ-boss-v5-acceptance-gate [3]). STUB:
/// a cart's tonne, no measurement behind it.
inline constexpr core::Grams kHeapHoldsHorsesGrams = 1'000'000;

/// The mode every run plays; the pair's arms change this one line.
inline constexpr RunDeliveryMode kRunDeliveryMode = RunDeliveryMode::kCanon;

class DeliveryPolicy {
 public:
  /// @brief Reads each crop's resource, winter flag and its sowing and
  ///        harvest months off crops.csv (human months 1..12, kept 0-based).
  explicit DeliveryPolicy(const core::ITableSet& tables) {
    const core::ITable* const crops = tables.FindTable("crops");
    const core::ITable* const resources = tables.FindTable("resources");
    if (crops == nullptr || resources == nullptr) {
      return;
    }
    const std::uint32_t resource_column = crops->FindColumn("resource");
    const std::uint32_t winter_column = crops->FindColumn("is_winter");
    const std::uint32_t sow_from = crops->FindColumn("sow_from_month");
    const std::uint32_t sow_to = crops->FindColumn("sow_to_month");
    const std::uint32_t harvest_from = crops->FindColumn("harvest_from_month");
    const std::uint32_t harvest_to = crops->FindColumn("harvest_to_month");
    for (std::uint32_t row = 0; row < crops->RowCount(); ++row) {
      CropRule rule;
      rule.resource = resources->FindRowByKey(crops->CellText(row, resource_column));
      rule.winter = MonthOf(crops, row, winter_column) != 0;
      rule.sow_from = MonthOf(crops, row, sow_from) - 1;
      rule.sow_to = MonthOf(crops, row, sow_to) - 1;
      rule.harvest_from = MonthOf(crops, row, harvest_from) - 1;
      rule.harvest_to = MonthOf(crops, row, harvest_to) - 1;
      crops_.push_back(rule);
    }
  }

  /// @brief One day of the chairman's attention to the plan: one kDeliverPlan
  ///        for each position owed beyond what is asked whose harvest is in.
  ///        Call once a day.
  void RunDay(core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    if (world.plan.announced == 0) {
      return;
    }
    const HorseHold hold = FieldsNeedHorses(world);
    ++hold_days_[static_cast<std::size_t>(hold)];
    ++hold_by_month_[(world.calendar.day % core::kDaysPerYear) / core::kDaysPerMonth]
                    [static_cast<std::size_t>(hold)];
    const bool own = mode_ == RunDeliveryMode::kOwnOnly ||
                     (mode_ == RunDeliveryMode::kCanon && hold == HorseHold::kFree);
    std::vector<core::OrderRow> orders;
    for (std::size_t index = 0; index < world.plan.due.size(); ++index) {
      // A request standing with the district is taken over by own carts on
      // the first free day (the core's door: own carts take what they load
      // off the request). Until then a position was ordered once, on the day
      // its harvest came in - always a day the next crop held the horses -
      // and arm C sent own carts nowhere in 36 village-years.
      const bool take_over =
          own && index < world.plan.requested.size() && world.plan.requested[index] > 0;
      if (!YieldedByACrop(index) || (!Unasked(world, index) && !take_over)) {
        continue;
      }
      if (!HarvestIn(world, index)) {
        ++held_days_;
        continue;
      }
      core::OrderRow order;
      order.kind = core::OrderKind::kDeliverPlan;
      order.resource = core::DefIdFromIndex<core::ResourceIdTag>(index);
      order.enable = own ? 1 : 0;
      order.amount = 0;  // what is owed of this position
      orders.push_back(order);
    }
    if (orders.empty()) {
      return;
    }
    simulation.StageOrders(std::span<const core::OrderRow>(orders), {});
    (own ? own_orders_ : district_orders_) += static_cast<std::uint32_t>(orders.size());
  }

  /// @brief Whose transport from now on, for a run whose bot is not the canon's
  ///        (host's novice rides the district always: boss-core-v5-gate [4]).
  ///        Says so in the run's stream, beside the Declare it overrides.
  void SetMode(std::string_view run, RunDeliveryMode mode) {
    mode_ = mode;
    std::cout << run << ": THE DELIVERY MODE IS SET AT RUN TIME - the plan goes " << ModeWords(mode)
              << '\n';
  }

  /// @brief The fixture difference, in words, before anything is measured.
  static void Declare(std::string_view run) {
    std::cout << run
              << ": FIXTURE DIFFERS FROM 0.37.257 - the run's chairman ORDERS THE PLAN'S "
                 "DELIVERY of each position some crop yields, owed beyond what is asked, once "
                 "that crop's harvest of the year is in (stage В5; econ's 1.1; milk keeps its "
                 "own cart), "
              << ModeWords(kRunDeliveryMode) << '\n';
  }

  /// @brief How many orders went by each transport, and how many position-days
  ///        waited for their harvest.
  void Report(std::string_view run) const {
    std::cout << run << ": the run's chairman ordered " << own_orders_
              << " plan deliveries by own carts and " << district_orders_
              << " by the district's cart; " << held_days_
              << " position-days owed waited for their harvest to be in\n"
              << run << ": the plan's days by what held own carts home (rule (c′)): free "
              << hold_days_[0] << ", a heap " << hold_days_[1] << ", ploughing " << hold_days_[2]
              << ", a field in harvest " << hold_days_[3] << ", a crop in its harvest months "
              << hold_days_[4] << ", a slot in its sow months " << hold_days_[5] << '\n'
              << run << ": by month, free/heap/plough/harvest/harvest-months/sow-months:";
    for (std::size_t month = 0; month < hold_by_month_.size(); ++month) {
      std::cout << ' ' << month + 1 << '=';
      for (std::size_t hold = 0; hold < hold_by_month_[month].size(); ++hold) {
        std::cout << (hold == 0 ? "" : "/") << hold_by_month_[month][hold];
      }
    }
    std::cout << '\n';
  }

 private:
  /// What held the own carts home on a day, by (c′)'s clauses.
  enum class HorseHold : std::uint8_t {
    kFree,
    kHeap,
    kPloughing,
    kHarvest,
    kHarvestWindow,
    kSowWindow,
    kCount,
  };

  static const char* ModeWords(RunDeliveryMode mode) {
    return mode == RunDeliveryMode::kDistrictOnly ? "always by the district's monthly cart (arm D)"
           : mode == RunDeliveryMode::kOwnOnly
               ? "always by own carts (arm O)"
               : "by own carts on a day with no field in its sowing or harvest window, no "
                 "ploughing open and no heap on a field of a tonne or of a crop in its harvest "
                 "months, by the district's monthly cart otherwise (econ's rule (c′), narrowed)";
  }

  struct CropRule {
    std::uint32_t resource = core::kNoTableRow;
    bool winter = false;
    std::int32_t sow_from = -1;
    std::int32_t sow_to = -1;
    std::int32_t harvest_from = -1;
    std::int32_t harvest_to = -1;
  };

  static std::int32_t MonthOf(const core::ITable* table, std::uint32_t row, std::uint32_t column) {
    const std::optional<float> value = table->CellReal(row, column);
    return value ? static_cast<std::int32_t>(*value) : 0;
  }

  static bool InMonths(std::int32_t month, std::int32_t from, std::int32_t to) {
    return from >= 0 && month >= from && month <= to;
  }

  /// The position is owed beyond what the district is asked for and what an
  /// own delivery carries.
  static bool Unasked(const core::WorldState& world, std::size_t index) {
    core::Grams covered = index < world.plan.delivered.size() ? world.plan.delivered[index] : 0;
    covered += index < world.plan.requested.size() ? world.plan.requested[index] : 0;
    for (const core::LimitDeliveryRow& row : world.limit_deliveries.rows) {
      covered += row.outbound != 0 && index < row.goods.size() ? row.goods[index] : 0;
    }
    return world.plan.due[index] > covered;
  }

  /// The rule of the crop on a field, or nullptr for none.
  const CropRule* RuleOf(core::CropId crop) const {
    return crop.value < crops_.size() ? &crops_[crop.value] : nullptr;
  }

  /// A position some crop yields (rye, oat, potato); milk is not one.
  bool YieldedByACrop(std::size_t index) const {
    for (const CropRule& rule : crops_) {
      if (rule.resource == index) {
        return true;
      }
    }
    return false;
  }

  /// econ's 1.1: this year's harvest of the resource is in the stores.
  bool HarvestIn(const core::WorldState& world, std::size_t index) const {
    if (index >= world.ledger.current.harvest.size() || world.ledger.current.harvest[index] <= 0) {
      return false;  // nothing of it harvested yet this year
    }
    const core::SimDay today = world.calendar.day;
    for (const core::FieldRow& field : world.fields.rows) {
      const CropRule* const rule = RuleOf(field.crop);
      if (rule == nullptr || rule->resource != index) {
        continue;
      }
      if (field.reaped_grams > 0 || field.phase == core::FieldPhase::kHarvest) {
        return false;
      }
      const bool standing =
          field.phase == core::FieldPhase::kSowing || field.phase == core::FieldPhase::kGrowing;
      const bool next_years = rule->winter && field.sown_day != core::kNeverSownDay &&
                              field.sown_day / core::kDaysPerYear == today / core::kDaysPerYear;
      if (standing && !next_years) {
        return false;
      }
    }
    return true;
  }

  /// A heap that holds the horses home: one of a cart's size, or of a crop
  /// still inside its harvest months (econ's narrowing of (c′),
  /// econ-boss-v5-acceptance-gate [3]). Until then any reaped gram did, and
  /// the crumbs lying all winter kept arm C's own carts home in every one of
  /// 36 village-years (C == D to the digit).
  bool HeapHoldsHorses(const core::FieldRow& field, std::int32_t month) const {
    if (field.reaped_grams >= kHeapHoldsHorsesGrams) {
      return true;
    }
    if (field.reaped_grams <= 0) {
      return false;
    }
    for (const CropRule& rule : crops_) {
      if (rule.resource == field.reaped_resource.value &&
          InMonths(month, rule.harvest_from, rule.harvest_to)) {
        return true;
      }
    }
    return false;
  }

  /// econ's (c′), narrowed: the first clause that holds the horses today, or
  /// kFree - a heap that holds them (HeapHoldsHorses), ploughing open, a field
  /// in kHarvest, a growing field inside its harvest months, an idle field
  /// inside its slot's sow months.
  HorseHold FieldsNeedHorses(const core::WorldState& world) const {
    const auto month =
        static_cast<std::int32_t>((world.calendar.day % core::kDaysPerYear) / core::kDaysPerMonth);
    for (const core::FieldRow& field : world.fields.rows) {
      if (HeapHoldsHorses(field, month)) {
        return HorseHold::kHeap;
      }
      if (field.kind != core::LandKind::kArable) {
        continue;
      }
      switch (field.phase) {
        case core::FieldPhase::kPlowing:
        case core::FieldPhase::kHarrowing:
        case core::FieldPhase::kSowing:
          return HorseHold::kPloughing;
        case core::FieldPhase::kHarvest:
          return HorseHold::kHarvest;
        case core::FieldPhase::kGrowing: {
          const CropRule* const rule = RuleOf(field.crop);
          if (rule != nullptr && InMonths(month, rule->harvest_from, rule->harvest_to)) {
            return HorseHold::kHarvestWindow;
          }
          break;
        }
        case core::FieldPhase::kIdle: {
          const CropRule* const rule = RuleOf(field.rotation_year0);
          if (rule != nullptr && InMonths(month, rule->sow_from, rule->sow_to)) {
            return HorseHold::kSowWindow;
          }
          break;
        }
        default:
          break;
      }
    }
    return HorseHold::kFree;
  }

  std::vector<CropRule> crops_;
  RunDeliveryMode mode_ = kRunDeliveryMode;
  std::array<std::uint32_t, static_cast<std::size_t>(HorseHold::kCount)> hold_days_{};
  std::array<std::array<std::uint32_t, static_cast<std::size_t>(HorseHold::kCount)>,
             core::kMonthsPerYear>
      hold_by_month_{};
  std::uint32_t own_orders_ = 0;
  std::uint32_t district_orders_ = 0;
  std::uint32_t held_days_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_DELIVERY_POLICY_H_
