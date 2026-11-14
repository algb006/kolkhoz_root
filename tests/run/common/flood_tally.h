/// @file
/// @brief #1375's print: the spring flood a village-year, for econ's gates (econ-boss-1375-
///        flood-lead [1]: P0a the lamp lit on every day from 1 December to the flood where
///        floodplain heaps lie, P0b dark where nothing lies, P0c «today» only after 1 March with
///        the cover lying; P1 the loss).
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// THE PRINT, a village, one line a flood season (1 December to the flood):
/// «FLOOD1375 seed S | y Y dec1_heaps_t H dec1_lamp_t A lit_days N season_days D lead_days L
///  today_days T today_before_march B eve_heaps_t E lost_t X flood_day F ...».
///   - dec1_heaps_t: hay and straw in the floodplain meadows' heaps on 1 December (the fields;
///     the run cannot read the map's floodplain cells, so stacks on them are not in it);
///   - dec1_lamp_t: the lamp's own tonnes that day (heaps AND stacks), 0 if dark;
///   - lit_days / season_days: the days the lamp was lit, of the days from 1 December to the
///     flood day (P0a: equal where H > 0; P0b: 0 where H = 0 and A = 0);
///   - lead_days: the first lit day to the flood day;
///   - today_days: lit days with days_ahead 0; today_before_march: of them, before 1 March
///     (P0c: 0);
///   - eve_heaps_t: the same heaps the evening before the flood - H less it is what the winter
///     carted (and the rot took);
///   - lost_t: YearLedger::lost_to_flood on the flood day (heaps and stacks);
///   - flood_day: the day of the year it came.

#ifndef TESTS_RUN_COMMON_FLOOD_TALLY_H_
#define TESTS_RUN_COMMON_FLOOD_TALLY_H_

#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/calendar.h"
#include "core_common/land_state.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"

namespace run {

class FloodTally {
 public:
  /// @brief Once a day after the day's steps.
  void CountDayEnd(const core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    const core::SimDay of_year = world.calendar.day % core::kDaysPerYear;
    const core::Grams heaps = FloodplainHeaps(world);
    if (of_year == kDecemberFirst) {
      season_open_ = true;
      dec1_heaps_ = heaps;
      dec1_lamp_ = 0;
      lit_days_ = 0;
      season_days_ = 0;
      first_lit_day_ = 0;
      today_days_ = 0;
      today_before_march_ = 0;
      season_year_ = (world.calendar.day / core::kDaysPerYear) + 2U;  // the spring it ends in
    }
    if (world.flood.last_flood_year != seen_flood_year_) {
      seen_flood_year_ = world.flood.last_flood_year;
      if (season_open_) {
        CloseSeason(world, of_year);
      }
      return;
    }
    if (!season_open_) {
      yesterday_heaps_ = heaps;
      return;
    }
    ++season_days_;
    std::vector<core::Alarm> alarms;
    simulation.CollectAlarms(alarms);
    for (const core::Alarm& alarm : alarms) {
      if (alarm.kind != core::AlarmKind::kFloodplainFodderAhead) {
        continue;
      }
      ++lit_days_;
      if (first_lit_day_ == 0) {
        first_lit_day_ = world.calendar.day + 1U;  // 1-based, 0 = never
      }
      if (of_year == kDecemberFirst) {
        dec1_lamp_ = alarm.amount;
      }
      if (alarm.days_ahead == 0) {
        ++today_days_;
        today_before_march_ += of_year >= kDecemberFirst || of_year < kMarchFirst ? 1U : 0U;
      }
      break;
    }
    yesterday_heaps_ = heaps;
  }

  /// @brief The village's line.
  void Print(std::ostream& out, std::uint64_t seed) const {
    out << "population_curve: FLOOD1375 seed " << seed;
    const std::string text = lines_.str();
    out << (text.empty() ? std::string(" | none") : text) << '\n';
  }

 private:
  static constexpr core::SimDay kDecemberFirst = 11U * core::kDaysPerMonth;
  static constexpr core::SimDay kMarchFirst = 2U * core::kDaysPerMonth;

  static core::Grams FloodplainHeaps(const core::WorldState& world) {
    core::Grams grams = 0;
    for (const core::FieldRow& field : world.fields.rows) {
      grams += field.kind == core::LandKind::kFloodplainMeadow && field.reaped_grams > 0
                   ? field.reaped_grams
                   : 0;
    }
    return grams;
  }

  void CloseSeason(const core::WorldState& world, core::SimDay of_year) {
    core::Grams lost = 0;
    for (const core::Grams grams : world.ledger.current.lost_to_flood) {
      lost += grams;
    }
    const core::SimDay flood_day = world.calendar.day;
    lines_ << " | y " << season_year_ << " dec1_heaps_t " << Tonnes(dec1_heaps_) << " dec1_lamp_t "
           << Tonnes(dec1_lamp_) << " lit_days " << lit_days_ << " season_days " << season_days_
           << " lead_days " << (first_lit_day_ == 0 ? 0U : flood_day + 1U - first_lit_day_)
           << " today_days " << today_days_ << " today_before_march " << today_before_march_
           << " eve_heaps_t " << Tonnes(yesterday_heaps_) << " lost_t " << Tonnes(lost)
           << " flood_day " << of_year;
    season_open_ = false;
  }

  static double Tonnes(core::Grams grams) {
    return static_cast<double>(static_cast<std::int64_t>(grams / 1000)) / 1000.0;
  }

  bool season_open_ = false;
  std::uint16_t seen_flood_year_ = 0;
  std::uint32_t season_year_ = 0;
  core::Grams dec1_heaps_ = 0;
  core::Grams dec1_lamp_ = 0;
  core::Grams yesterday_heaps_ = 0;
  std::uint32_t lit_days_ = 0;
  std::uint32_t season_days_ = 0;
  core::SimDay first_lit_day_ = 0;
  std::uint32_t today_days_ = 0;
  std::uint32_t today_before_march_ = 0;
  std::ostringstream lines_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_FLOOD_TALLY_H_
