/// @file
/// @brief CATCH2191 (#2191's arm; boss and econ's no-pump read, 26 Nov 2026): the run's
///        chairman patrols, the day after each moonlit night, the store the night's sacks took
///        most from (kCatchThief), and judges the catch by a fixed policy (kJudgeThief):
///        L never (every catch lapses), C the constable, E «закрыть глаза», F «найти аппарат»,
///        R the seven in turn. Off unless `--catch-arm=` names one (the canon sends no catch).
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// THE INSTRUMENT, a closed year: CATCH2191 - catches ordered and uncovered, judged by choice,
/// lapsed, the Детектив, Дисциплина, the authority, the district's reputation, the mean
/// attitude of the judged thieves' families at the year's end, and the Детектив's credits.
#ifndef TESTS_RUN_COMMON_CATCH_POLICY_H_
#define TESTS_RUN_COMMON_CATCH_POLICY_H_

#include <array>
#include <cstdint>
#include <map>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/event_state.h"
#include "core_common/order_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/thief_catch_state.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"

namespace run {

class CatchPolicy {
 public:
  /// @brief The arm by its letter (L, C, E, F, R); anything else leaves the policy off.
  void SetArm(char arm) {
    arm_ = arm;
    on_ = arm == 'L' || arm == 'C' || arm == 'E' || arm == 'F' || arm == 'R';
  }

  bool On() const { return on_; }

  /// @brief Once a day: patrol the last night's most-robbed store, judge an open catch.
  void RunDay(core::ISimulation& simulation) {
    if (!on_) {
      return;
    }
    const core::WorldState& world = simulation.CompletedState();
    CloseYearIfTurned(world);
    std::vector<core::OrderRow> orders;
    const core::ThiefCatchState& state = world.thief_catch;
    // JUDGE an open catch (not the lapse arm), the day after it opened.
    if (arm_ != 'L' && state.open_catch.thief.value != core::kInvalidEntityIdValue) {
      core::OrderRow judge;
      judge.kind = core::OrderKind::kJudgeThief;
      judge.resident = state.open_catch.thief;
      judge.amount = static_cast<core::Grams>(Choice());
      orders.push_back(judge);
      judged_families_.insert(FamilyOf(world, state.open_catch.thief));
    }
    // PATROL the night's most-robbed store, once a night.
    if (!state.last_night.empty() && state.last_night.front().day != patrolled_night_) {
      std::map<std::uint32_t, core::Grams> by_unit;
      for (const core::NightSack& sack : state.last_night) {
        by_unit[sack.unit.value] += sack.grams;
      }
      std::uint32_t unit = core::kInvalidEntityIdValue;
      core::Grams most = 0;
      for (const auto& [id, grams] : by_unit) {
        if (grams > most) {
          most = grams;
          unit = id;
        }
      }
      if (unit != core::kInvalidEntityIdValue) {
        core::OrderRow patrol;
        patrol.kind = core::OrderKind::kCatchThief;
        patrol.unit = core::UnitId{unit};
        orders.push_back(patrol);
        ++year_.patrols;
      }
      patrolled_night_ = state.last_night.front().day;
    }
    if (!orders.empty()) {
      simulation.StageOrders(std::span<const core::OrderRow>(orders), {});
    }
  }

  /// @brief After every step: the catches and judgements the step raised.
  void WatchStep(const core::WorldState& world) {
    if (!on_) {
      return;
    }
    for (const core::SimEvent& event : world.step_events) {
      if (event.kind == core::EventKind::kTheftUncovered) {
        ++year_.uncovered;
        judged_families_.insert(FamilyOf(world, event.resident));
      } else if (event.kind == core::EventKind::kThiefJudged) {
        const auto choice = static_cast<std::size_t>(event.amount);
        if (event.order.value == core::kInvalidEntityIdValue) {
          ++year_.lapsed;
        } else if (choice < year_.judged.size()) {
          ++year_.judged[choice];
        }
      }
    }
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    if (!on_) {
      return;
    }
    for (const std::string& line : lines_) {
      out << "population_curve: CATCH2191 seed " << seed << " arm " << arm_ << ' ' << line << '\n';
    }
  }

 private:
  struct Year {
    std::uint32_t patrols = 0;
    std::uint32_t uncovered = 0;
    std::uint32_t lapsed = 0;
    std::array<std::uint32_t, static_cast<std::size_t>(core::ThiefChoice::kThiefChoiceCount)>
        judged{};
  };

  core::ThiefChoice Choice() {
    switch (arm_) {
      case 'C':
        return core::ThiefChoice::kHandToConstable;
      case 'E':
        return core::ThiefChoice::kCloseEyes;
      case 'F':
        return core::ThiefChoice::kFindStill;
      default: {
        const auto choice = static_cast<core::ThiefChoice>(
            rotation_ % static_cast<std::uint32_t>(core::ThiefChoice::kThiefChoiceCount));
        ++rotation_;
        return choice;
      }
    }
  }

  static std::uint32_t FamilyOf(const core::WorldState& world, core::ResidentId resident) {
    const std::uint32_t row = core::FindRow(world.residents, resident);
    return row == core::kNoRow ? core::kInvalidEntityIdValue
                               : world.residents.rows[row].family.value;
  }

  void CloseYearIfTurned(const core::WorldState& world) {
    const std::uint32_t day = world.calendar.day;
    if (day == 0 || day % core::kDaysPerYear != 0 || day == closed_day_) {
      return;
    }
    closed_day_ = day;
    double attitude = 0.0;
    std::uint32_t people = 0;
    for (const core::ResidentRow& person : world.residents.rows) {
      if (judged_families_.contains(person.family.value)) {
        attitude += static_cast<double>(person.attitude_to_chairman);
        ++people;
      }
    }
    std::ostringstream line;
    line << "year " << (day / core::kDaysPerYear)
         << " ground=bot_patrols_most_robbed_store;family_attitude_mean_of_caught_thieves"
         << " patrols " << year_.patrols << " uncovered " << year_.uncovered << " lapsed "
         << year_.lapsed << " judged";
    for (const std::uint32_t count : year_.judged) {
      line << ' ' << count;
    }
    line << " detective " << world.chairman.detective << " discipline " << world.chairman.discipline
         << " authority " << world.chairman.authority << " district "
         << world.chairman.raikom_reputation << " thief_family_attitude "
         << (people > 0 ? attitude / people : -1.0) << " (" << people << " people)";
    lines_.push_back(line.str());
    year_ = Year{};
  }

  char arm_ = 0;
  bool on_ = false;
  std::uint32_t rotation_ = 0;
  core::SimDay patrolled_night_ = 0xFFFFFFFFU;
  core::SimDay closed_day_ = 0;
  std::set<std::uint32_t> judged_families_;
  Year year_;
  std::vector<std::string> lines_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_CATCH_POLICY_H_
