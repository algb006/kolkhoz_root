/// @file
/// @brief STILL2189 (#2189's probe, not for release): where the stills' supply comes from in
///        years 1-3 - litres brewed and the share out of the thieves' raw, still-months
///        supplied and holding, the sugar carried, and every night sack by its store's type.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation. Compiles on a tree with
/// and without ResidentRow::still_raw_* (the base): there the own raw reads 0.
#ifndef TESTS_RUN_COMMON_STILL_SUPPLY_PROBE_H_
#define TESTS_RUN_COMMON_STILL_SUPPLY_PROBE_H_

#include <array>
#include <cstdint>
#include <map>
#include <ostream>
#include <string>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/event_state.h"
#include "core_common/resident_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

namespace still_probe {

template <typename Row>
std::uint64_t RawGrainOf(const Row& row) {
  if constexpr (requires { row.still_raw_grain_g; }) {
    return row.still_raw_grain_g;
  } else {
    return 0;
  }
}

template <typename Row>
std::uint64_t RawPotatoOf(const Row& row) {
  if constexpr (requires { row.still_raw_potato_g; }) {
    return row.still_raw_potato_g;
  } else {
    return 0;
  }
}

}  // namespace still_probe

class StillSupplyProbe {
 public:
  explicit StillSupplyProbe(const core::ITableSet& tables) {
    if (const core::ITable* const types = tables.FindTable("unit_types")) {
      const std::uint32_t key = types->FindColumn("key");
      for (std::uint32_t row = 0; row < types->RowCount(); ++row) {
        type_keys_.emplace_back(key != core::kNoTableColumn ? types->CellText(row, key) : "?");
      }
    }
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      potato_ = resources->FindRowByKey("potato");
    }
  }

  /// @brief After every step: the night sacks (kFoodStolen, night) by store type, the sugar.
  void CountStep(const core::WorldState& world) {
    const std::uint32_t year = world.calendar.day / core::kDaysPerYear;
    if (year >= kYears) {
      return;
    }
    Year& at = years_[year];
    for (const core::SimEvent& event : world.step_events) {
      if (event.kind == core::EventKind::kFoodStolen && event.family.value == 0) {
        const std::uint32_t row = core::FindRow(world.units, event.unit);
        const std::uint32_t type =
            row != core::kNoRow ? world.units.rows[row].type.value : core::kNoTableRow;
        const std::string key = type < type_keys_.size() ? type_keys_[type] : "?";
        at.sacks_kg[key] += static_cast<double>(event.amount) / 1000.0;
        (event.resource.value == potato_ ? added_potato_ : added_grain_) +=
            static_cast<std::uint64_t>(event.amount);
      } else if (event.kind == core::EventKind::kSugarStolen) {
        at.sugar_kg += static_cast<double>(event.amount) / 1000.0;
      }
    }
  }

  /// @brief After each day's step: litres brewed, the raw used, the still-months.
  void CountDay(const core::WorldState& world) {
    const std::uint32_t day = world.calendar.day;
    const std::uint32_t year = day / core::kDaysPerYear;
    std::uint64_t grain = 0;
    std::uint64_t potato = 0;
    for (const core::ResidentRow& person : world.residents.rows) {
      grain += still_probe::RawGrainOf(person);
      potato += still_probe::RawPotatoOf(person);
    }
    const std::int64_t brewed = world.ledger.current.samogon_brewed_ml;
    if (year < kYears) {
      Year& at = years_[year];
      // Used = yesterday's + today's sacks - today's (a still's raw only falls by brewing).
      const auto used = [](std::uint64_t before, std::uint64_t added, std::uint64_t now) {
        return before + added > now ? before + added - now : 0;
      };
      at.own_grain_g += used(grain_, added_grain_, grain);
      at.own_potato_g += used(potato_raw_, added_potato_, potato);
      if (brewed >= brewed_) {
        at.brewed_ml += brewed - brewed_;
      } else {
        at.brewed_ml += brewed;  // the ledger's year turned
      }
      if (day % core::kDaysPerMonth == 0 && day > 0) {
        const core::Date closed = core::DateFromDay(day - 1U);
        const auto tag = static_cast<std::uint32_t>(
            (static_cast<std::uint32_t>(closed.year) * core::kMonthsPerYear) +
            static_cast<std::uint32_t>(closed.month) + 1U);
        for (const core::ResidentRow& person : world.residents.rows) {
          if (person.night_trade != core::NightTrade::kDistiller) {
            continue;
          }
          ++at.still_months;
          at.supplied_months += person.distiller_supplied_month == tag ? 1U : 0U;
          at.holding_months += person.samogon_ml >= kLitreMl ? 1U : 0U;
        }
      }
    }
    grain_ = grain;
    potato_raw_ = potato;
    added_grain_ = 0;
    added_potato_ = 0;
    brewed_ = brewed;
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    for (std::uint32_t year = 0; year < kYears; ++year) {
      const Year& at = years_[year];
      const double own_l = (static_cast<double>(at.own_grain_g) / kGrainGPerLitre) +
                           (static_cast<double>(at.own_potato_g) / kPotatoGPerLitre);
      out << "population_curve: STILL2189 seed " << seed << " year " << (year + 1U)
          << " ground=litres_from_ledger;own_raw_l_by_recipe(1.5_kg_grain,4_kg_potato);"
             "still_months_at_month_turns;sacks=kFoodStolen_night_by_store_type"
          << " brewed_l " << static_cast<double>(at.brewed_ml) / 1000.0 << " own_raw_l " << own_l
          << " sugar_carried_kg " << at.sugar_kg << " still_months " << at.still_months
          << " supplied_months " << at.supplied_months << " holding_months " << at.holding_months
          << " sacks_kg";
      if (at.sacks_kg.empty()) {
        out << " none";
      }
      for (const auto& [key, kg] : at.sacks_kg) {
        out << ' ' << key << '=' << kg;
      }
      out << '\n';
    }
  }

 private:
  static constexpr std::uint32_t kYears = 3;
  static constexpr std::uint32_t kLitreMl = 1000;
  static constexpr double kGrainGPerLitre = 1500.0;
  static constexpr double kPotatoGPerLitre = 4000.0;

  struct Year {
    std::int64_t brewed_ml = 0;
    std::uint64_t own_grain_g = 0;
    std::uint64_t own_potato_g = 0;
    double sugar_kg = 0.0;
    std::uint32_t still_months = 0;
    std::uint32_t supplied_months = 0;
    std::uint32_t holding_months = 0;
    std::map<std::string, double> sacks_kg;
  };

  std::array<Year, kYears> years_{};
  std::vector<std::string> type_keys_;
  std::uint32_t potato_ = core::kNoTableRow;
  std::uint64_t grain_ = 0;
  std::uint64_t potato_raw_ = 0;
  std::uint64_t added_grain_ = 0;
  std::uint64_t added_potato_ = 0;
  std::int64_t brewed_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_STILL_SUPPLY_PROBE_H_
