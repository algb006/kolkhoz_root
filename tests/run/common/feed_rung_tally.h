#ifndef TESTS_RUN_COMMON_FEED_RUNG_TALLY_H_
#define TESTS_RUN_COMMON_FEED_RUNG_TALLY_H_

/// @file feed_rung_tally.h
/// @brief #1573's book line (econ's gates, hay-lamp-feed-before-cows-
///        2026-11-05.md §5): a village-year y1-kYears, off the closed book at
///        each turn - the district's points granted and spent (S1's income
///        and spend; S2 reads the PAIR's difference of the next grant against
///        the feed points, HayAnswer's F1573A line), the herds' hunger deaths
///        (P2) - and the y2 meadow line econ asked (§2, «NOT READ»): at the
///        day-end of the cut's last day, every meadow's area and its grass
///        still standing (the uncut share of a meadow in its harvest, a
///        meadow still growing unmown this year whole), against the hay the
///        year's book says was cut.
/// @threading SINGLE_THREADED - the run's own loop, after the day.

#include <array>
#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/land_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

class FeedRungTally {
 public:
  static constexpr std::uint32_t kYears = 6;
  static constexpr std::uint32_t kMeadowYear = 2;

  explicit FeedRungTally(const core::ITableSet& tables) {
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      hay_ = resources->FindRowByKey("hay");
    }
    if (const core::ITable* const farming = tables.FindTable("farming")) {
      const std::uint32_t row = farming->FindRowByKey("meadow_cut_month_end");
      const std::uint32_t col = farming->FindColumn("value");
      if (row != core::kNoTableRow && col != core::kNoTableColumn) {
        cut_end_month_ =
            static_cast<std::uint32_t>(std::stoi(std::string(farming->CellText(row, col))));
      }
    }
    if (const core::ITable* const kinds = tables.FindTable("meadow_kinds")) {
      const std::uint32_t col = kinds->FindColumn("yield_kg_per_ha");
      const auto rate = [&](std::string_view key, float& into) {
        const std::uint32_t row = kinds->FindRowByKey(key);
        if (row != core::kNoTableRow && col != core::kNoTableColumn) {
          into = std::stof(std::string(kinds->CellText(row, col)));
        }
      };
      rate("upland", upland_kg_ha_);
      rate("floodplain", floodplain_kg_ha_);
    }
  }

  /// @brief Once a day after the chairman. At a year's first day the closed
  ///        book is the year just ended; on y2's cut's last day the meadows.
  void CountDayEnd(const core::WorldState& world) {
    const std::uint32_t day_of_year = world.calendar.day % core::kDaysPerYear;
    const std::uint32_t year = (world.calendar.day / core::kDaysPerYear) + 1U;
    if (day_of_year == 0 && year >= 2 && year - 2 < kYears && world.calendar.day != last_turn_) {
      last_turn_ = world.calendar.day;
      const core::YearLedger& closed = world.ledger.closed;
      Year& book = years_[year - 2];
      book.closed = true;
      book.granted = closed.limit_points_granted;
      book.spent = closed.limit_points_spent;
      book.hunger_deaths = closed.herd_deaths_hunger;
      book.hay_cut_g = hay_ < closed.harvest.size() ? closed.harvest[hay_] : 0;
    }
    // The cut's last day: month `cut_end_month_` (1-based), its last day.
    const auto month_index = static_cast<std::uint32_t>(world.calendar.date.month);
    const bool last_day = world.calendar.date.day_in_month == core::kDaysPerMonth - 1U;
    if (year == kMeadowYear && cut_end_month_ != 0 && month_index + 1U == cut_end_month_ &&
        last_day && meadows_.empty()) {
      for (std::uint32_t row = 0; row < world.fields.rows.size(); ++row) {
        const core::FieldRow& field = world.fields.rows[row];
        if (field.kind != core::LandKind::kMeadow &&
            field.kind != core::LandKind::kFloodplainMeadow) {
          continue;
        }
        const float rate =
            field.kind == core::LandKind::kFloodplainMeadow ? floodplain_kg_ha_ : upland_kg_ha_;
        float left = 0.0F;
        if (field.phase == core::FieldPhase::kHarvest) {
          left = field.harvest_work_days > 0.0F
                     ? field.work_days_remaining / field.harvest_work_days
                     : 0.0F;
          left = left < 0.0F ? 0.0F : (left > 1.0F ? 1.0F : left);
        } else if (field.phase == core::FieldPhase::kGrowing &&
                   (field.last_mown_day == core::kNeverMownDay ||
                    field.last_mown_day / core::kDaysPerYear !=
                        world.calendar.day / core::kDaysPerYear)) {
          left = 1.0F;
        }
        meadows_.push_back(Meadow{.row = row,
                                  .ha = field.area_ga,
                                  .phase = static_cast<int>(field.phase),
                                  .left_t = rate * field.area_ga * left / 1000.0F});
      }
      meadows_read_ = true;
    }
  }

  /// @brief «F1573B seed S | y1 granted G spent P hunger_deaths D hay_cut_t T
  ///        | y2 ... MEADOW_Y2 read R grass_left_t L meadows M list row:ha:phase:left_t
  ///        ... MEADOW_END». A year not closed prints «-».
  void Print(std::ostream& out, std::uint64_t seed) const {
    out << "population_curve: F1573B seed " << seed;
    for (std::uint32_t year = 0; year < kYears; ++year) {
      const Year& book = years_[year];
      out << " | y" << year + 1;
      if (!book.closed) {
        out << " -";
        continue;
      }
      out << " granted " << book.granted << " spent " << book.spent << " hunger_deaths "
          << book.hunger_deaths << " hay_cut_t " << static_cast<double>(book.hay_cut_g) / 1e6;
    }
    double left = 0.0;
    for (const Meadow& meadow : meadows_) {
      left += static_cast<double>(meadow.left_t);
    }
    // NOT READ is said, not printed as nought (the run ended before y2's cut).
    out << " MEADOW_Y2 read " << (meadows_read_ ? 1 : 0) << " grass_left_t " << left << " meadows "
        << meadows_.size() << " list";
    for (const Meadow& meadow : meadows_) {
      out << ' ' << meadow.row << ':' << meadow.ha << ':' << meadow.phase << ':' << meadow.left_t;
    }
    out << " MEADOW_END\n";
  }

 private:
  struct Year {
    bool closed = false;
    std::int32_t granted = 0;
    std::int32_t spent = 0;
    std::uint32_t hunger_deaths = 0;
    core::Grams hay_cut_g = 0;
  };

  struct Meadow {
    std::uint32_t row = 0;
    float ha = 0.0F;
    int phase = 0;
    float left_t = 0.0F;
  };

  std::uint32_t hay_ = core::kNoTableRow;
  std::uint32_t cut_end_month_ = 0;
  float upland_kg_ha_ = 1500.0F;
  float floodplain_kg_ha_ = 2500.0F;
  core::SimDay last_turn_ = 0;
  std::array<Year, kYears> years_ = {};
  std::vector<Meadow> meadows_;
  bool meadows_read_ = false;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_FEED_RUNG_TALLY_H_
