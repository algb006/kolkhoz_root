/// @file
/// @brief S2072 (econ's side gates of the winter horse, econ-boss-2072-trails-gates-2026-11-23
///        [16]; boss 25 Nov): per village-year, the rows the HORSE1573 and PLANY prints do not
///        carry -
///          S1  the deferrable own-cart loads' waiting task-days in Nov-Feb (a district lot, a
///              store emptied, a dig, the manure's dose, a field's heap, a works' supply) and
///              those still open on the first March day;
///          S3' the milk spoiled (the closed book's spoiled, kg);
///          S5  the old households' hungry family-days (every member at or past the food
///              table's plot_elder_from_bio_years; the family's hunger alarm lit);
///          S6  the site-days a works waited for logs (logs awaiting delivery to it) and the
///              logs built in, by month, November to February.
///        S4 (the horses' condition and deaths) and S3''s feed-short days are HORSE1573's.
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.

#ifndef TESTS_RUN_COMMON_ECON_SIDE_TALLY_H_
#define TESTS_RUN_COMMON_ECON_SIDE_TALLY_H_

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <ostream>
#include <string>

#include "core_catalog/timber_catalog.h"
#include "core_common/calendar.h"
#include "core_common/logistics_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

class EconSideTally {
 public:
  explicit EconSideTally(const core::ITableSet& tables) {
    std::string error;
    core::TimberCatalog timber;
    if (core::ParseTimberCatalog(tables, timber, error)) {
      log_ = timber.log_resource;
      log_grams_ = timber.log_grams;
    }
    const auto real = [&tables](const char* table, const char* key) {
      std::optional<float> value;
      if (const core::ITable* const found = tables.FindTable(table)) {
        value = found->CellReal(found->FindRowByKey(key), found->FindColumn("value"));
      }
      return value;
    };
    elder_years_ = real("food", "plot_elder_from_bio_years").value_or(-1.0F);
    speedup_ = real("life", "life_speedup").value_or(0.0F);
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      milk_ = resources->FindRowByKey("milk");
    }
  }

  /// @brief Once a day, after the day's step.
  void CountDay(const core::WorldState& world) {
    const std::uint32_t day_of_year = world.calendar.day % core::kDaysPerYear;
    const std::uint32_t year = (world.calendar.day / core::kDaysPerYear) + 1U;
    const std::uint32_t month = day_of_year / core::kDaysPerMonth;
    const bool winter = month >= 10U || month <= 1U;
    Year& row = by_year_[year];
    // S3': the closed book's milk spoiled, on the year's first day (the year gone).
    if (day_of_year == 0 && world.calendar.day > 0 && milk_ < world.ledger.closed.spoiled.size()) {
      by_year_[year - 1U].milk_spoiled_kg =
          static_cast<double>(world.ledger.closed.spoiled[milk_]) / 1000.0;
    }
    // S1: the deferrable loads open today.
    std::uint32_t deferrable = 0;
    for (const core::LogisticsTaskRow& task : world.logistics_tasks.rows) {
      deferrable += Deferrable(task.load_kind) ? 1U : 0U;
    }
    if (winter) {
      row.deferrable_task_days += deferrable;
    }
    constexpr std::uint32_t kMarch = 2;
    if (month == kMarch && day_of_year % core::kDaysPerMonth == 0) {
      row.deferrable_open_1_march = deferrable;
    }
    // S5: old households hungry today.
    if (elder_years_ > 0.0F && speedup_ > 0.0F) {
      for (std::uint32_t family = 0; family < world.families.rows.size(); ++family) {
        if (world.families.rows[family].hunger_alarm_lit == 0) {
          continue;
        }
        const core::FamilyId id = world.families.row_ids[family];
        bool any = false;
        bool all_old = true;
        for (const core::ResidentRow& person : world.residents.rows) {
          if (person.family.value != id.value) {
            continue;
          }
          any = true;
          all_old = all_old && core::BiologicalAgeYears(
                                   speedup_, person.birth_day, world.calendar.day) >= elder_years_;
        }
        row.old_hungry_family_days += any && all_old ? 1U : 0U;
      }
    }
    // S6: sites waiting for logs, and the logs built in by winter month.
    if (log_grams_ > 0 && log_.value != core::kInvalidDefIdValue) {
      for (const core::UnitRow& unit : world.units.rows) {
        const core::ResourceAmounts& awaiting = unit.construction.awaiting;
        if (log_.value < awaiting.size() && awaiting[log_.value] > 0) {
          ++row.site_days_waiting_logs;
        }
      }
      const core::ResourceAmounts& built = world.ledger.current.built_in;
      const core::Grams now = log_.value < built.size() ? built[log_.value] : 0;
      const core::Grams before = day_of_year == 0 ? 0 : last_built_;
      last_built_ = now;
      if (winter) {
        const std::size_t slot = month >= 10U ? month - 10U : month + 2U;  // Nov, Dec, Jan, Feb
        row.winter_logs_built[slot] +=
            static_cast<double>(now > before ? now - before : 0) / static_cast<double>(log_grams_);
      }
    }
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    for (const auto& [year, row] : by_year_) {
      out << "population_curve: S2072 seed " << seed << " year " << year
          << " ground=econ_side_gates;deferrable=district_lot,store_emptied,dig,manure_dose,"
             "field_heap,site_supply;old=every_member_at_plot_elder_age"
          << " S1(deferrable_task_days_nov_feb/open_1_march) " << row.deferrable_task_days << '/'
          << row.deferrable_open_1_march << " S3_milk_spoiled_kg " << row.milk_spoiled_kg
          << " S5_old_hungry_family_days " << row.old_hungry_family_days
          << " S6(site_days_waiting_logs) " << row.site_days_waiting_logs
          << " winter_logs_built(nov/dec/jan/feb) " << row.winter_logs_built[0] << '/'
          << row.winter_logs_built[1] << '/' << row.winter_logs_built[2] << '/'
          << row.winter_logs_built[3]
          << (elder_years_ > 0.0F ? "" : " S5_NOT_MEASURED(elder age not read)") << '\n';
    }
  }

 private:
  struct Year {
    std::uint32_t deferrable_task_days = 0;
    std::uint32_t deferrable_open_1_march = 0;
    double milk_spoiled_kg = 0.0;
    std::uint32_t old_hungry_family_days = 0;
    std::uint32_t site_days_waiting_logs = 0;
    std::array<double, 4> winter_logs_built{};
  };

  static bool Deferrable(core::LogisticsLoadKind kind) {
    return kind == core::LogisticsLoadKind::kDistrictLot ||
           kind == core::LogisticsLoadKind::kStoreTransfer ||
           kind == core::LogisticsLoadKind::kSiteDig ||
           kind == core::LogisticsLoadKind::kManureField ||
           kind == core::LogisticsLoadKind::kFieldHeap ||
           kind == core::LogisticsLoadKind::kSiteSupply;
  }

  core::ResourceId log_;
  core::Grams log_grams_ = 0;
  float elder_years_ = -1.0F;
  float speedup_ = 0.0F;
  std::uint32_t milk_ = core::kNoTableRow;
  core::Grams last_built_ = 0;
  std::map<std::uint32_t, Year> by_year_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_ECON_SIDE_TALLY_H_
