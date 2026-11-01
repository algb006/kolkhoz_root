/// @file
/// @brief The outdoor workers' rest in June-August, years 1-3, by host's
/// definition, and the summer's hot days and warm days off - #1373's gates.
/// @threading SINGLE_THREADED
/// Test-side code, read from the thread that owns the simulation.
///
/// WHY IT EXISTS (#1373; econ, econ-boss-1373-rest-gates [1]): P1-P3 read the
/// share of outdoor person-days below 40 in June-August, and the walk-offs.
/// host's print (host-boss-rest-print-1229 [1]) measured it on its own tree;
/// this is the same definition in the canon run, so the arms base / A / B are
/// read in one world:
///   OUTDOOR - an adult (16+) whose MORNING order is outdoor work
///   (core::IsOutdoorWork, host's list plus the road work) - read at noon;
///   HIS REST - read at hour 22, his day's low (a walk-off is read after it).
/// The mechanism's two weather counts ride along: the summer's hot and
/// sultry afternoons (the heat price must have days to bite, or P1 reads
/// NOT MEASURED) and its warm days off (the bathing Sunday's: >= 1 a summer).

#ifndef TESTS_RUN_COMMON_REST_TALLY_H_
#define TESTS_RUN_COMMON_REST_TALLY_H_

#include <array>
#include <cstdint>
#include <unordered_set>
#include <utility>

#include "core_common/calendar.h"
#include "core_common/labor_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

class RestTally {
 public:
  /// The years read (econ: y1-y3).
  static constexpr std::uint32_t kYears = 3;
  /// June-August, 0-based months.
  static constexpr std::uint32_t kSummerFrom = 5;
  static constexpr std::uint32_t kSummerTo = 7;
  /// The adult age of host's definition.
  static constexpr float kAdultYears = 16.0F;
  /// The hour the rest is read (host's: his day's low).
  static constexpr std::uint32_t kEveningHour = 22;

  explicit RestTally(const core::ITableSet& tables) {
    if (const core::ITable* const life = tables.FindTable("life")) {
      const std::uint32_t value = life->FindColumn("value");
      const std::uint32_t row = life->FindRowByKey("life_speedup");
      if (row != core::kNoTableRow) {
        life_speedup_ = static_cast<float>(life->CellReal(row, value).value_or(1.0F));
      }
    }
    if (const core::ITable* const labor = tables.FindTable("labor")) {
      const std::uint32_t value = labor->FindColumn("value");
      for (const auto& [key, out] : {std::pair{"bathing_teen_from_age", &teen_from_},
                                     std::pair{"bathing_teen_to_age", &teen_to_}}) {
        const std::uint32_t row = labor->FindRowByKey(key);
        if (row != core::kNoTableRow) {
          *out = static_cast<float>(labor->CellReal(row, value).value_or(*out));
        }
      }
    }
    if (const core::ITable* const weather = tables.FindTable("weather_params")) {
      const std::uint32_t value = weather->FindColumn("value");
      const auto number = [weather, value](const char* key, float fallback) {
        const std::uint32_t row = weather->FindRowByKey(key);
        return row == core::kNoTableRow
                   ? fallback
                   : static_cast<float>(weather->CellReal(row, value).value_or(fallback));
      };
      hot_c_ = number("hot_afternoon_c", hot_c_);
      sultry_c_ = number("sultry_afternoon_c", sultry_c_);
      warm_c_ = number("warm_water_afternoon_c", warm_c_);
    }
  }

  /// @brief The morning's outdoor adults and the day's weather. At noon.
  void CountNoon(const core::WorldState& world) {
    outdoor_.clear();
    if (!InSummer(world)) {
      return;
    }
    for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
      const core::ResidentRow& resident = world.residents.rows[row];
      if (core::IsOutdoorWork(resident.work.kind) &&
          core::BiologicalAgeYears(life_speedup_, resident.birth_day, world.calendar.day) >=
              kAdultYears) {
        outdoor_.insert(world.residents.row_ids[row].value);
      }
    }
    const float afternoon =
        world.weather.air_temperature_celsius + world.weather.temperature_swing_celsius;
    hot_days_ += afternoon >= hot_c_ ? 1U : 0U;
    sultry_days_ += afternoon >= sultry_c_ ? 1U : 0U;
    const bool rest_day =
        core::IsRestDay(world.calendar.day, world.calendar.day_zero_weekday, world.epoch);
    rest_days_ += rest_day ? 1U : 0U;
    warm_rest_days_ += rest_day && afternoon >= warm_c_ ? 1U : 0U;
  }

  /// @brief The noon's outdoor adults' rest. At kEveningHour.
  void CountEvening(const core::WorldState& world) {
    if (!InSummer(world)) {
      return;
    }
    // THE TEENS (econ [4]: the beach's measurable effect in the reference
    // village is theirs), all of them, at work or not.
    for (const core::ResidentRow& resident : world.residents.rows) {
      const float age =
          core::BiologicalAgeYears(life_speedup_, resident.birth_day, world.calendar.day);
      if (age >= teen_from_ && age < teen_to_ + 1.0F) {
        ++teen_days_;
        teen_below40_ += resident.rest < 40.0F ? 1U : 0U;
      }
    }
    for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
      if (outdoor_.count(world.residents.row_ids[row].value) == 0) {
        continue;
      }
      const float rest = world.residents.rows[row].rest;
      ++person_days_;
      below40_ += rest < 40.0F ? 1U : 0U;
      below20_ += rest < 20.0F ? 1U : 0U;
    }
    outdoor_.clear();
  }

  std::uint32_t PersonDays() const { return person_days_; }

  std::uint32_t Below40() const { return below40_; }

  std::uint32_t Below20() const { return below20_; }

  std::uint32_t HotDays() const { return hot_days_; }

  std::uint32_t SultryDays() const { return sultry_days_; }

  std::uint32_t RestDays() const { return rest_days_; }

  std::uint32_t WarmRestDays() const { return warm_rest_days_; }

  std::uint32_t TeenDays() const { return teen_days_; }

  std::uint32_t TeenBelow40() const { return teen_below40_; }

 private:
  static bool InSummer(const core::WorldState& world) {
    const auto month = static_cast<std::uint32_t>(world.calendar.date.month);
    // CalendarDate::year is ONE-based (calendar.h, DateFromDay): until host's tie
    // on 636eba31 (host-boss-1404 [1]) this read `< kYears` and counted y1-y2.
    return world.calendar.date.year <= kYears && month >= kSummerFrom && month <= kSummerTo;
  }

  float life_speedup_ = 1.0F;
  float hot_c_ = 25.0F;
  float sultry_c_ = 28.0F;
  float warm_c_ = 20.0F;
  float teen_from_ = 12.0F;
  float teen_to_ = 17.0F;
  std::uint32_t teen_days_ = 0, teen_below40_ = 0;
  std::unordered_set<std::uint32_t> outdoor_;
  std::uint32_t person_days_ = 0, below40_ = 0, below20_ = 0;
  std::uint32_t hot_days_ = 0, sultry_days_ = 0, rest_days_ = 0, warm_rest_days_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_REST_TALLY_H_
