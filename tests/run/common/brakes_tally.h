/// @file
/// @brief What the building sites stood on, day by day, and how many hands
/// stood about beside them: the instrument for "what holds the early years".
/// @threading SINGLE_THREADED
/// Test-side instrument, read from the thread that owns the simulation.
///
/// WHY (boss, parcel 312). The felling reach was found by one number — 3188
/// m3 felled on every seed and arm. The early years want the same kind of
/// number: for every day a site stood, ON WHAT — boards, logs not yet felled,
/// logs felled and lying in the grove, clay or another material, the queue
/// (the recipe is in hand and nobody started it), no crew on a started site —
/// and how many employable people had no work that day.

#ifndef TESTS_RUN_COMMON_BRAKES_TALLY_H_
#define TESTS_RUN_COMMON_BRAKES_TALLY_H_

#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "core_common/labor_state.h"
#include "core_common/timber_state.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace run {

class BrakesTally {
 public:
  explicit BrakesTally(const core::ITableSet& tables) {
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      board_ = resources->FindRowByKey("board");
      log_ = resources->FindRowByKey("log");
    }
  }

  /// @brief One day's reading, at the day's middle. Call once a day.
  void CountDay(core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    Year& year = years_.empty() ? years_.emplace_back() : years_.back();
    const core::WorkforceCount force = simulation.Workforce();
    year.idle_sum += force.idle;
    year.employable_sum += force.employable;
    ++year.days;
    core::Grams logs_lying = 0;
    for (const core::TimberStandRow& stand : world.stands.rows) {
      logs_lying += stand.load_grams;
    }
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      const core::ConstructionPhase phase = unit.construction.phase;
      const bool new_site = unit.level == 0 && phase != core::ConstructionPhase::kDemolishing &&
                            phase != core::ConstructionPhase::kNone;
      if (!new_site) {
        continue;
      }
      ++year.site_days[static_cast<std::size_t>(Cause(simulation, world, row, logs_lying))];
    }
  }

  /// @brief Opens the next year's row. Call at each year's end.
  void CloseYear() { years_.emplace_back(); }

  void Report(std::string_view run_name, std::uint32_t last_year) const {
    for (std::uint32_t index = 0; index < years_.size() && index < last_year; ++index) {
      const Year& year = years_[index];
      if (year.days == 0) {
        continue;
      }
      std::cout << run_name << ": BRAKES year " << index + 1 << " — site-days";
      for (std::size_t cause = 0; cause < kCauseCount; ++cause) {
        std::cout << " " << kCauseNames[cause] << " " << year.site_days[cause];
      }
      std::cout << "; idle hands " << year.idle_sum / year.days << " of "
                << year.employable_sum / year.days << " employable a day\n";
    }
  }

 private:
  enum class SiteCause : std::uint8_t {
    kBoards,
    kLogsToFell,
    kLogsToHaul,
    kOtherMaterial,
    kQueue,
    kNoCrew,
    kBuilding,
    kPaused,
  };
  static constexpr std::size_t kCauseCount = 8;
  static constexpr std::array<const char*, kCauseCount> kCauseNames = {"boards",
                                                                       "logs-to-fell",
                                                                       "logs-to-haul",
                                                                       "other-material",
                                                                       "queue",
                                                                       "no-crew",
                                                                       "building",
                                                                       "paused"};

  struct Year {
    std::array<std::uint32_t, kCauseCount> site_days{};
    std::uint64_t idle_sum = 0;
    std::uint64_t employable_sum = 0;
    std::uint32_t days = 0;
  };

  SiteCause Cause(core::ISimulation& simulation,
                  const core::WorldState& world,
                  std::uint32_t row,
                  core::Grams logs_lying) const {
    const core::UnitRow& unit = world.units.rows[row];
    const core::UnitId id = world.units.row_ids[row];
    if (unit.paused != 0) {
      return SiteCause::kPaused;
    }
    if (unit.construction.phase == core::ConstructionPhase::kBuilding) {
      for (const core::ResidentRow& resident : world.residents.rows) {
        if (resident.work.kind == core::WorkKind::kConstruction &&
            resident.work.unit.value == id.value) {
          return SiteCause::kBuilding;
        }
      }
      return SiteCause::kNoCrew;
    }
    // Marked (or delivering): the first line short names the wait, boards
    // before logs because the saw is the longer chain.
    const std::vector<core::MaterialShortfall> lines = simulation.MaterialsShortFor(id);
    if (lines.empty()) {
      return SiteCause::kQueue;
    }
    for (const core::MaterialShortfall& line : lines) {
      if (line.resource.value == board_) {
        return SiteCause::kBoards;
      }
    }
    for (const core::MaterialShortfall& line : lines) {
      if (line.resource.value == log_) {
        return line.needed - line.held <= logs_lying ? SiteCause::kLogsToHaul
                                                     : SiteCause::kLogsToFell;
      }
    }
    return SiteCause::kOtherMaterial;
  }

  std::uint32_t board_ = core::kNoTableRow;

  std::uint32_t log_ = core::kNoTableRow;

  std::vector<Year> years_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_BRAKES_TALLY_H_
