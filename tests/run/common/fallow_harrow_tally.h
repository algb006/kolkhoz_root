/// @file
/// @brief #1837's print (econ's gates F1, F2 and the STUB's histogram; boss 17 Nov): per village
///        over the run, every black fallow opened for its winter crop - re-harrowed (the old
///        rule) or straight to the sowing (the new) - with the game days since its bare harrow
///        ended, the second harrow's norm-days, and the winter rye sown (hectares, and how many
///        of those ha were sown inside the crop's window).
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation, once a day at its end.
///
/// THE LINE: «FALLOW1837 seed S reharrows R reharrow_days D skipped K gaps g0:g1:g2:g3:g4:g5_8:
/// g9_16:g17 rye_sown_ha A rye_in_window_ha W». A gap is today - the field's bare harrow end, the
/// day the black fallow's turn opened it; a field opened with no bare harrow on record (ploughed
/// fallow of an older year) is not in the gaps.

#ifndef TESTS_RUN_COMMON_FALLOW_HARROW_TALLY_H_
#define TESTS_RUN_COMMON_FALLOW_HARROW_TALLY_H_

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <ostream>
#include <string_view>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/land_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

class FallowHarrowTally {
 public:
  /// @brief Reads crops.csv's is_winter and sow_to_month (human 1..12) by crop row.
  explicit FallowHarrowTally(const core::ITableSet& tables) {
    if (const core::ITable* const crops = tables.FindTable("crops")) {
      const std::uint32_t winter_column = crops->FindColumn("is_winter");
      const std::uint32_t to_column = crops->FindColumn("sow_to_month");
      for (std::uint32_t row = 0; row < crops->RowCount(); ++row) {
        winter_.push_back(crops->CellText(row, winter_column) == std::string_view("1"));
        const std::optional<float> to = crops->CellReal(row, to_column);
        sow_to_.push_back(to && *to >= 1.0F ? static_cast<std::uint32_t>(*to) - 1U : 11U);
      }
    }
  }

  /// @brief The day's end: each field's change since yesterday.
  void CountDayEnd(const core::WorldState& world) {
    for (std::uint32_t row = 0; row < world.fields.rows.size(); ++row) {
      const core::FieldRow& field = world.fields.rows[row];
      const std::uint32_t id = world.fields.row_ids[row].value;
      Seen& seen = seen_[id];
      const bool bare_now =
          field.phase == core::FieldPhase::kGrowing && field.crop.value == core::kInvalidDefIdValue;
      if (bare_now && field.bare_harrowed_day != core::kNoFurrowDay) {
        seen.bare_day = field.bare_harrowed_day;
      }
      const bool winter_crop = field.crop.value < winter_.size() && winter_[field.crop.value];
      // Opened today: harrowing (the old rule, or the new one's «waits harrowed» at seam 0), the
      // sowing, or already sown within the day (the new rule straight to the drill).
      const bool opened =
          seen.bare && winter_crop &&
          (field.phase == core::FieldPhase::kHarrowing ||
           field.phase == core::FieldPhase::kSowing || field.phase == core::FieldPhase::kGrowing);
      if (opened) {
        const bool reharrow =
            field.phase == core::FieldPhase::kHarrowing && field.work_days_remaining > 0.0F;
        if (reharrow) {
          ++reharrows_;
          reharrow_days_ += field.work_days_remaining;
        } else {
          ++skipped_;
        }
        if (seen.bare_day != core::kNoFurrowDay && world.calendar.day >= seen.bare_day) {
          ++gaps_[GapBucket(world.calendar.day - seen.bare_day)];
        }
        seen.bare_day = core::kNoFurrowDay;
      }
      // THE RYE SOWN, the day a winter crop's field first stands growing on it: its area x the
      // sown share, and inside the crop's window (sown_day's month at most sow_to_month).
      const bool sown_now = field.phase == core::FieldPhase::kGrowing && winter_crop &&
                            field.sown_day != core::kNeverSownDay;
      if (sown_now && !seen.sown) {
        const double ha = static_cast<double>(field.area_ga) * field.sown_share;
        rye_sown_ha_ += ha;
        const auto month = static_cast<std::uint32_t>(core::DateFromDay(field.sown_day).month);
        if (field.crop.value < sow_to_.size() && month <= sow_to_[field.crop.value]) {
          rye_in_window_ha_ += ha;
        }
      }
      seen.sown = sown_now;
      seen.bare = bare_now;
    }
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    out << "population_curve: FALLOW1837 seed " << seed << " reharrows " << reharrows_
        << " reharrow_days " << reharrow_days_ << " skipped " << skipped_ << " gaps";
    for (std::size_t index = 0; index < gaps_.size(); ++index) {
      out << (index == 0 ? " " : ":") << gaps_[index];
    }
    out << " rye_sown_ha " << rye_sown_ha_ << " rye_in_window_ha " << rye_in_window_ha_ << '\n';
  }

 private:
  struct Seen {
    bool bare = false;
    bool sown = false;
    core::SimDay bare_day = core::kNoFurrowDay;
  };

  static std::size_t GapBucket(core::SimDay gap) {
    if (gap <= 4) {
      return gap;
    }
    return gap <= 8 ? 5 : (gap <= 16 ? 6 : 7);
  }

  std::vector<bool> winter_;
  std::vector<std::uint32_t> sow_to_;
  std::map<std::uint32_t, Seen> seen_;
  std::uint32_t reharrows_ = 0;
  double reharrow_days_ = 0.0;
  std::uint32_t skipped_ = 0;
  std::array<std::uint32_t, 8> gaps_{};
  double rye_sown_ha_ = 0.0;
  double rye_in_window_ha_ = 0.0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_FALLOW_HARROW_TALLY_H_
