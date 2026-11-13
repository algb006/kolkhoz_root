/// @file
/// @brief #1687 (econ's #1686 audit of 378 against 376): prints that no run carried.
/// @threading SINGLE_THREADED
/// Test-side code, read once a day from the thread that owns the simulation.
///
///   * AUD1687 a village-year y1-y6: hunger family-days (the hunger lamp's red stage,
///     kFamilyGoingHungry, not its forecast) and the plan's failed positions (the closed
///     book's plan_delivered below plan_due, by resource);
///   * AUD1687H a village: wooden/brick/old houses and barracks standing (housing class,
///     built) at the close of y4, y8 and y16;
///   * AUD1687F a village-year, only for seeds in KOLKHOZ_FIELD_SEEDS (comma list): every
///     arable field and every field mown as it lies, with its area, this year's slot, the
///     crop standing, the last crop and whether it is mown as it lies - so «which field B
///     marked and which the base sowed with rye» reads off the rows.

#ifndef TESTS_RUN_COMMON_AUDIT_1687_TALLY_H_
#define TESTS_RUN_COMMON_AUDIT_1687_TALLY_H_

#include <cstdint>
#include <cstdlib>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/calendar.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "core_tables/tables.h"

namespace run {

class Audit1687Tally {
 public:
  Audit1687Tally(const core::ITableSet& tables, std::uint64_t seed) : seed_(seed) {
    if (const core::ITable* const types = tables.FindTable("unit_types")) {
      const std::uint32_t klass = types->FindColumn("class");
      for (std::uint32_t row = 0; row < types->RowCount(); ++row) {
        is_house_.push_back(klass != core::kNoTableColumn &&
                            types->CellText(row, klass) == std::string_view("housing"));
      }
    }
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      for (std::uint32_t row = 0; row < resources->RowCount(); ++row) {
        resource_keys_.emplace_back(resources->CellText(row, 0));
      }
    }
    if (const core::ITable* const crops = tables.FindTable("crops")) {
      for (std::uint32_t row = 0; row < crops->RowCount(); ++row) {
        crop_keys_.emplace_back(crops->CellText(row, 0));
      }
    }
    if (const char* const seeds = std::getenv("KOLKHOZ_FIELD_SEEDS")) {
      std::stringstream list(seeds);
      std::string one;
      while (std::getline(list, one, ',')) {
        if (!one.empty() && std::to_string(seed) == one) {
          fields_ = true;
        }
      }
    }
  }

  void CountDayEnd(const core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    const core::SimDay day = world.calendar.day;
    const std::uint32_t year = (day / core::kDaysPerYear) + 1U;
    if (day % core::kDaysPerYear == 0 && year >= 2 && day != closed_day_) {
      closed_day_ = day;
      CloseYear(world, year - 1U);
    }
    if (year <= 6) {
      std::vector<core::Alarm> alarms;
      simulation.CollectAlarms(alarms);
      for (const core::Alarm& alarm : alarms) {
        hunger_days_ +=
            alarm.kind == core::AlarmKind::kFamilyGoingHungry && alarm.days_to_loss <= 0 ? 1U : 0U;
      }
    }
  }

  void Print(std::ostream& out) const {
    out << lines_.str();
    out << "population_curve: AUD1687H seed " << seed_ << " houses_y4 " << houses_[0]
        << " houses_y8 " << houses_[1] << " houses_y16 " << houses_[2] << '\n';
  }

 private:
  static core::Grams Of(const core::ResourceAmounts& amounts, std::size_t resource) {
    return resource < amounts.size() ? amounts[resource] : 0;
  }

  std::string CropKey(core::CropId crop) const {
    return crop.value < crop_keys_.size() ? crop_keys_[crop.value] : std::string("-");
  }

  void CloseYear(const core::WorldState& world, std::uint32_t year) {
    const core::YearLedger& book = world.ledger.closed;
    if (year <= 6) {
      lines_ << "population_curve: AUD1687 seed " << seed_ << " year " << year
             << " hunger_family_days " << hunger_days_ << " plan_failed";
      bool any = false;
      for (std::size_t r = 0; r < book.plan_due.size(); ++r) {
        const core::Grams due = Of(book.plan_due, r);
        const core::Grams given = Of(book.plan_delivered, r);
        if (due > 0 && given < due) {
          lines_ << ' ' << (r < resource_keys_.size() ? resource_keys_[r] : "?") << ':'
                 << given / 1000 << '/' << due / 1000;
          any = true;
        }
      }
      lines_ << (any ? "" : " -") << '\n';
      hunger_days_ = 0;
    }
    if (year == 4 || year == 8 || year == 16) {
      std::uint32_t houses = 0;
      for (const core::UnitRow& unit : world.units.rows) {
        houses += unit.level > 0 && unit.type.value < is_house_.size() && is_house_[unit.type.value]
                      ? 1U
                      : 0U;
      }
      houses_[year == 4 ? 0 : year == 8 ? 1 : 2] = houses;
    }
    if (fields_ && year <= 16) {
      for (std::uint32_t row = 0; row < world.fields.rows.size(); ++row) {
        const core::FieldRow& field = world.fields.rows[row];
        const bool mown = field.mown_fallow != 0;
        if (field.kind != core::LandKind::kArable && !mown) {
          continue;
        }
        lines_ << "population_curve: AUD1687F seed " << seed_ << " year " << year << " field "
               << world.fields.row_ids[row].value << " at " << static_cast<int>(field.center.x)
               << ',' << static_cast<int>(field.center.y) << " ha "
               << static_cast<int>(field.area_ga * 10.0F + 0.5F) / 10.0 << " slot0 "
               << CropKey(field.rotation_year0) << " crop " << CropKey(field.crop) << " last "
               << CropKey(field.last_crop) << " mown_as_it_lies " << (mown ? 1 : 0) << " overgrown "
               << static_cast<int>(field.overgrown) << '\n';
      }
    }
  }

  std::uint64_t seed_ = 0;
  std::vector<bool> is_house_;
  std::vector<std::string> resource_keys_;
  std::vector<std::string> crop_keys_;
  bool fields_ = false;
  std::uint32_t hunger_days_ = 0;
  std::uint32_t houses_[3] = {0, 0, 0};
  core::SimDay closed_day_ = 0;
  std::ostringstream lines_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_AUDIT_1687_TALLY_H_
