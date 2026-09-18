/// @file
/// @brief The run's chairman raises the KOLKHOZ's buildings to the level the
/// era transition asks of them, one at a time.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THIS EXISTS. Measured 2026-09-18, nine villages over thirty-three
/// years: ONE building of every village had ever reached level two, and it
/// was the chairman's yard rising to its stable by a rule of its own. No
/// fixture sent `kUpgradeUnit` for anything else, so the transition block
/// «все юниты эпохи доведены до 2 уровня» stood shut in every year — a
/// blocker measuring the fixture rather than the world, which is the third of
/// these this tree has found in two days.
///
/// THE FAMILIES' HOUSES ARE NOT IN IT (boss, design commit ac48054e). Two
/// lines of the design decide it and neither is about how much work it would
/// be: the funds component already says «жилые дома семей не входят», and
/// housing §6 says «уровень дома на комфорт не влияет — изба первого уровня в
/// порядке тоже 100 %». Requiring a level of something the game calls
/// unimportant is requiring nothing worth having. The measured denominator
/// fell from 180 buildings to 51 with that one reading, which is the village
/// the design's «несколько сезонов, а не десятилетие» was written about.
///
/// BY THE CATALOGUE AND NOT BY NAME, as the social objects are: every type
/// the score counts is a type this raises, so a kind added to the design base
/// is one the fixture keeps at level without anybody coming back here.
///
/// ONE AT A TIME, AND AFTER EVERYTHING ELSE. An upgrade is the least urgent
/// thing a chairman does — the design calls it «наведение порядка, а не
/// перестройка» — so it waits behind the farm's own shortage, the houses, the
/// school, the office and the social objects, and never has two going at once.

#ifndef TESTS_RUN_COMMON_UPGRADE_POLICY_H_
#define TESTS_RUN_COMMON_UPGRADE_POLICY_H_

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core_common/order_state.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/era_readiness.h"
#include "core_world/world.h"
#include "start_gate.h"

namespace run {

/// @brief Raises one kolkhoz building a time to the era's level.
class UpgradePolicy {
 public:
  explicit UpgradePolicy(const core::ITableSet& tables) {
    const core::ReadinessCatalog catalog = core::ReadReadinessCatalog(tables, core::Epoch::kOne);
    kolkhoz_ = catalog.kolkhoz_types;
    // THE LADDER, BECAUSE MOST TYPES HAVE NONE. Of 111 types in
    // unit_levels.csv, 77 carry a single level and cannot be upgraded at all —
    // units rules §11 says so in words: «у остальных пока только первая, и это
    // значит „ещё не расписано"». Without this the policy ordered the first
    // un-upgradable building it met, was refused with kRuleForbids, returned,
    // and did the same the next day: 428 orders a village, one building
    // raised, and the world identical to a policy that never ran.
    const core::ITable* types = tables.FindTable("unit_types");
    const core::ITable* levels = tables.FindTable("unit_levels");
    if (types == nullptr || levels == nullptr) {
      return;
    }
    ladder_.assign(types->RowCount(), 0);
    const std::uint32_t unit_column = levels->FindColumn("unit");
    if (unit_column == core::kNoTableColumn) {
      return;
    }
    // The era each rung opens in, read beside the ladder: the verdict at the
    // unit splits its refusals by it (ReadAtSubject).
    const std::uint32_t level_column = levels->FindColumn("level");
    const std::uint32_t era_column = levels->FindColumn("era");
    rung_era_.assign(types->RowCount(), {});
    for (std::uint32_t row = 0; row < levels->RowCount(); ++row) {
      const std::uint32_t type_row = types->FindRowByKey(levels->CellText(row, unit_column));
      if (type_row != core::kNoTableRow && type_row < ladder_.size()) {
        ++ladder_[type_row];
        if (level_column != core::kNoTableColumn && era_column != core::kNoTableColumn) {
          const auto level = static_cast<std::size_t>(
              std::strtoul(std::string(levels->CellText(row, level_column)).c_str(), nullptr, 10));
          if (level < kRungs) {
            rung_era_[type_row][level] = static_cast<std::uint8_t>(
                std::strtoul(std::string(levels->CellText(row, era_column)).c_str(), nullptr, 10));
          }
        }
      }
    }
  }

  /// @brief The question asked before every upgrade (start_gate.h).
  void SetStartGate(StartGate gate) { start_gate_ = std::move(gate); }

  /// @brief One day of the chairman's attention. Call once a day, last of the
  ///        buildings.
  /// @param farm_first The farm's own shortage has a site waiting: nothing is
  ///        raised today.
  void RunDay(core::ISimulation& simulation, bool farm_first) {
    if (kolkhoz_.empty() || farm_first) {
      return;
    }
    const core::WorldState& world = simulation.CompletedState();
    if (world.chairman.horses_stabled == 0) {
      return;  // the same scar as the office's: not before the farm stands
    }
    // ONE UPGRADE AT A TIME — AND ONLY AN UPGRADE COUNTS.
    //
    // WRITTEN THE WRONG WAY FIRST, one hour after removing the same mistake
    // from the social objects. The first version returned when ANY unit was
    // building, and the village has a house going up on almost every day of
    // thirty-three years, so the policy never acted once: measured 1 of 50.7
    // before and 1 of 50.7 after. A veto that reads "something is being
    // built" in a settlement that always builds is a veto that never lifts.
    //
    // A unit already standing (level >= 1) and building is one being RAISED;
    // a level-nought site is a new building and none of this policy's
    // business.
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.dead == 0 && unit.level >= 1 &&
          unit.construction.phase == core::ConstructionPhase::kBuilding) {
        return;
      }
    }
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      if (unit.level == 0 || unit.dead != 0 || unit.level >= kEraLevel || !Kolkhoz(unit.type)) {
        continue;
      }
      // A type whose ladder ends here has no next level to ask for, and
      // asking is not free: the refusal ends this day's attention.
      if (unit.type.value >= ladder_.size() || ladder_[unit.type.value] < kEraLevel) {
        continue;
      }
      const auto next = static_cast<std::uint8_t>(unit.level + 1U);
      if (!GateOpen(start_gate_, world, unit.type, next)) {
        continue;
      }
      // THE NEXT LEVEL'S RECIPE, and the door for it already existed.
      //
      // This call was added, measured to change the order count by zero
      // thousandths, and removed as "a guard that cannot fail" — and that
      // reading was wrong twice over. `MaterialsShortFor` answers precisely
      // this question for a standing unit with a ladder: construction_system
      // returns `ShortfallOf(row, level + 1)` for phase kNone, level > 0 and
      // a rung left. It returned empty then because every candidate the
      // policy could reach was a SINGLE-LEVEL type, for which there is no
      // next rung to be short of — so the refusals were kRuleForbids and the
      // guard was right to pass them.
      //
      // The lesson is the older one: a number that does not move says the
      // cause is elsewhere, and it does not say WHICH elsewhere. I read a
      // missing door out of it and offered to build one that was already
      // there.
      if (!simulation.MaterialsShortFor(world.units.row_ids[row]).empty()) {
        continue;
      }
      core::OrderRow order;
      order.kind = core::OrderKind::kUpgradeUnit;
      order.unit = world.units.row_ids[row];
      const std::array<core::OrderRow, 1> one = {order};
      simulation.StageOrders(std::span<const core::OrderRow>(one.data(), one.size()), {});
      ++ordered_;
      last_ = Placed{.unit = order.unit,
                     .type = unit.type,
                     .level = unit.level,
                     .phase = unit.construction.phase};
      return;
    }
  }

  /// @brief The fixture difference, in words, BEFORE the run measures.
  static void Declare(std::string_view run_name) {
    std::cout << run_name
              << ": FIXTURE DIFFERS FROM THE START CANON — once the chairman's yard stands, the "
                 "run's chairman raises the KOLKHOZ's buildings to level 2 one at a time, taking "
                 "the list from the design base rather than by name; the families' houses are "
                 "NOT in it, because a house at level one in good repair is a hundred-per-cent "
                 "house (housing §6) and the funds component already excludes them. Measured "
                 "before this existed: one building per village had ever reached level 2 in "
                 "thirty-three years, and it was the yard (boss, design ac48054e)\n";
  }

  std::uint32_t ordered() const { return ordered_; }

  /// @brief What became of the orders, read off the UNIT and not the book.
  struct Fates {
    /// Yesterday's order, judged by the unit today. The four sum to the
    /// orders read; an order placed on the run's last day is never read.
    std::uint32_t onto_open_site = 0;  ///< the unit already had a site when ordered
    std::uint32_t started = 0;         ///< a site opened for level + 1
    std::uint32_t refused = 0;         ///< no site, level unchanged, the rung IS this era's
    /// Refused where the next rung opens in a LATER era than the world's —
    /// the pair beside `refused`, so the era gate is counted, not inferred.
    std::uint32_t refused_later_era = 0;
    std::uint32_t other = 0;  ///< the unit gone, or anything else
    /// And every started upgrade, followed to its end.
    std::uint32_t finished = 0;   ///< the level rose to the target
    std::uint32_t abandoned = 0;  ///< the site closed at the old level
    std::uint32_t gone = 0;       ///< the unit left the world
    std::uint64_t days_to_finish = 0;
    /// Still open when the run ends, by phase.
    std::uint32_t open_delivering = 0;
    std::uint32_t open_building = 0;
    std::uint32_t open_other = 0;
  };

  /// @brief Reads yesterday's order and every open upgrade AT THE UNIT.
  /// Call once a day, BEFORE RunDay.
  ///
  /// WHY THE UNIT AND NOT THE BOOK. The first tally read the order book a
  /// day late, after the events slot had swept every settled row, and
  /// reported nought refusals from a book it could not see one order in —
  /// published as "the orders are accepted" and retracted (3af834f).
  /// Construction settles an upgrade in the step it reads it: a site opens
  /// for level + 1, or the unit is left as it was. So the unit, read the next
  /// day, IS the verdict, and it is not swept.
  void ReadAtSubject(const core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    const auto unit_of = [&world](core::UnitId id) -> const core::UnitRow* {
      for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
        if (world.units.row_ids[row].value == id.value && world.units.rows[row].dead == 0) {
          return &world.units.rows[row];
        }
      }
      return nullptr;
    };
    if (last_.has_value()) {
      const core::UnitRow* unit = unit_of(last_->unit);
      if (last_->phase != core::ConstructionPhase::kNone) {
        ++fates_.onto_open_site;
      } else if (unit == nullptr) {
        ++fates_.other;
      } else if (unit->construction.phase != core::ConstructionPhase::kNone &&
                 unit->construction.target_level == last_->level + 1U) {
        ++fates_.started;
        watching_.push_back({.unit = last_->unit,
                             .target = unit->construction.target_level,
                             .day = world.calendar.day});
      } else if (unit->construction.phase == core::ConstructionPhase::kNone &&
                 unit->level == last_->level) {
        const std::uint8_t era = RungEra(last_->type, last_->level + 1U);
        if (era > core::EpochHumanNumber(world.epoch)) {
          ++fates_.refused_later_era;
        } else {
          ++fates_.refused;
        }
      } else {
        ++fates_.other;
      }
      last_.reset();
    }
    std::erase_if(watching_, [&](const Watch& watch) {
      const core::UnitRow* unit = unit_of(watch.unit);
      if (unit == nullptr) {
        ++fates_.gone;
        return true;
      }
      if (unit->level >= watch.target) {
        ++fates_.finished;
        fates_.days_to_finish += world.calendar.day - watch.day;
        return true;
      }
      if (unit->construction.phase == core::ConstructionPhase::kNone) {
        ++fates_.abandoned;
        return true;
      }
      return false;
    });
  }

  /// @brief The era (1-based, as unit_levels.csv spells it) the rung `level`
  /// of `type` opens in; 0 when the ladder has no such rung.
  std::uint8_t RungEra(core::UnitTypeId type, std::uint32_t level) const {
    if (type.value >= rung_era_.size() || level >= kRungs) {
      return 0;
    }
    return rung_era_[type.value][level];
  }

  /// @brief The fates, with the upgrades still open counted by phase.
  Fates FatesAtEnd(const core::ISimulation& simulation) const {
    Fates fates = fates_;
    const core::WorldState& world = simulation.CompletedState();
    for (const Watch& watch : watching_) {
      for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
        if (world.units.row_ids[row].value != watch.unit.value) {
          continue;
        }
        const core::ConstructionPhase phase = world.units.rows[row].construction.phase;
        fates.open_delivering += phase == core::ConstructionPhase::kDelivering ? 1U : 0U;
        fates.open_building += phase == core::ConstructionPhase::kBuilding ? 1U : 0U;
        fates.open_other += phase != core::ConstructionPhase::kDelivering &&
                                    phase != core::ConstructionPhase::kBuilding
                                ? 1U
                                : 0U;
      }
    }
    return fates;
  }

 private:
  /// The order placed today, as the unit stood when it was placed.
  struct Placed {
    core::UnitId unit;
    core::UnitTypeId type;
    std::uint8_t level = 0;
    core::ConstructionPhase phase = core::ConstructionPhase::kNone;
  };

  /// A started upgrade being followed.
  struct Watch {
    core::UnitId unit;
    std::uint8_t target = 0;
    core::SimDay day = 0;
  };

  /// The level every Era I unit must reach for the I -> II transition (units
  /// rules §11). Not a stub: the design's own table.
  static constexpr std::uint8_t kEraLevel = 2;

  bool Kolkhoz(core::UnitTypeId type) const {
    for (const core::UnitTypeId id : kolkhoz_) {
      if (id.value == type.value) {
        return true;
      }
    }
    return false;
  }

  std::vector<core::UnitTypeId> kolkhoz_;

  /// How many levels each type has, dense by type row.
  std::vector<std::uint32_t> ladder_;

  /// Rungs kept per type, level 0 unused; the tables stop at three.
  static constexpr std::size_t kRungs = 6;

  /// The era each rung opens in, by type row and level.
  std::vector<std::array<std::uint8_t, kRungs>> rung_era_;

  StartGate start_gate_;

  std::uint32_t ordered_ = 0;

  std::optional<Placed> last_;
  std::vector<Watch> watching_;
  Fates fates_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_UPGRADE_POLICY_H_
