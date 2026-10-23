/// @file delivery_policy.h
/// @brief The run chairman's plan deliveries and whose transport carries them
/// (routing stage В5; district §4 «Логистика с районом»; econ's rules 1.1 and
/// (c″), econ/manual/audit/stage-v5-acceptance-gate-2026-10-17.md, taken by
/// boss in boss-core-v5-gate-2026-10-17 [1] and [7]).
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
/// WHOSE TRANSPORT: the runs play kDistrictOnly (see kRunDeliveryMode).
/// kOwnWhenFree is econ's rule (c″), boss-core-v5-gate [7], kept as an arm: OWN
/// CARTS only on a day with no plough, harrow or sowing crew and no harvest
/// crew assigned that day or the previous working day, and no heap of a tonne
/// or more (kHeapHoldsHorsesGrams) lying reaped on a field; the DISTRICT's
/// cart otherwise. A standing district request is taken over by own carts on
/// a free day. Rule (c′) before it read the fields' phases and windows: a
/// part-turned autumn furrow sits in kPlowing to the year's turn, and no day
/// after a harvest was ever free (C == D in 36 of 36 village-years). The
/// straw lying at a field is not counted as a heap (the straw is feed,
/// carted after the grain).
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
  kOwnWhenFree,   ///< own carts on a day free of field crews and heaps (c″), else the district
  kDistrictOnly,  ///< always the district's calendar (arm D) - THE RUNS' MODE
  kOwnOnly,       ///< always own carts (arm O)
  kNone,          ///< no plan delivery ordered: the deadline carries it (host's novice)
};

/// A reaped heap of at least this many grams holds the own carts home, in any
/// month (econ's narrowing of (c′), econ-boss-v5-acceptance-gate [3]). STUB:
/// a cart's tonne, no measurement behind it.
inline constexpr core::Grams kHeapHoldsHorsesGrams = 1'000'000;

/// The mode every run plays; the pair's arms change this one line. THE
/// DISTRICT ALWAYS (boss-core-v5-gate [10], 17 October 2026): every arm with
/// own carts was red on econ's gate 7 - old households' hungry family-days
/// over 16 years +18.9 % (c″), +21.7 % (c″ waiting), +24.7 % (O) against
/// +1.0 % for D - and O lost no harvest against D in 0 of 36 village-years.
inline constexpr RunDeliveryMode kRunDeliveryMode = RunDeliveryMode::kDistrictOnly;

class DeliveryPolicy {
 public:
  /// @brief Reads each crop's resource and winter flag off crops.csv.
  explicit DeliveryPolicy(const core::ITableSet& tables) {
    const core::ITable* const crops = tables.FindTable("crops");
    const core::ITable* const resources = tables.FindTable("resources");
    if (crops == nullptr || resources == nullptr) {
      return;
    }
    const std::uint32_t resource_column = crops->FindColumn("resource");
    const std::uint32_t winter_column = crops->FindColumn("is_winter");
    for (std::uint32_t row = 0; row < crops->RowCount(); ++row) {
      CropRule rule;
      rule.resource = resources->FindRowByKey(crops->CellText(row, resource_column));
      const std::optional<float> winter = crops->CellReal(row, winter_column);
      rule.winter = winter && *winter != 0.0F;
      crops_.push_back(rule);
    }
  }

  /// @brief One day of the chairman's attention to the plan: one kDeliverPlan
  ///        for each position owed beyond what is asked whose harvest is in.
  ///        Call once a day.
  void RunDay(core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    const HorseHold hold = FieldsNeedHorses(world);
    if (world.plan.announced == 0 || mode_ == RunDeliveryMode::kNone) {
      return;
    }
    ++hold_days_[static_cast<std::size_t>(hold)];
    ++hold_by_month_[(world.calendar.day % core::kDaysPerYear) / core::kDaysPerMonth]
                    [static_cast<std::size_t>(hold)];
    const bool own = mode_ == RunDeliveryMode::kOwnOnly ||
                     (mode_ == RunDeliveryMode::kOwnWhenFree && hold == HorseHold::kFree);
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
              << run << ": the days by what held own carts home (rule (c″)): free " << hold_days_[0]
              << ", a heap of a tonne " << hold_days_[1] << ", a plough/harrow/sowing crew today "
              << hold_days_[2] << " or the previous working day " << hold_days_[3]
              << ", a harvest crew today " << hold_days_[4] << " or the previous working day "
              << hold_days_[5] << '\n'
              << run << ": by month, free/heap/plough/plough-before/harvest/harvest-before:";
    for (std::size_t month = 0; month < hold_by_month_.size(); ++month) {
      std::cout << ' ' << month + 1 << '=';
      for (std::size_t hold = 0; hold < hold_by_month_[month].size(); ++hold) {
        std::cout << (hold == 0 ? "" : "/") << hold_by_month_[month][hold];
      }
    }
    std::cout << '\n';
  }

 private:
  /// What held the own carts home on a day, by (c″)'s clauses.
  enum class HorseHold : std::uint8_t {
    kFree,
    kHeap,
    kPloughCrew,
    kPloughCrewBefore,
    kHarvestCrew,
    kHarvestCrewBefore,
    kCount,
  };

  static const char* ModeWords(RunDeliveryMode mode) {
    if (mode == RunDeliveryMode::kNone) {
      return "by no order of the run's chairman: the deadline carries what is owed";
    }
    return mode == RunDeliveryMode::kDistrictOnly ? "always by the district's monthly cart (arm D)"
           : mode == RunDeliveryMode::kOwnOnly
               ? "always by own carts (arm O)"
               : "by own carts on a day with no plough, harrow, sowing or harvest crew "
                 "assigned that day or the previous working day and no heap of a tonne on a "
                 "field, by the district's monthly cart otherwise (econ's rule (c″))";
  }

  struct CropRule {
    std::uint32_t resource = core::kNoTableRow;
    bool winter = false;
  };

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

  /// econ's (c″) (econ-boss-v5-acceptance-gate [4], boss's ruling [7]): the
  /// first clause that holds the horses today, or kFree - a reaped heap of a
  /// tonne or more on a field; a plough, harrow or sowing crew assigned
  /// today, or on the previous working day; a harvest crew the same. Until
  /// (c″) the clauses read the fields' PHASES and windows, and a part-turned
  /// autumn furrow sits in kPlowing to the year's turn: no free day came
  /// after any harvest, and arm C == arm D. A working day is one with anyone
  /// assigned; a day off keeps the previous working day's crews.
  HorseHold FieldsNeedHorses(const core::WorldState& world) {
    bool plough_crew = false;
    bool harvest_crew = false;
    bool anyone = false;
    for (const core::ResidentRow& person : world.residents.rows) {
      const core::WorkKind kind = person.work.kind;
      anyone = anyone || kind != core::WorkKind::kNone;
      plough_crew = plough_crew || kind == core::WorkKind::kPlowing ||
                    kind == core::WorkKind::kHarrowing || kind == core::WorkKind::kSowing;
      harvest_crew = harvest_crew || kind == core::WorkKind::kHarvest;
    }
    const bool plough_held = plough_crew || plough_crew_before_;
    const bool harvest_held = harvest_crew || harvest_crew_before_;
    if (anyone) {
      plough_crew_before_ = plough_crew;
      harvest_crew_before_ = harvest_crew;
    }
    // A HEAP IS A CROP's (a named reading of econ's «heap»): grain or potato
    // reaped on arable. The hay mown on a meadow and the straw are not - the
    // smoke of 17 October 2026 found hay heaps of 15-50 t on meadows from June
    // to December holding the horses every one of those days.
    for (const core::FieldRow& field : world.fields.rows) {
      if (field.kind == core::LandKind::kArable && field.reaped_grams >= kHeapHoldsHorsesGrams &&
          YieldedByACrop(field.reaped_resource.value)) {
        return HorseHold::kHeap;
      }
    }
    if (plough_held) {
      return plough_crew ? HorseHold::kPloughCrew : HorseHold::kPloughCrewBefore;
    }
    if (harvest_held) {
      return harvest_crew ? HorseHold::kHarvestCrew : HorseHold::kHarvestCrewBefore;
    }
    return HorseHold::kFree;
  }

  std::vector<CropRule> crops_;
  RunDeliveryMode mode_ = kRunDeliveryMode;
  bool plough_crew_before_ = false;
  bool harvest_crew_before_ = false;
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
