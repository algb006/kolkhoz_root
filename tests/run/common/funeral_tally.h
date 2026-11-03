/// @file
/// @brief The funerals of a canon village, y1-y6 - #1084's pair print
/// (econ's gates F1, F6, P1, S2 and boss's graves count, core-boss-1084-
/// funeral-body [2]).
/// @threading SINGLE_THREADED
/// Test-side code, read from the thread that owns the simulation.
///
/// WHAT IT COUNTS: every funeral the day it is opened (a new row in
/// world.funerals, by id): its dead, its party split into kin and neighbours,
/// its road and whether it is loud, by the game year it opened in; the closed
/// books' kFuneral idle person-days y1-y3 (the kin's working days lost); the
/// graves the cemeteries hold at the end of y6. The canon's chairman never
/// presses «Пойти», so this is econ's «goes to none» arm.

#ifndef TESTS_RUN_COMMON_FUNERAL_TALLY_H_
#define TESTS_RUN_COMMON_FUNERAL_TALLY_H_

#include <algorithm>
#include <array>
#include <cstdint>
#include <ostream>
#include <vector>

#include "core_common/labor_state.h"
#include "core_common/ledger_state.h"
#include "core_common/world_state.h"

namespace run {

class FuneralTally {
 public:
  static constexpr std::uint32_t kYears = 6;

  /// @brief Every funeral opened since the last call, by its id.
  void CountDayEnd(const core::WorldState& world) {
    const std::uint32_t year = world.calendar.date.year;
    for (std::uint32_t row = 0; row < world.funerals.rows.size(); ++row) {
      const std::uint32_t id = world.funerals.row_ids[row].value;
      if (id <= last_id_) {
        continue;
      }
      last_id_ = std::max(last_id_, id);
      if (year < 1 || year > kYears) {
        continue;
      }
      const core::FuneralRow& funeral = world.funerals.rows[row];
      ++funerals_;
      dead_ += static_cast<std::uint32_t>(funeral.deceased.size());
      kin_.push_back(funeral.kin_count);
      neighbours_.push_back(static_cast<std::uint32_t>(funeral.party.size()) - funeral.kin_count);
      road_km_ += static_cast<double>(funeral.road_km);
      loud_by_year_[year - 1] += funeral.loud;
      if (funeral.cemetery.value == core::kInvalidEntityIdValue) {
        ++no_cemetery_;
      }
    }
    if (year == kYears) {
      graves_ = 0;
      for (const core::UnitRow& unit : world.units.rows) {
        graves_ += unit.graves;
      }
    }
  }

  /// @brief The kin's working days off (kFuneral), y1-y3.
  void CloseYear(const core::YearLedger& closed, std::uint32_t year) {
    if (year >= 1 && year <= 3) {
      idle_y1y3_ += closed.idle_person_days[static_cast<std::size_t>(core::IdleReason::kFuneral)];
    }
    // The book's deaths, both arms alike: F2 reads them, not the funerals'.
    if (year >= 1 && year <= kYears) {
      book_deaths_ += closed.deaths;
    }
  }

  /// @brief The village's line.
  void Print(std::ostream& out, std::uint64_t seed) const {
    const auto median = [](std::vector<std::uint32_t> values) -> std::uint32_t {
      if (values.empty()) {
        return 0;
      }
      std::ranges::sort(values);
      return values[values.size() / 2];
    };
    std::vector<std::uint32_t> party(kin_.size());
    for (std::size_t index = 0; index < kin_.size(); ++index) {
      party[index] = kin_[index] + neighbours_[index];
    }
    std::uint32_t loud = 0;
    std::uint32_t loud_max = 0;
    for (const std::uint32_t count : loud_by_year_) {
      loud += count;
      loud_max = std::max(loud_max, count);
    }
    out << "population_curve: F1084 seed " << seed << " funerals_y1y6 " << funerals_
        << " dead_y1y6 " << dead_ << " book_deaths_y1y6 " << book_deaths_ << " party_median "
        << median(party) << " kin_median " << median(kin_) << " neighbours_median "
        << median(neighbours_) << " party_max "
        << (party.empty() ? 0U : *std::ranges::max_element(party)) << " road_km_mean "
        << (funerals_ > 0 ? road_km_ / funerals_ : 0.0) << " loud_y1y6 " << loud
        << " loud_max_year " << loud_max << " no_cemetery " << no_cemetery_
        << " idle_funeral_pd_y1y3 " << idle_y1y3_ << " graves_end_y6 " << graves_;
    if (funerals_ == 0) {
      out << " — NO FUNERAL OPENED y1-y6: the step is off or no one died (NOT MEASURED)";
    }
    out << '\n';
  }

 private:
  std::uint32_t last_id_ = 0;
  std::uint32_t funerals_ = 0;
  std::uint32_t dead_ = 0;
  std::vector<std::uint32_t> kin_;
  std::vector<std::uint32_t> neighbours_;
  double road_km_ = 0.0;
  std::array<std::uint32_t, kYears> loud_by_year_ = {};
  std::uint32_t no_cemetery_ = 0;
  std::uint32_t idle_y1y3_ = 0;
  std::uint32_t book_deaths_ = 0;
  std::uint32_t graves_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_FUNERAL_TALLY_H_
