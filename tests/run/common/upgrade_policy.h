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
#include <iostream>
#include <map>
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
  // THE SCORE'S OWN CATALOGUE AND ITS OWN RULE, not a ladder parsed here. This
  // policy used to count rungs itself, and a count could not tell a second
  // rung of Epoch I from one of Epoch II: it ordered the same school to its
  // Epoch II rung day after day, and four orders in five were refused with
  // kGateClosed (2026-09-18). The level it now aims at is RequiredUnitLevel —
  // the one the block reads — so the fixture and the score cannot disagree
  // about what "at its level" means.
  explicit UpgradePolicy(const core::ITableSet& tables)
      : catalog_(core::ReadReadinessCatalog(tables, core::Epoch::kOne)) {
    ReadPens(tables);
  }

  /// @brief Whether a SECOND cattle yard waits for its warm (the cattle yard's
  ///        winter warm; econ's kdig ruling §4 and males-work-pair-ruling §2):
  ///        a level-1 cattle yard while a warm one already stands is raised
  ///        only when the kolkhoz's cattle heads today are above the warm
  ///        places standing — the herd expected at the November close, read
  ///        as today's — or once the village has 340 residents. The first pen
  ///        (no warm yard yet) is never held here.
  /// Measured on the warm pair: of eight second-pen warms in dm, three came on
  /// a day the warm places already held every head; with the rule nought of
  /// eight (the thread core-boss-c2-site-supply-2026-10-09 [97]).
  bool SecondPenWaits(const core::WorldState& world, const core::UnitRow& unit) const {
    if (unit.type.value != cattle_type_ || unit.level != 1 ||
        world.residents.rows.size() >= kSecondPenResidents) {
      return false;
    }
    float warm = 0.0F;
    for (const core::UnitRow& yard : world.units.rows) {
      if (yard.type.value == cattle_type_ && yard.level >= 2 && yard.dead == 0 &&
          static_cast<std::size_t>(yard.level) - 1U < cattle_places_.size()) {
        warm += cattle_places_[static_cast<std::size_t>(yard.level) - 1U];
      }
    }
    if (!(warm > 0.0F)) {
      return false;
    }
    float heads = 0.0F;
    for (const core::HerdRow& herd : world.herds.rows) {
      if (herd.household_owned == 0 && herd.kind.value < cattle_home_.size() &&
          cattle_home_[herd.kind.value] != 0) {
        heads += static_cast<float>(herd.adult_count + herd.juvenile_count + herd.newborn_count);
      }
    }
    return !(heads > warm);
  }

  /// @brief The question asked before every upgrade (start_gate.h).
  void SetStartGate(StartGate gate) { start_gate_ = std::move(gate); }

  /// @brief One day of the chairman's attention. Call once a day, last of the
  ///        buildings.
  /// @param farm_first The farm's own shortage has a site waiting: nothing is
  ///        raised today.
  void RunDay(core::ISimulation& simulation, bool farm_first) {
    if (catalog_.kolkhoz_types.empty()) {
      return;
    }
    const core::WorldState& world = simulation.CompletedState();
    // WHY NOTHING WAS RAISED TODAY, counted only on days a kolkhoz unit
    // stands below the era's level (2026-09-27, boss-core-epoch1-queue [19]:
    // seed 1931's bathhouse stood at level 1 for 1320 days and kept the era
    // shut, and the policy said nothing of why).
    const bool wanted = AnyBelowLevel(world);
    if (farm_first) {
      held_.farm_first += wanted ? 1U : 0U;
      return;
    }
    if (world.chairman.horses_stabled == 0) {
      held_.farm_not_standing += wanted ? 1U : 0U;
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
        held_.one_going_up += wanted ? 1U : 0U;
        return;
      }
    }
    // The FIRST unit below level's reason is the day's (one reason a day).
    bool reason_taken = false;
    const auto hold = [&reason_taken](std::uint32_t& counter) {
      if (!reason_taken) {
        ++counter;
        reason_taken = true;
      }
    };
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      if (unit.level == 0 || unit.dead != 0 || !Kolkhoz(unit.type)) {
        continue;
      }
      // At the level this era requires, or with no rung left that the era
      // has opened: nothing to ask for, and asking is not free — the refusal
      // ends this day's attention.
      if (unit.level >= core::RequiredUnitLevel(catalog_, unit.type, world.epoch, world)) {
        continue;
      }
      // A unit that is already a site — being delivered to, repaired,
      // insulated — cannot take an upgrade until that closes: 19 orders a
      // village went onto such units and were refused, measured the same day.
      if (unit.construction.phase != core::ConstructionPhase::kNone) {
        hold(held_.site_open);
        continue;
      }
      // A second cattle yard waits for the winter herd or 340 residents.
      if (SecondPenWaits(world, unit)) {
        hold(held_.second_pen_waits);
        continue;
      }
      const auto next = static_cast<std::uint8_t>(unit.level + 1U);
      if (!GateOpen(start_gate_, world, unit.type, next)) {
        hold(held_.gate_closed);
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
      const std::vector<core::MaterialShortfall> short_lines =
          simulation.MaterialsShortFor(world.units.row_ids[row]);
      if (!short_lines.empty()) {
        if (!reason_taken) {
          ++held_.short_by_resource[short_lines.front().resource.value];
        }
        hold(held_.materials_short);
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
    // Wanted, and no unit gave a reason: the instrument's own hole.
    held_.unexplained += wanted && !reason_taken ? 1U : 0U;
  }

  /// Days a kolkhoz unit stood below the era's level and nothing was
  /// raised, by the first reason that held it, in RunDay's order.
  struct Held {
    std::uint32_t farm_first = 0;         ///< the farm's own shortage waits for its recipe
    std::uint32_t farm_not_standing = 0;  ///< the chairman's yard not up yet
    std::uint32_t one_going_up = 0;       ///< another upgrade is building
    std::uint32_t site_open = 0;          ///< the unit is a site already (delivering, repair…)
    std::uint32_t gate_closed = 0;        ///< the start gate said no
    std::uint32_t materials_short = 0;    ///< the next rung's recipe is short
    std::uint32_t second_pen_waits = 0;   ///< a second cattle yard waits for its winter herd
    std::uint32_t unexplained = 0;        ///< none of the above: must stay nought
    /// The first short line on the materials_short days, by resource row.
    std::map<std::uint16_t, std::uint32_t> short_by_resource;
  };

  const Held& held() const { return held_; }

  /// @brief The row of the kolkhoz unit the era's level waits on — RunDay's
  /// first candidate: standing, below the level this era requires, no site
  /// open — or kNoRow (rise_watch.h). THE SAME KNOT AS THE YARD'S (2026-09-27,
  /// boss-core-epoch1-queue [21]-[22]): an upgrade is refused until its whole
  /// recipe is in the village, so it is no site, and the limit bought nothing
  /// for it — seed 1931's bathhouse waited 426 days with glass its first
  /// short line, and the era never came.
  std::uint32_t RowWaitingToRise(const core::WorldState& world) const {
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      if (unit.level != 0 && unit.dead == 0 && Kolkhoz(unit.type) &&
          unit.level < core::RequiredUnitLevel(catalog_, unit.type, world.epoch, world) &&
          unit.construction.phase == core::ConstructionPhase::kNone &&
          !SecondPenWaits(world, unit)) {
        return row;
      }
    }
    return core::kNoRow;
  }

  /// @brief The fixture difference, in words, BEFORE the run measures.
  static void Declare(std::string_view run_name) {
    std::cout << run_name
              << ": FIXTURE DIFFERS FROM THE START CANON — once the chairman's yard stands, the "
                 "run's chairman raises the KOLKHOZ's buildings one at a time to the level the "
                 "era requires — the highest rung the era has opened (epochs §6) — taking "
                 "the list from the design base rather than by name; the families' houses are "
                 "NOT in it, because a house at level one in good repair is a hundred-per-cent "
                 "house (housing §6) and the funds component already excludes them. Measured "
                 "before this existed: one building per village had ever reached level 2 in "
                 "thirty-three years, and it was the yard (boss, design ac48054e)\n";
  }

  std::uint32_t ordered() const { return ordered_; }

  /// @brief The unit whose upgrade order was placed and not yet read at the
  /// unit (ReadAtSubject) — the one a kOrderRefused of the steps between is
  /// about. Invalid when none is waiting.
  core::UnitId Awaiting() const { return last_.has_value() ? last_->unit : core::UnitId{}; }

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
  /// of `type` opens in; 0 when the ladder has no such rung. Read off the
  /// score's catalogue.
  std::uint8_t RungEra(core::UnitTypeId type, std::uint32_t level) const {
    if (type.value >= catalog_.rung_eras.size() || level == 0 ||
        level > catalog_.rung_eras[type.value].size()) {
      return 0;
    }
    return catalog_.rung_eras[type.value][level - 1U];
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

  bool Kolkhoz(core::UnitTypeId type) const {
    for (const core::UnitTypeId id : catalog_.kolkhoz_types) {
      if (id.value == type.value) {
        return true;
      }
    }
    return false;
  }

  /// A standing kolkhoz unit below the era's level: RunDay's own candidate
  /// test, before its reasons.
  bool AnyBelowLevel(const core::WorldState& world) const {
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.level != 0 && unit.dead == 0 && Kolkhoz(unit.type) &&
          unit.level < core::RequiredUnitLevel(catalog_, unit.type, world.epoch, world)) {
        return true;
      }
    }
    return false;
  }

  /// The cattle yard's row (unit_types.csv), the kinds whose home it is
  /// (livestock.csv home_unit), and its places by level (unit_levels.csv
  /// livestock_capacity_head) — what SecondPenWaits reads.
  void ReadPens(const core::ITableSet& tables) {
    const core::ITable* types = tables.FindTable("unit_types");
    const std::uint32_t cattle =
        types == nullptr ? core::kNoTableRow : types->FindRowByKey("cattle_yard");
    cattle_type_ =
        cattle == core::kNoTableRow ? core::kInvalidDefIdValue : static_cast<std::uint16_t>(cattle);
    const core::ITable* livestock = tables.FindTable("livestock");
    const std::uint32_t home =
        livestock == nullptr ? core::kNoTableColumn : livestock->FindColumn("home_unit");
    for (std::uint32_t row = 0; home != core::kNoTableColumn && row < livestock->RowCount();
         ++row) {
      cattle_home_.push_back(livestock->CellText(row, home) == "cattle_yard" ? 1U : 0U);
    }
    const core::ITable* levels = tables.FindTable("unit_levels");
    if (levels == nullptr) {
      return;
    }
    const std::uint32_t unit_column = levels->FindColumn("unit");
    const std::uint32_t level_column = levels->FindColumn("level");
    const std::uint32_t heads_column = levels->FindColumn("livestock_capacity_head");
    for (std::uint32_t row = 0; row < levels->RowCount(); ++row) {
      if (unit_column == core::kNoTableColumn ||
          levels->CellText(row, unit_column) != "cattle_yard") {
        continue;
      }
      const std::optional<float> level = levels->CellReal(row, level_column);
      const std::optional<float> heads = levels->CellReal(row, heads_column);
      if (!level || !(*level >= 1.0F)) {
        continue;
      }
      const auto index = static_cast<std::size_t>(*level) - 1U;
      if (cattle_places_.size() <= index) {
        cattle_places_.resize(index + 1U, 0.0F);
      }
      cattle_places_[index] = heads ? *heads : 0.0F;
    }
  }

  /// SecondPenWaits's residents, past which a second pen is warmed for the
  /// era's «units at level» whatever the herd (econ's kdig ruling §4).
  static constexpr std::size_t kSecondPenResidents = 340;

  std::uint16_t cattle_type_ = core::kInvalidDefIdValue;

  std::vector<std::uint8_t> cattle_home_;

  std::vector<float> cattle_places_;

  Held held_;

  /// The score's catalogue: the kolkhoz's types and the ladders by era.
  core::ReadinessCatalog catalog_;

  StartGate start_gate_;

  std::uint32_t ordered_ = 0;

  std::optional<Placed> last_;
  std::vector<Watch> watching_;
  Fates fates_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_UPGRADE_POLICY_H_
