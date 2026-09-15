/// @file
/// @brief The houses the run's chairman puts up for the couples waiting for
/// one and the families left without a roof.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THE RUN BUILDS THIS. Until 2026-09-14 the core raised a house from
/// nothing for every wedding with no free house and every family whose old
/// house fell — a STUB that overrode life-cycle §12 ("a wedding only when a
/// free house is ready") and made the thirty-year population an upper bound.
/// The stub is gone (boss, parcel 257): a house now comes only from the
/// chairman's building. A run has no chairman, so it plays one here, as it
/// does for the granaries (fixture_policy.h) — and says so out loud.

#ifndef TESTS_RUN_COMMON_HOUSE_POLICY_H_
#define TESTS_RUN_COMMON_HOUSE_POLICY_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core_catalog/definitions.h"
#include "core_common/family_state.h"
#include "core_common/order_state.h"
#include "core_common/plot.h"
#include "core_common/unit_state.h"
#include "core_common/wedding_state.h"
#include "core_common/world_state.h"
#include "core_tables/stub_tables.h"
#include "core_tables/tables.h"
#include "core_world/world.h"
#include "start_gate.h"

namespace run {

/// @brief Marks and starts wooden houses while the village has fewer free
/// houses, finished or going up, than couples waiting and families without a
/// roof — a few sites at a time, at the nearest free place to the village.
class HousePolicy {
 public:
  explicit HousePolicy(const core::ITableSet& tables) {
    const core::ITable* types = tables.FindTable("unit_types");
    const std::uint32_t row =
        types == nullptr ? core::kNoTableRow : types->FindRowByKey("wooden_house");
    house_ = row == core::kNoTableRow ? core::UnitTypeId{}
                                      : core::UnitTypeId{static_cast<std::uint16_t>(row)};
    std::string error;
    core::LoadDefinitions(tables, core::StubTables::kAllowed, definitions_, error);
    if (const core::ITable* resources = tables.FindTable("resources")) {
      const std::uint32_t key_col = resources->FindColumn("key");
      for (std::uint32_t index = 0; index < resources->RowCount(); ++index) {
        resource_keys_.emplace_back(resources->CellText(index, key_col));
      }
    }
  }

  /// @brief The question asked before every start (start_gate.h).
  void SetStartGate(StartGate gate) { start_gate_ = std::move(gate); }

  /// @brief How many house sites may stand marked or going up at once.
  void SetSitesAtOnce(std::uint32_t sites) { sites_at_once_ = sites; }

  /// @brief One day of the chairman's attention. Call once a day.
  /// @param farm_first No house starts today: the farm's own shortage — a
  /// store for the harvest or a roof for the herds — has a site waiting for
  /// its recipe (FixturePolicy::HoldsHousesBack; boss, parcel 298).
  void RunDay(core::ISimulation& simulation, bool farm_first) {
    if (house_.value == core::kInvalidDefIdValue) {
      return;
    }
    const core::WorldState& world = simulation.CompletedState();
    std::vector<core::OrderRow> orders;
    std::uint32_t sites = 0;
    std::uint32_t free_houses = 0;
    bool an_older_site_waits = false;
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      if (IsHousing(unit) && unit.level > 0 &&
          unit.household.value == core::kInvalidEntityIdValue) {
        ++free_houses;
      }
      // THE FARM'S SHORTAGE FIRST, THEN HOUSES IN MARKING ORDER (boss, parcel
      // 298). Starting a house the day its smaller recipe was covered left the
      // granary marked before it waiting for years: on nine seeds 1 granary
      // stood against 7 without the materials check, the harvest had nowhere
      // to go and the plan failed 27 years of 30 (parcel 297). The chairman
      // builds what the farm lacks most — a store while the harvest waits, a
      // roof while animals stand out (`farm_first`) — and then the houses, the
      // older mark first; everything else comes after and holds nothing back.
      if (unit.type.value != house_.value || unit.level != 0) {
        continue;
      }
      const bool marked = unit.construction.phase == core::ConstructionPhase::kMarked;
      const std::vector<core::MaterialShortfall> shortfall =
          marked ? simulation.MaterialsShortFor(world.units.row_ids[row])
                 : std::vector<core::MaterialShortfall>{};
      const bool short_of_recipe = !shortfall.empty();
      // What the recipe lacks, line by line (debt (a)): a site-day short of
      // two lines counts under both.
      for (const core::MaterialShortfall& line : shortfall) {
        ++short_by_resource_[line.resource.value];
      }
      ++sites;
      // Pegs and string: marked, and started the day its recipe is covered —
      // the chairman asks the construction door first (construction design
      // §6) rather than sending a start the core would refuse.
      const bool gate_open =
          GateOpen(start_gate_, world, unit.type, unit.construction.target_level);
      if (marked && !short_of_recipe && !an_older_site_waits && !farm_first && gate_open) {
        core::OrderRow start;
        start.kind = core::OrderKind::kStartBuild;
        start.unit = world.units.row_ids[row];
        orders.push_back(start);
      }
      // THE QUEUE BY CAUSE (debt (a), boss parcel 320): each site-day of a
      // marked house counted under the FIRST rule that held it, in the order
      // the start asks them.
      QueueTally& tally = TallyOf(world);
      if (!marked) {
        ++tally.going_up;
      } else if (short_of_recipe) {
        ++tally.short_of_recipe;
      } else if (farm_first) {
        ++tally.farm_first;
      } else if (an_older_site_waits) {
        ++tally.older_site_waits;
      } else if (!gate_open) {
        ++tally.saw_gate;
      } else {
        ++tally.started;
      }
      an_older_site_waits = an_older_site_waits || short_of_recipe;
    }
    std::uint32_t roofless = 0;
    for (const core::FamilyRow& family : world.families.rows) {
      roofless += family.house.value == core::kInvalidEntityIdValue ? 1U : 0U;
    }
    // AND THE HOUSES ABOUT TO FALL. A chairman sees a rotting roof long
    // before it comes down, and building only once it has come down leaves
    // the family in the cold for the whole of a site's life: the first run of
    // this policy lost 69 of 111 people in two winters that way.
    std::uint32_t rotting = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      rotting += IsHousing(unit) && unit.level > 0 &&
                         unit.household.value != core::kInvalidEntityIdValue &&
                         unit.wear >= kRottingWear
                     ? 1U
                     : 0U;
    }
    const auto wanted =
        static_cast<std::uint32_t>(world.wedding_waits.rows.size()) + roofless + rotting;
    // The houses wanted and not even marked, by why: the three-site cap, the
    // two-day pause between marks, or nothing (marked today).
    if (wanted > free_houses + sites) {
      QueueTally& tally = TallyOf(world);
      const std::uint32_t unmarked = wanted - free_houses - sites;
      if (sites >= sites_at_once_) {
        tally.cap_held += unmarked;
      } else if (cooldown_ > 0) {
        tally.cooldown_held += unmarked;
      }
    }
    if (wanted > free_houses + sites && sites < sites_at_once_ && cooldown_ == 0) {
      core::OrderRow mark;
      mark.kind = core::OrderKind::kBuildUnit;
      mark.unit_type = house_;
      const std::vector<float>& radii = definitions_.units.keep_out_radius_m;
      const float radius = house_.value < radii.size() ? radii[house_.value] : 0.0F;
      mark.position =
          core::FreePlot(world.units, definitions_.Plots(), VillageCentre(world), radius);
      orders.push_back(mark);
      ++ordered_;
      cooldown_ = kCooldownDays;
    } else if (cooldown_ > 0) {
      --cooldown_;
    }
    if (!orders.empty()) {
      simulation.StageOrders(std::span<const core::OrderRow>(orders.data(), orders.size()), {});
    }
  }

  /// @brief The fixture difference, in words, for the run to print BEFORE it
  /// measures anything.
  static void Declare(std::string_view run_name) {
    std::cout << run_name
              << ": FIXTURE DIFFERS FROM THE START CANON — the run's chairman builds WOODEN "
                 "HOUSES while couples wait for one or families have no roof, a few sites at a "
                 "time; the core raises no house from nothing any more (boss, parcel 257)\n";
  }

  /// @brief What the fixture did, for the run to print at the end.
  void Report(const core::WorldState& world, std::string_view run_name) const {
    std::uint32_t built = 0;
    std::uint32_t marked = 0;
    std::uint32_t delivering = 0;
    std::uint32_t building = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.type.value != house_.value) {
        continue;
      }
      built += unit.level > 0 ? 1U : 0U;
      if (unit.level == 0) {
        marked += unit.construction.phase == core::ConstructionPhase::kMarked ? 1U : 0U;
        delivering += unit.construction.phase == core::ConstructionPhase::kDelivering ? 1U : 0U;
        building += unit.construction.phase == core::ConstructionPhase::kBuilding ? 1U : 0U;
      }
    }
    std::cout << run_name << ": house sites at the end — " << marked << " marked, " << delivering
              << " waiting on materials, " << building << " being built\n";
    std::uint32_t roofless = 0;
    std::uint32_t in_tents = 0;
    for (const core::FamilyRow& family : world.families.rows) {
      roofless += family.house.value == core::kInvalidEntityIdValue ? 1U : 0U;
      in_tents += family.in_tent;
    }
    std::cout << run_name << ": the run's chairman marked " << ordered_ << " wooden houses; "
              << built << " stand; " << world.wedding_waits.rows.size()
              << " couples wait for a house and " << roofless << " families have no roof ("
              << in_tents << " in tents) at the end\n";
    ReportQueue(run_name);
  }

  /// @brief The queue by cause in four windows of years (debt (a)): site-days
  /// of marked houses by the rule that held them, and house-days wanted but
  /// not marked.
  void ReportQueue(std::string_view run_name) const {
    constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 4> kWindows = {
        {{1, 5}, {6, 10}, {11, 15}, {16, 30}}};
    for (const auto& [first, last] : kWindows) {
      QueueTally sum;
      for (std::uint32_t year = first; year <= last && year <= years_.size(); ++year) {
        const QueueTally& one = years_[year - 1];
        sum.going_up += one.going_up;
        sum.short_of_recipe += one.short_of_recipe;
        sum.farm_first += one.farm_first;
        sum.older_site_waits += one.older_site_waits;
        sum.saw_gate += one.saw_gate;
        sum.started += one.started;
        sum.cap_held += one.cap_held;
        sum.cooldown_held += one.cooldown_held;
      }
      std::cout << run_name << ":   house queue years " << first << "-" << last
                << " — site-days: going up " << sum.going_up << ", short of recipe "
                << sum.short_of_recipe << ", farm first " << sum.farm_first << ", older site waits "
                << sum.older_site_waits << ", saw gate " << sum.saw_gate << ", started "
                << sum.started << "; house-days unmarked: cap " << sum.cap_held << ", cooldown "
                << sum.cooldown_held << '\n';
    }
    std::cout << run_name << ":   house site-days short, by line —";
    for (const auto& [resource, days] : short_by_resource_) {
      std::cout << ' ' << (resource < resource_keys_.size() ? resource_keys_[resource] : "?") << ' '
                << days;
    }
    std::cout << '\n';
  }

 private:
  /// Sites going up at once. A chairman does not put the whole village on
  /// house building; three sites is a brigade each, and the queue still
  /// shortens while the fields are worked. P2 (boss, parcel 314) measures six,
  /// and only together with the saw's board reserve.
  std::uint32_t sites_at_once_ = 3;

  static constexpr std::uint32_t kCooldownDays = 2;

  /// Wear from which a lived-in house counts as one to replace. At the old
  /// houses' twelve years to collapse (construction.csv) it is some two
  /// years of warning — a site's materials and labour, with room to spare.
  static constexpr float kRottingWear = 80.0F;

  bool IsHousing(const core::UnitRow& unit) const {
    return unit.type.value < definitions_.units.is_housing.size() &&
           definitions_.units.is_housing[unit.type.value] != 0;
  }

 public:
  /// @brief The mean position of the houses people live in: the wanted spot,
  /// which FreePlot moves to the nearest free place. Public for the school
  /// (school_policy.h), which is put up among the same houses.
  static core::Vec2 VillageCentre(const core::WorldState& world) {
    core::Vec2 sum{.x = 0.0F, .y = 0.0F};
    std::uint32_t seen = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.household.value == core::kInvalidEntityIdValue) {
        continue;
      }
      sum.x += unit.position.x;
      sum.y += unit.position.y;
      ++seen;
    }
    if (seen == 0) {
      return core::Vec2{.x = 150.0F, .y = 150.0F};
    }
    return core::Vec2{.x = sum.x / static_cast<float>(seen), .y = sum.y / static_cast<float>(seen)};
  }

 private:
  /// One year's queue by cause (ReportQueue).
  struct QueueTally {
    std::uint64_t going_up = 0;
    std::uint64_t short_of_recipe = 0;
    std::uint64_t farm_first = 0;
    std::uint64_t older_site_waits = 0;
    std::uint64_t saw_gate = 0;
    std::uint64_t started = 0;
    std::uint64_t cap_held = 0;
    std::uint64_t cooldown_held = 0;
  };

  QueueTally& TallyOf(const core::WorldState& world) {
    const std::size_t year = world.calendar.day / core::kDaysPerYear;
    if (years_.size() <= year) {
      years_.resize(year + 1);
    }
    return years_[year];
  }

  std::vector<QueueTally> years_;

  /// Site-days short, by ResourceId value; ordered so the report is stable.
  std::map<std::uint16_t, std::uint64_t> short_by_resource_;

  std::vector<std::string> resource_keys_;

  core::UnitTypeId house_;

  core::Definitions definitions_;

  std::uint32_t cooldown_ = 0;

  std::uint32_t ordered_ = 0;

  StartGate start_gate_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_HOUSE_POLICY_H_
