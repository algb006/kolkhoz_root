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
///     second the rest;
///   * AND ONLY WHEN THE HOUSES ARE BUILT (the human, 24 October 2026: «Только
///     когда дома построены», tasks.db #1373; econ, the page's «24 October»):
///     the lamp «жилья не хватает» out and no house site at level 0, read at
///     the same moment (HousesBuilt). A block once open stays open; a new one
///     waits again whenever the houses are short. The rule that gave the
///     block's spring the house teams (31888cc1) and the plan-trade door for
///     its seed (86b36ead) were dropped with that word and are not here.

#ifndef TESTS_RUN_COMMON_FALLOW_POLICY_H_
#define TESTS_RUN_COMMON_FALLOW_POLICY_H_

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_catalog/seed_norm_catalog.h"
#include "core_common/alarm_state.h"
#include "core_common/calendar.h"
#include "core_common/fund_ladder.h"
#include "core_common/land_state.h"
#include "core_common/order_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

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
                 "year's three fields on barley, oat, barley, no potato; only when the "
                 "houses are built (the housing-short lamp out, no house site open); the "
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
    CountSpringBuilding(world);
    SendHouseCrews(simulation, world);
    const std::uint32_t day_of_year = world.calendar.day % core::kDaysPerYear;
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
    const bool houses_built = HousesBuilt(simulation, world, year);
    std::cout << "fallow_policy: year " << year << " forecast "
              << std::llround(forecast / kKcalPerGcal) << " Gcal of food against a need of "
              << std::llround(need / kKcalPerGcal) << " Gcal";
    if (!(need > 0.0) || forecast >= need || opened_blocks_ >= kBlocks) {
      std::cout << (opened_blocks_ >= kBlocks ? " - every block open\n" : " - enough\n");
      if (opened_blocks_ < kBlocks) {
        NoteTurn(world, year, 'E');
      }
      held_for_houses_in_a_row_ = 0;  // a turn with food enough breaks the houses' row
      return;
    }
    std::cout << " - SHORT\n";
    // THE HOUSES WAIT HAS A TWO-YEAR DEADLINE (#1726 option 2; the human 16 Nov «Ваше решение»
    // -> econ's option, econ-boss-1737-1726-gates [1]): a block that the houses guard held two
    // turns in a row opens on the third even while the houses are short - the houses stay
    // first, but not forever. Turn code D. Off (--no-fallow-houses-deadline): 0.37.386's rule.
    const bool deadline = houses_deadline_on_ && held_for_houses_in_a_row_ >= kHousesDeadlineTurns;
    if (!houses_built && !deadline) {
      std::cout << "fallow_policy: year " << year << " block " << opened_blocks_ + 1U
                << " HELD: the houses are not built\n";
      NoteTurn(world, year, 'H');
      ++held_for_houses_in_a_row_;
      return;
    }
    const bool by_deadline = !houses_built;
    held_for_houses_in_a_row_ = 0;
    if (kPlanGuardOn && !BlockKeepsThePlan(world, NextBlockFields(world))) {
      std::cout << "fallow_policy: year " << year << " block " << opened_blocks_ + 1U
                << " HELD by the plan guard\n";
      NoteTurn(world, year, 'G');
      return;
    }
    NoteTurn(world, year, by_deadline ? 'D' : 'O');
    deadline_opened_ = deadline_opened_ || by_deadline;
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
    // THE HOUSES-FIRST INSTRUMENT SHOUTS (econ, fields-grow-with-eaters «24
    // October»): a condition that almost never holds stops the fallow while
    // the blocks' count reads as the rule's choice.
    std::cout << "fallow_policy: houses built at " << houses_built_years_ << " of "
              << planning_years_ << " planning moments";
    if (planning_years_ == 0) {
      std::cout << " - NOT MEASURED: no planning moment was read\n";
    } else if (houses_built_years_ * 10U < planning_years_) {
      std::cout << " - UNDER 10 %: THE FALLOW HAS EFFECTIVELY STOPPED\n";
    } else {
      std::cout << '\n';
    }
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
  }

  /// @brief #1692's hold print (econ, econ-boss-1692-hold-print [1]), a village: «FALLOW1692
  ///        seed S turns y:code:m ...» - every turn with a block still to open, code E (food
  ///        enough, no block wanted), H (short, held: the houses not built), G (short, held by
  ///        the plan guard), O (opened); m 1 when a field of the next block lies mown as it
  ///        lies (B's meadow) that turn. The food forecast (ForecastFoodKcal) reads arable
  ///        fields only, so B's meadow adds nothing to it: the figure with and without it is one.
  void PrintHolds(std::ostream& out, std::uint64_t seed) const {
    out << "population_curve: FALLOW1692 seed " << seed << " turns"
        << (turns_.empty() ? " -" : turns_) << '\n';
  }

  /// @brief The fields opened so far, for a run's own prints.
  const std::vector<core::FieldId>& OpenedFields() const { return opened_fields_; }

 private:
  static constexpr std::uint32_t kFieldsPerBlock = 3;
  static constexpr std::uint32_t kBlocks = 2;
  /// #1726 option 2: the turns in a row the houses guard may hold a block before it opens.
  static constexpr std::uint32_t kHousesDeadlineTurns = 2;

 public:
  /// @brief #1726's arm: false is 0.37.386's rule, the houses guard holding for ever.
  void SetHousesDeadline(bool on) { houses_deadline_on_ = on; }

  /// @brief #1726 (b)'s arm (econ, core-econ-1726-canon-pair [10]; boss [11] GO): false
  ///        sends no house crews (--no-fallow-house-crews), the split pair's world.
  void SetHouseCrews(bool on) { house_crews_on_ = on; }

  /// @brief #1726 (b)'s trace (boss [13], [18]): at hour 2 of each day, every house site under
  ///        construction - its hands now (T) and at hour 2 the day before - and for each day crew
  ///        on one, its N, the picks placed on the site (k), and each unplaced pick's rest and
  ///        work. Called every step; acts at hour 2 only. Lines CREWTRACE1726 (one an order-day)
  ///        and the village's sums in PrintCrewTrace.
  void TraceCrews(const core::WorldState& world) {
    const std::uint64_t seed = world.world_seed;
    CountHorseEvents(world);  // every step: the hand-overs and the lots' arrivals
    if (core::HourFromTick(world.calendar.tick) != 2U || world.calendar.day == trace_day_) {
      return;
    }
    trace_day_ = world.calendar.day;
    const std::uint32_t year_index = world.calendar.day / core::kDaysPerYear;
    std::map<std::uint32_t, std::uint32_t> hands;  // site id -> hands on it now
    for (const core::ResidentRow& resident : world.residents.rows) {
      if (resident.work.kind == core::WorkKind::kConstruction) {
        ++hands[resident.work.unit.value];
      }
    }
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      if (unit.type.value != house_type_ ||
          unit.construction.phase != core::ConstructionPhase::kBuilding) {
        continue;
      }
      const std::uint32_t id = world.units.row_ids[row].value;
      if (year_index < house_hands_.size()) {
        house_hands_[year_index] += hands[id];
      }
    }
    // Each day crew on a house site, by its picks - ONLY TODAY'S: the book holds the picks of every
    // crew still due, and a crew issued for a later working day, or moved on by a day off, is not
    // on its site this morning (the first trace, t1726on, counted them all).
    std::map<std::uint32_t, std::vector<core::ResidentId>> picks;
    for (const core::CrewPick& pick : world.day_orders.picks) {
      const bool due_today =
          std::ranges::any_of(world.day_orders.due, [&](const core::DayOrderDue& due) {
            return due.order.value == pick.crew.value && due.day == world.calendar.day;
          });
      if (due_today) {
        picks[pick.crew.value].push_back(pick.resident);
      }
    }
    for (const auto& [crew, residents] : picks) {
      const std::uint32_t order_row = core::FindRow(world.orders, core::OrderId{crew});
      if (order_row == core::kNoRow) {
        continue;
      }
      const core::OrderRow& order = world.orders.rows[order_row];
      const std::uint32_t site_row = core::FindRow(world.units, order.unit);
      if (order.kind != core::OrderKind::kAssignCrew || site_row == core::kNoRow ||
          world.units.rows[site_row].type.value != house_type_) {
        continue;
      }
      std::uint32_t placed = 0;
      std::uint32_t tired = 0;
      std::uint32_t elsewhere = 0;
      std::uint32_t held = 0;
      float walk = 0.0F;
      for (const core::ResidentId resident_id : residents) {
        const std::uint32_t r = core::FindRow(world.residents, resident_id);
        if (r == core::kNoRow) {
          continue;
        }
        const core::ResidentRow& resident = world.residents.rows[r];
        if (carters_yesterday_.contains(resident_id.value) && year_index < kSplitYears) {
          ++supply_.picks_were_carters[year_index];  // boss [20]: a supply carter taken
        }
        if (resident.work.kind == core::WorkKind::kConstruction &&
            resident.work.unit.value == order.unit.value) {
          ++placed;
          walk += resident.work.travel_hours > 0.0F ? resident.work.travel_hours : 0.0F;
        } else if (resident.work.kind != core::WorkKind::kNone) {
          ++elsewhere;
        } else if (resident.rest <= kTiredRest) {
          ++tired;
        } else if (resident.idle_reason == core::IdleReason::kIdleReasonCount) {
          ++held;  // no reason booked: the morning's plan never had him (the order held him)
        }
      }
      const std::uint32_t n = order.crew_size;
      const std::uint32_t t = hands[order.unit.value];
      const auto prev = hands_yesterday_.find(order.unit.value);
      const std::uint32_t t_prev = prev == hands_yesterday_.end() ? 0U : prev->second;
      std::cout << "population_curve: CREWTRACE1726 seed " << seed << " day " << world.calendar.day
                << " site " << order.unit.value << " N " << n << " k " << placed << " T " << t
                << " T_prev " << t_prev << " unplaced_tired " << tired << " picks_elsewhere "
                << elsewhere << " unplaced_other "
                << (residents.size() - placed - tired - elsewhere) << " walk_h "
                << (placed > 0 ? walk / static_cast<float>(placed) : 0.0F) << " site_phase "
                << static_cast<int>(world.units.rows[site_row].construction.phase) << " site_level "
                << static_cast<int>(world.units.rows[site_row].level) << " site_labor "
                << world.units.rows[site_row].construction.labor_days_remaining << " idle_village "
                << IdleAdults(world) << " held_by_order " << held << '\n';
      trace_.orders += 1;
      trace_.n += n;
      trace_.k += placed;
      trace_.t += t;
      trace_.t_prev += t_prev;
      trace_.tired += tired;
      trace_.elsewhere += elsewhere;
    }
    hands_yesterday_ = std::move(hands);
    CountSupply(world, year_index);
  }

  /// @brief Boss [20]'s print, a line a village: per year, at hour 2, the site-supply carters,
  ///        the horses in work by kind (field work, site-supply carting, other), the hands felling
  ///        and carting from the stands, the house sites waiting for materials (kDelivering), and
  ///        the crews' picks that had been site-supply carters the day before.
  void PrintSupply(std::ostream& out, std::uint64_t seed) const {
    const auto row = [&out](const char* name, const std::array<std::uint32_t, kSplitYears>& v) {
      out << ' ' << name;
      for (std::uint32_t index = 0; index < kSplitYears; ++index) {
        out << (index == 0 ? " " : ":") << v[index];
      }
    };
    out << "population_curve: SUPPLY1726 seed " << seed;
    row("supply_carters", supply_.carters);
    row("horse_field", supply_.horse_field);
    row("horse_supply", supply_.horse_supply);
    row("horse_other", supply_.horse_other);
    row("felling", supply_.felling);
    row("stand_haul", supply_.stand_haul);
    row("house_wait_sites", supply_.wait_sites);
    row("picks_were_carters", supply_.picks_were_carters);
    out << '\n';
  }

 private:
  struct SupplyTally {
    std::array<std::uint32_t, 16> carters{};
    std::array<std::uint32_t, 16> horse_field{};
    std::array<std::uint32_t, 16> horse_supply{};
    std::array<std::uint32_t, 16> horse_other{};
    std::array<std::uint32_t, 16> felling{};
    std::array<std::uint32_t, 16> stand_haul{};
    std::array<std::uint32_t, 16> wait_sites{};
    std::array<std::uint32_t, 16> picks_were_carters{};
  };

  SupplyTally supply_;
  std::set<std::uint32_t> carters_yesterday_;

  // -- boss [24]'s horse count (HORSE1726) ------------------------------------
  /// What a ridden horse does at hour 2, the classes of the print.
  enum HorseUse : std::uint8_t {
    kUseField = 0,   ///< plough, harrow, the sowing and reaping brigades' carts
    kUseTimber,      ///< stand carting, the district's timber lot, site supply
    kUseHeap,        ///< carting a field's heap (the harvest home)
    kUseHerd,        ///< a herd's leg (hay, milk, the move)
    kUseUnit,        ///< a unit's own carting (manure, stores, production)
    kUseRoad,        ///< road work
    kUseExtraction,  ///< a dig's load
    kUsePeople,      ///< a people's cart or a ride on any other work
    kUseCount,
  };

  struct HorseTally {
    std::array<double, 16> adults{};        ///< adult horses, hour-2 head-days
    std::array<double, 16> foals{};         ///< juvenile + newborn, hour-2 head-days
    std::array<std::int64_t, 16> handed{};  ///< kStockHandedOver of a horse herd, head
    std::array<std::int64_t, 16> bought{};  ///< horse lots arrived, head
    /// y4-y16 by month (0-based): horse-days ridden by use, and adults not ridden.
    std::array<std::array<std::uint32_t, 12>, kUseCount> by_month{};
    std::array<std::uint32_t, 12> idle_by_month{};
  };

  HorseTally horses_;
  std::uint32_t horse_kind_ = core::kNoTableRow;
  std::set<std::uint32_t> arrivals_seen_;

  void CountHorseEvents(const core::WorldState& world) {
    const std::uint32_t year = world.calendar.day / core::kDaysPerYear;
    if (horse_kind_ == core::kNoTableRow || year >= 16) {
      return;
    }
    for (const core::SimEvent& event : world.step_events) {
      if (event.kind != core::EventKind::kStockHandedOver) {
        continue;
      }
      const std::uint32_t row = core::FindRow(world.herds, event.herd);
      if (row != core::kNoRow && world.herds.rows[row].kind.value == horse_kind_) {
        horses_.handed[year] += event.amount;
      }
    }
    for (std::uint32_t row = 0; row < world.livestock_arrivals.rows.size(); ++row) {
      const core::LivestockArrivalRow& lot = world.livestock_arrivals.rows[row];
      const std::uint32_t id = world.livestock_arrivals.row_ids[row].value;
      if (lot.kind.value == horse_kind_ && !arrivals_seen_.contains(id)) {
        arrivals_seen_.insert(id);
        horses_.bought[year] += lot.head_count;
      }
    }
  }

  /// The hour-2 horse census for HORSE1726.
  void CountHorses(const core::WorldState& world,
                   std::uint32_t year_index,
                   const std::set<std::uint32_t>& sites) {
    if (horse_kind_ == core::kNoTableRow || year_index >= 16) {
      return;
    }
    std::uint32_t adults = 0;
    for (const core::HerdRow& herd : world.herds.rows) {
      if (herd.kind.value == horse_kind_) {
        adults += herd.adult_count;
        horses_.foals[year_index] += herd.juvenile_count + herd.newborn_count;
      }
    }
    horses_.adults[year_index] += adults;
    const auto month = static_cast<std::uint32_t>(world.calendar.date.month);
    std::uint32_t ridden = 0;
    for (const core::ResidentRow& resident : world.residents.rows) {
      const core::WorkAssignment& work = resident.work;
      if (work.rides_horse == 0) {
        continue;
      }
      ++ridden;
      if (year_index < 3) {
        continue;  // by month over y4-y16
      }
      HorseUse use = kUsePeople;
      const bool field_kind =
          work.kind == core::WorkKind::kPlowing || work.kind == core::WorkKind::kHarrowing ||
          work.kind == core::WorkKind::kSowing || work.kind == core::WorkKind::kHarvest;
      if (field_kind) {
        use = kUseField;
      } else if (work.kind == core::WorkKind::kHauling) {
        if (work.stand.value != core::kInvalidEntityIdValue ||
            work.limit_delivery.value != core::kInvalidEntityIdValue ||
            sites.contains(work.unit.value)) {
          use = kUseTimber;
        } else if (work.extraction_site.value != core::kInvalidEntityIdValue) {
          use = kUseExtraction;
        } else if (work.field.value != core::kInvalidEntityIdValue) {
          use = kUseHeap;
        } else if (work.herd.value != core::kInvalidEntityIdValue) {
          use = kUseHerd;
        } else if (work.unit.value != core::kInvalidEntityIdValue) {
          use = kUseUnit;
        }
      } else if (work.kind == core::WorkKind::kRoadWork) {
        use = kUseRoad;
      }
      if (month < 12) {
        ++horses_.by_month[use][month];
      }
    }
    if (year_index >= 3 && month < 12) {
      horses_.idle_by_month[month] += adults > ridden ? adults - ridden : 0U;
    }
  }

  // -- boss [26]: why idle teams are not given to the timber chain (TIMBER1726) -------------
  /// The timber chain's jobs the morning left short (WorldState::crew_gaps, the plan's own
  /// diagnosis), y4-y16 by month: by job (felling, stand carting, house/site supply) and by the
  /// plan's reason; their hands placed; and the horses the morning plan left over.
  enum TimberJob : std::uint8_t { kJobFelling = 0, kJobStand, kJobSupply, kTimberJobs };

  std::array<std::array<std::array<std::uint32_t, 12>, 4>, kTimberJobs> timber_gaps_{};
  std::array<std::array<std::uint32_t, 12>, kTimberJobs> timber_placed_{};
  std::array<std::uint32_t, 12> horses_left_{};

  void CountTimberGaps(const core::WorldState& world,
                       std::uint32_t year_index,
                       const std::set<std::uint32_t>& sites) {
    const auto month = static_cast<std::uint32_t>(world.calendar.date.month);
    if (year_index < 3 || year_index >= 16 || month >= 12) {
      return;
    }
    if (world.morning_horses_left != core::kMorningHorsesUnknown) {
      horses_left_[month] += world.morning_horses_left;
    }
    for (const core::CrewGapLine& gap : world.crew_gaps) {
      TimberJob job = kTimberJobs;
      if (gap.work == core::WorkKind::kFelling) {
        job = kJobFelling;
      } else if (gap.work == core::WorkKind::kHauling &&
                 gap.stand.value != core::kInvalidEntityIdValue) {
        job = kJobStand;
      } else if (gap.work == core::WorkKind::kHauling && sites.contains(gap.unit.value)) {
        job = kJobSupply;
      }
      if (job == kTimberJobs) {
        continue;
      }
      const auto reason = static_cast<std::size_t>(gap.shortfall);
      if (reason < 4) {
        ++timber_gaps_[job][reason][month];
      }
      timber_placed_[job][month] += gap.placed;
    }
  }

 public:
  /// @brief Boss [26]'s line: per timber job and the plan's reason (no_horse, crew_cap, no_hands,
  ///        road), y4-y16 by month, the job-days left short; the hands placed on them; the
  ///        horses the morning left.
  void PrintTimberGaps(std::ostream& out, std::uint64_t seed) const {
    static constexpr std::array<std::string_view, kTimberJobs> kJobs = {
        "felling", "stand", "supply"};
    static constexpr std::array<std::string_view, 4> kReasons = {
        "no_horse", "crew_cap", "no_hands", "road"};
    const auto months = [&out](const std::array<std::uint32_t, 12>& v) {
      for (std::uint32_t m = 0; m < 12; ++m) {
        out << (m == 0 ? " " : ":") << v[m];
      }
    };
    out << "population_curve: TIMBER1726 seed " << seed;
    for (std::size_t job = 0; job < kTimberJobs; ++job) {
      for (std::size_t reason = 0; reason < 4; ++reason) {
        out << ' ' << kJobs[job] << '_' << kReasons[reason];
        months(timber_gaps_[job][reason]);
      }
      out << ' ' << kJobs[job] << "_placed";
      months(timber_placed_[job]);
    }
    out << " horses_left";
    months(horses_left_);
    out << '\n';
  }

  /// @brief Boss [24]'s line: horses by year (adults and foals, mean head; handed; bought), and
  ///        y4-y16 by month the horse-days ridden by use and the adults not ridden at hour 2.
  void PrintHorses(std::ostream& out, std::uint64_t seed) const {
    static constexpr std::array<std::string_view, kUseCount> kUses = {
        "field", "timber", "heap", "herd", "unit", "road", "extraction", "people_or_other"};
    out << "population_curve: HORSE1726 seed " << seed;
    const auto mean = [](double sum) { return std::lround(sum / core::kDaysPerYear); };
    out << " adults";
    for (std::uint32_t y = 0; y < 16; ++y) {
      out << (y == 0 ? " " : ":") << mean(horses_.adults[y]);
    }
    out << " foals";
    for (std::uint32_t y = 0; y < 16; ++y) {
      out << (y == 0 ? " " : ":") << mean(horses_.foals[y]);
    }
    out << " handed";
    for (std::uint32_t y = 0; y < 16; ++y) {
      out << (y == 0 ? " " : ":") << horses_.handed[y];
    }
    out << " bought";
    for (std::uint32_t y = 0; y < 16; ++y) {
      out << (y == 0 ? " " : ":") << horses_.bought[y];
    }
    for (std::size_t use = 0; use < kUseCount; ++use) {
      out << " m_" << kUses[use];
      for (std::uint32_t m = 0; m < 12; ++m) {
        out << (m == 0 ? " " : ":") << horses_.by_month[use][m];
      }
    }
    out << " m_idle";
    for (std::uint32_t m = 0; m < 12; ++m) {
      out << (m == 0 ? " " : ":") << horses_.idle_by_month[m];
    }
    out << '\n';
  }

 private:
  /// Hour 2's census for PrintSupply; keeps today's supply carters for tomorrow's picks.
  void CountSupply(const core::WorldState& world, std::uint32_t year_index) {
    std::set<std::uint32_t> sites;
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      const auto phase = unit.construction.phase;
      if (phase == core::ConstructionPhase::kDelivering ||
          phase == core::ConstructionPhase::kBuilding) {
        sites.insert(world.units.row_ids[row].value);
      }
      if (unit.type.value == house_type_ && phase == core::ConstructionPhase::kDelivering &&
          year_index < kSplitYears) {
        ++supply_.wait_sites[year_index];
      }
    }
    std::set<std::uint32_t> carters;
    for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
      const core::WorkAssignment& work = world.residents.rows[row].work;
      const bool supply = work.kind == core::WorkKind::kHauling && sites.contains(work.unit.value);
      const bool field =
          work.kind == core::WorkKind::kPlowing || work.kind == core::WorkKind::kHarrowing ||
          work.kind == core::WorkKind::kSowing || work.kind == core::WorkKind::kHarvest;
      if (supply) {
        carters.insert(world.residents.row_ids[row].value);
      }
      if (year_index >= kSplitYears) {
        continue;
      }
      supply_.carters[year_index] += supply ? 1U : 0U;
      supply_.felling[year_index] += work.kind == core::WorkKind::kFelling ? 1U : 0U;
      supply_.stand_haul[year_index] +=
          work.kind == core::WorkKind::kHauling && work.stand.value != core::kInvalidEntityIdValue
              ? 1U
              : 0U;
      if (work.rides_horse != 0) {
        ++(field    ? supply_.horse_field[year_index]
           : supply ? supply_.horse_supply[year_index]
                    : supply_.horse_other[year_index]);
      }
    }
    carters_yesterday_ = std::move(carters);
    CountHorses(world, year_index, sites);      // boss [24]
    CountTimberGaps(world, year_index, sites);  // boss [26]
  }

 public:
  /// Adults with no work at hour 2 who are not resting (rest above the trace's tired line): the
  /// village's idle hands, to read the crew's unplaced picks against.
  [[nodiscard]] static std::uint32_t IdleAdults(const core::WorldState& world) {
    std::uint32_t idle = 0;
    for (const core::ResidentRow& resident : world.residents.rows) {
      idle += resident.work.kind == core::WorkKind::kNone && resident.rest > kTiredRest &&
                      resident.idle_reason != core::IdleReason::kIdleReasonCount
                  ? 1U
                  : 0U;
    }
    return idle;
  }

  /// @brief The village's trace sums, and the house hands at hour 2 summed by year.
  void PrintCrewTrace(std::ostream& out, std::uint64_t seed) const {
    out << "population_curve: CREWTRACE1726S seed " << seed << " orders " << trace_.orders << " N "
        << trace_.n << " k " << trace_.k << " T " << trace_.t << " T_prev " << trace_.t_prev
        << " tired " << trace_.tired << " elsewhere " << trace_.elsewhere << " house_hands";
    for (std::uint32_t index = 0; index < kSplitYears; ++index) {
      out << (index == 0 ? " " : ":") << house_hands_[index];
    }
    out << '\n';
  }

  /// @brief The year's (b) line: crews ordered and hands asked, y1-y16.
  void PrintHouseCrews(std::ostream& out, std::uint64_t seed) const {
    out << "population_curve: CREWS1726 seed " << seed << " deadline_opened "
        << (deadline_opened_ ? 1 : 0) << " orders";
    for (std::uint32_t index = 0; index < kSplitYears; ++index) {
      out << (index == 0 ? " " : ":") << crew_orders_[index];
    }
    out << " hands";
    for (std::uint32_t index = 0; index < kSplitYears; ++index) {
      out << (index == 0 ? " " : ":") << crew_hands_[index];
    }
    out << '\n';
  }

 private:
  bool houses_deadline_on_ = true;
  std::uint32_t held_for_houses_in_a_row_ = 0;

  // #1726 (b): THE HOUSE TEAMS BEFORE THE BLOCK'S FIELDS (econ [10]: «while couples wait, the
  // house sites keep at least the off arm's house man-days, and the block's field work takes
  // what is left»). Once a block has opened by the deadline - with the houses short, which the
  // off arm never does, so its world is untouched - every day a couple waits, each house site
  // under construction gets a day order (kAssignCrew, #1778) of its whole brigade
  // (ConstructionState::max_crew) for the next working day: the morning seats them before the
  // accountant plans, so the fields take the hands that are left.
  bool house_crews_on_ = true;
  bool deadline_opened_ = false;
  /// The trace's «tired»: at or under this rest a pick is not sent (labor.csv
  /// rest_walkoff_threshold reads 10 in the shipped tables; the trace's own copy).
  static constexpr float kTiredRest = 10.0F;

  struct CrewTraceSums {
    std::uint32_t orders = 0;
    std::uint32_t n = 0;
    std::uint32_t k = 0;
    std::uint32_t t = 0;
    std::uint32_t t_prev = 0;
    std::uint32_t tired = 0;
    std::uint32_t elsewhere = 0;
  };

  CrewTraceSums trace_;
  core::SimDay trace_day_ = static_cast<core::SimDay>(-1);
  std::map<std::uint32_t, std::uint32_t> hands_yesterday_;
  std::array<std::uint32_t, 16> house_hands_{};
  std::array<std::uint32_t, 16> crew_orders_{};
  std::array<std::uint32_t, 16> crew_hands_{};

  void SendHouseCrews(core::ISimulation& simulation, const core::WorldState& world) {
    if (!house_crews_on_ || !deadline_opened_ || world.wedding_waits.rows.empty() ||
        house_type_ == core::kNoTableRow) {
      return;
    }
    std::vector<core::OrderRow> orders;
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      if (unit.type.value != house_type_ || unit.dead != 0 ||
          unit.construction.phase != core::ConstructionPhase::kBuilding ||
          unit.construction.max_crew == 0) {
        continue;
      }
      core::OrderRow order;
      order.kind = core::OrderKind::kAssignCrew;
      order.work = core::WorkKind::kConstruction;
      order.unit = world.units.row_ids[row];
      order.crew_size = unit.construction.max_crew;
      orders.push_back(order);
    }
    if (orders.empty()) {
      return;
    }
    const std::uint32_t index = world.calendar.day / core::kDaysPerYear;
    if (index < crew_orders_.size()) {
      crew_orders_[index] += static_cast<std::uint32_t>(orders.size());
      for (const core::OrderRow& order : orders) {
        crew_hands_[index] += order.crew_size;
      }
    }
    simulation.StageOrders(std::span<const core::OrderRow>(orders), {});
  }

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
  };

  /// THE HOUSES ARE BUILT (the human, 24 October 2026: «Только когда дома
  /// построены», tasks.db #1373; econ's measurable form, fields-grow-with-
  /// eaters «24 October» §1): the lamp «жилья не хватает» is out - through
  /// the alarm door the player reads, CollectAlarms - and no wooden_house
  /// site stands at level 0, marked, in delivery or building. Read and
  /// printed at every planning moment, whatever the food says, so the
  /// condition's village-years are counted where the rule could not act.
  bool HousesBuilt(const core::ISimulation& simulation,
                   const core::WorldState& world,
                   std::uint32_t year) {
    alarms_.clear();
    simulation.CollectAlarms(alarms_);
    const bool lamp_lit = std::ranges::any_of(alarms_, [](const core::Alarm& alarm) {
      return alarm.kind == core::AlarmKind::kHousingShort;
    });
    std::uint32_t house_sites = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      house_sites += unit.type.value == house_type_ && unit.level == 0 && unit.dead == 0 ? 1U : 0U;
    }
    // BOTH HALVES, the human's choice «1» of 24 October (tasks.db #1376). The
    // lamp-only arm (d81d66f6, measured, not kept) opened all 18 blocks by y7
    // at A's cost: April-June construction -37.5 %, three plan failures.
    const bool built = house_type_ != core::kNoTableRow && !lamp_lit && house_sites == 0;
    ++planning_years_;
    houses_built_years_ += built ? 1U : 0U;
    std::cout << "fallow_policy: year " << year << " houses " << (built ? "BUILT" : "short")
              << " (lamp " << (lamp_lit ? "lit" : "out") << ", house sites at level 0 "
              << house_sites << ")\n";
    return built;
  }

  void ReadTables(const core::ITableSet& tables) {
    if (const core::ITable* const kinds = tables.FindTable("livestock")) {
      horse_kind_ = kinds->FindRowByKey("horse");  // boss [24]'s horse count
    }
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
    house_type_ = types->FindRowByKey("wooden_house");
    if (const core::ITable* const goods = tables.FindTable("resources")) {  // #1726's split
      const std::array<std::string_view, 5> crop_keys = {"rye", "oat", "barley", "potato", "wheat"};
      for (std::size_t crop = 0; crop < crop_keys.size(); ++crop) {
        split_crops_[crop] = goods->FindRowByKey(crop_keys[crop]);
        if (crop != 3 && split_crops_[crop] != core::kNoTableRow) {
          grains_.push_back(split_crops_[crop]);
        }
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

  /// #1692's hold print: one turn, with whether the next block holds a field mown as it lies.
  /// #1726's B1 (econ-boss-1737-1726-gates [1]): the April-June construction man-days of years
  /// 1-6, read off the open book's work_days_by_kind as the day's rise (the book re-sets at the
  /// turn, so a fall is a new year and is not counted).
  void CountSpringBuilding(const core::WorldState& world) {
    const auto kind = static_cast<std::size_t>(core::WorkKind::kConstruction);
    const float total = world.ledger.current.work_days_by_kind[kind];
    const std::uint32_t year = world.ledger.current.year;
    const auto month = static_cast<std::uint8_t>(world.calendar.date.month);
    const bool spring = month >= static_cast<std::uint8_t>(core::Month::kApril) &&
                        month <= static_cast<std::uint8_t>(core::Month::kJune);
    if (spring && year >= 1 && year <= spring_building_.size() && total > building_last_) {
      spring_building_[year - 1U] += total - building_last_;
    }
    building_last_ = total;
    CountTheSplit(world);
  }

  /// #1726's split (boss, core-econ-1726-canon-pair [5]-[7]): by year 1-16, the building's man-days
  /// on house sites and on every other site (each site's labor_days_remaining as it falls
  /// day to day), the field man-days (the open book's plough, harrow, sow and reap kinds as
  /// they rise), and at each turn the closed book's grain due, manure carted and harvest by
  /// crop.
  void CountTheSplit(const core::WorldState& world) {
    const std::uint32_t year = world.ledger.current.year;
    if (year < 1 || year > kSplitYears) {
      return;
    }
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      if (!(unit.construction.labor_days_total > 0.0F)) {
        continue;  // no works on it
      }
      const std::uint32_t id = world.units.row_ids[row].value;
      const float left = unit.construction.labor_days_remaining;
      const auto seen = site_left_.find(id);
      if (seen != site_left_.end() && left < seen->second) {
        (unit.type.value == house_type_ ? house_days_ : other_days_)[year - 1U] +=
            seen->second - left;
      }
      site_left_[id] = left;
    }
    float field = 0.0F;
    for (const core::WorkKind kind : {core::WorkKind::kPlowing,
                                      core::WorkKind::kHarrowing,
                                      core::WorkKind::kSowing,
                                      core::WorkKind::kHarvest}) {
      field += world.ledger.current.work_days_by_kind[static_cast<std::size_t>(kind)];
    }
    if (field > field_last_) {
      field_days_[year - 1U] += field - field_last_;
    }
    field_last_ = field;  // the book re-sets at the turn: a fall is a new year, not counted
    const std::uint32_t closed = world.ledger.closed.year;
    if (closed >= 1 && closed <= kSplitYears && !turn_read_[closed - 1U]) {
      turn_read_[closed - 1U] = true;
      const core::YearLedger& book = world.ledger.closed;
      const auto of = [](const core::ResourceAmounts& amounts, std::uint32_t resource) {
        return resource < amounts.size() ? amounts[resource] : core::Grams{0};
      };
      for (const std::uint32_t grain : grains_) {
        grain_due_[closed - 1U] += of(book.plan_due, grain);
      }
      manure_[closed - 1U] = book.manure_carted;
      for (std::size_t crop = 0; crop < split_crops_.size(); ++crop) {
        harvest_[closed - 1U][crop] = of(book.harvest, split_crops_[crop]);
      }
    }
  }

 public:
  /// @brief #1726's split print, a line a year: «FALLOW1726S seed S y Y house_md H other_md O
  ///        field_md F grain_due_t G manure_t M harvest_t rye:oat:barley:potato:wheat».
  void PrintSplit(std::ostream& out, std::uint64_t seed) const {
    for (std::uint32_t index = 0; index < kSplitYears; ++index) {
      out << "population_curve: FALLOW1726S seed " << seed << " y " << index + 1U << " house_md "
          << std::lround(house_days_[index]) << " other_md " << std::lround(other_days_[index])
          << " field_md " << std::lround(field_days_[index]) << " grain_due_t "
          << grain_due_[index] / 1'000'000 << " manure_t " << manure_[index] / 1'000'000
          << " harvest_t";
      for (std::size_t crop = 0; crop < split_crops_.size(); ++crop) {
        out << (crop == 0 ? " " : ":") << harvest_[index][crop] / 1'000'000;
      }
      out << '\n';
    }
  }

 private:
  static constexpr std::uint32_t kSplitYears = 16;
  std::map<std::uint32_t, float> site_left_;
  std::array<float, kSplitYears> house_days_{};
  std::array<float, kSplitYears> other_days_{};
  std::array<float, kSplitYears> field_days_{};
  float field_last_ = 0.0F;
  std::array<bool, kSplitYears> turn_read_{};
  std::array<core::Grams, kSplitYears> grain_due_{};
  std::array<core::Grams, kSplitYears> manure_{};
  std::array<std::array<core::Grams, 5>, kSplitYears> harvest_{};
  std::vector<std::uint32_t> grains_;  ///< rye, oat, barley, wheat rows
  std::array<std::uint32_t, 5> split_crops_{core::kNoTableRow,
                                            core::kNoTableRow,
                                            core::kNoTableRow,
                                            core::kNoTableRow,
                                            core::kNoTableRow};  ///< rye oat barley potato wheat

 public:
  /// @brief #1726's print: «FALLOW1726 seed S spring_building_man_days y1..y6 a b c d e f
  ///        block1_year Y» - April-June construction man-days by year, and the year the first
  ///        block opened (0 never).
  void PrintSpringBuilding(std::ostream& out, std::uint64_t seed) const {
    out << "population_curve: FALLOW1726 seed " << seed << " spring_building_man_days";
    for (const float days : spring_building_) {
      out << ' ' << std::lround(days);
    }
    out << " block1_year " << (opened_in_year_.empty() ? 0U : opened_in_year_.front()) << '\n';
  }

 private:
  std::array<float, 6> spring_building_{};
  float building_last_ = 0.0F;

  void NoteTurn(const core::WorldState& world, std::uint32_t year, char code) {
    bool mown = false;
    for (const core::FieldId id : NextBlockFields(world)) {
      const std::uint32_t row = core::FindRow(world.fields, id);
      mown = mown || (row != core::kNoRow && world.fields.rows[row].mown_fallow != 0);
    }
    turns_ += " " + std::to_string(year) + ":" + code + ":" + (mown ? "1" : "0");
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
  /// unit_types' wooden_house row: a site of it at level 0 holds a block.
  std::uint32_t house_type_ = core::kNoTableRow;
  /// The village-years the planning moment read, and of them those whose
  /// houses were built (the instrument of the houses-first rule).
  std::uint32_t planning_years_ = 0;
  std::uint32_t houses_built_years_ = 0;
  std::vector<core::Alarm> alarms_;
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
  std::string turns_;  ///< #1692's hold print

  std::vector<std::pair<std::uint32_t, double>> block_potato_seed_t_;
  std::vector<std::pair<std::uint32_t, double>> block_potato_on_field_t_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_FALLOW_POLICY_H_
