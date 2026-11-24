/// @file
/// @brief WHORSE2072 (econ's #2072 winter-horses page, ed67a62/371237a; boss 25 Nov: measure
///        only, no rule): three prints a winter horse for the fellers would stand on.
///        (1) PRECONDITION H, on every November-February day a felling mark stands: the
///        draught horses (DraughtHorsesOf), the horses the morning left (morning_horses_left,
///        what the felling's ride offer reads), the core's own free count (HorsesByUse kFree),
///        and the horses under the day's placements by what holds them - the fellers' ride, a
///        stand's logs (not a competitor), the milk leg and a rick's supply (NOT deferrable,
///        econ 25 Nov), every other own-cart load (deferrable: a district lot in or the plan
///        out, a field's heap or dose, a works' supply, a store emptied, a dig), and any other
///        horse work; the days with at least one horse free or on a deferrable load.
///        (2) THE MARK SLOT: the village's one live mark (timber_felling.cpp MarkFelling), by
///        stand - days held, days its felling was offered, its distance from the village's
///        middle - in Nov-Feb and Mar-Oct.
///        (3) THE LOGS BUILT IN, by the unit type that took them: the closed book's
///        built_in_by_type on each year's first day (the year gone), and the logs the shops
///        took in (processed).
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.
///
/// GROUND, printed: a horse is counted under a placement by its rider (WorkAssignment::
/// rides_horse), so a cart's passengers add none; a load is told by the assignment's own
/// address (the herd: the milk leg; a unit that RickSupplyStands: a rick's supply). An
/// unknown morning count (before the first plan) is left out and counted.

#ifndef TESTS_RUN_COMMON_WINTER_HORSE_TALLY_H_
#define TESTS_RUN_COMMON_WINTER_HORSE_TALLY_H_

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "core_catalog/timber_catalog.h"
#include "core_common/alarm_state.h"
#include "core_common/calendar.h"
#include "core_common/labor_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/timber_state.h"
#include "core_common/unit_state.h"
#include "core_common/work_seam.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "core_tables/tables.h"
#include "village_middle.h"

namespace run {

class WinterHorseTally {
 public:
  explicit WinterHorseTally(const core::ITableSet& tables) {
    std::string error;
    core::TimberCatalog catalog;
    if (core::ParseTimberCatalog(tables, catalog, error) && catalog.log_grams > 0) {
      log_ = catalog.log_resource;
      log_grams_ = catalog.log_grams;
    }
    if (const core::ITable* const types = tables.FindTable("unit_types")) {
      const std::uint32_t key = types->FindColumn("key");
      for (std::uint32_t row = 0; row < types->RowCount(); ++row) {
        type_keys_.emplace_back(types->CellText(row, key));
      }
    }
    if (const core::ITable* const kinds = tables.FindTable("livestock")) {
      const std::uint32_t row = kinds->FindRowByKey("horse");
      if (row != core::kNoTableRow) {
        horse_kind_ = core::LivestockKindId{static_cast<std::uint16_t>(row)};
      }
    }
  }

  /// @brief Once a day, after the day's step.
  void CountDay(const core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    const std::uint32_t day_of_year = world.calendar.day % core::kDaysPerYear;
    const std::uint32_t year = (world.calendar.day / core::kDaysPerYear) + 1U;
    if (day_of_year == 0 && world.calendar.day > 0) {
      ReadTheClosedBook(world, year - 1U);
    }
    const std::uint32_t month = day_of_year / core::kDaysPerMonth;
    const bool winter = month >= 10U || month <= 1U;
    Year& row = by_year_[year];
    // (2) the mark slot, every day.
    const core::Vec2 middle = VillageMiddle(world);
    bool marked_today = false;
    for (std::size_t at = 0; at < world.stands.rows.size(); ++at) {
      const core::TimberStandRow& stand = world.stands.rows[at];
      if (!(stand.marked_m3 > 0.0F && stand.work_days_remaining > 0.0F)) {
        continue;
      }
      marked_today = true;
      Slot& slot = row.slots[winter ? 0 : 1][at];
      ++slot.days;
      slot.offered += simulation.FellingCanBeManned(stand.position) ? 1U : 0U;
      slot.km = std::hypot(stand.position.x - middle.x, stand.position.y - middle.y) / 1000.0F;
    }
    // (1) H on the winter days a mark stands.
    if (!winter || !marked_today) {
      return;
    }
    Horses& horses = row.horses;
    ++horses.days;
    const std::uint32_t draught = core::DraughtHorsesOf(world, horse_kind_);
    horses.draught += draught;
    if (world.morning_horses_left == core::kMorningHorsesUnknown) {
      ++horses.left_unknown;
    } else {
      horses.left += world.morning_horses_left;
    }
    const std::array<std::uint16_t, core::kCartUseCount> uses = core::HorsesByUse(world, draught);
    const std::uint32_t core_free = uses[static_cast<std::size_t>(core::CartUse::kFree)];
    horses.core_free += core_free;
    std::array<std::uint32_t, kClasses> today{};
    for (const core::ResidentRow& person : world.residents.rows) {
      if (person.work.rides_horse == 0) {
        continue;
      }
      ++today[ClassOf(world, person.work)];
    }
    for (std::size_t at = 0; at < kClasses; ++at) {
      horses.by_class[at] += today[at];
    }
    const bool free_one =
        core_free > 0 ||
        (world.morning_horses_left != core::kMorningHorsesUnknown && world.morning_horses_left > 0);
    horses.days_free_or_deferrable += free_one || today[kDeferrable] > 0 ? 1U : 0U;
    horses.days_free += free_one ? 1U : 0U;
    horses.days_no_horse_at_all += draught == 0 ? 1U : 0U;
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    static constexpr std::array<const char*, kClasses> kNames = {
        "felling_ride", "stand_logs", "milk_leg", "rick_supply", "deferrable", "other"};
    for (const auto& [year, row] : by_year_) {
      const Horses& horses = row.horses;
      out << "population_curve: WHORSE2072 seed " << seed << " year " << year
          << " ground=winter_days_with_a_mark;rider_counts_the_horse;milk_leg+rick_supply_not_"
             "deferrable H(days/draught/left/left_unknown/core_free/days_free/days_free_or_"
             "deferrable/days_no_horse) "
          << horses.days << '/' << horses.draught << '/' << horses.left << '/'
          << horses.left_unknown << '/' << horses.core_free << '/' << horses.days_free << '/'
          << horses.days_free_or_deferrable << '/' << horses.days_no_horse_at_all
          << " horse_days_by_class";
      for (std::size_t at = 0; at < kClasses; ++at) {
        out << ' ' << kNames[at] << ' ' << horses.by_class[at];
      }
      for (std::size_t half = 0; half < 2; ++half) {
        out << (half == 0 ? " slot_nov_feb" : " slot_mar_oct") << "(stand:days/offered/km)";
        std::vector<std::pair<std::size_t, Slot>> slots(row.slots[half].begin(),
                                                        row.slots[half].end());
        std::sort(slots.begin(), slots.end(), [](const auto& left, const auto& right) {
          return left.second.days > right.second.days;
        });
        for (const auto& [stand, slot] : slots) {
          out << ' ' << stand << ':' << slot.days << '/' << slot.offered << '/' << slot.km;
        }
      }
      if (row.closed) {
        out << " built_in_logs(year_closed) total " << row.built_in_logs << " processed "
            << row.processed_logs << " by_type";
        for (const auto& [key, logs] : row.built_by_type) {
          out << ' ' << key << ':' << logs;
        }
        if (log_grams_ == 0) {
          out << " NOT_MEASURED(timber catalog not read)";
        }
      }
      out << '\n';
    }
  }

 private:
  static constexpr std::size_t kFellingRide = 0;
  static constexpr std::size_t kStandLogs = 1;
  static constexpr std::size_t kMilkLeg = 2;
  static constexpr std::size_t kRickSupply = 3;
  static constexpr std::size_t kDeferrable = 4;
  static constexpr std::size_t kOther = 5;
  static constexpr std::size_t kClasses = 6;

  struct Horses {
    std::uint32_t days = 0;
    std::uint32_t draught = 0;
    std::uint32_t left = 0;
    std::uint32_t left_unknown = 0;
    std::uint32_t core_free = 0;
    std::uint32_t days_free = 0;
    std::uint32_t days_free_or_deferrable = 0;
    std::uint32_t days_no_horse_at_all = 0;
    std::array<std::uint32_t, kClasses> by_class{};
  };

  struct Slot {
    std::uint32_t days = 0;
    std::uint32_t offered = 0;
    float km = 0.0F;
  };

  struct Year {
    Horses horses;
    std::array<std::map<std::size_t, Slot>, 2> slots;
    bool closed = false;
    double built_in_logs = 0.0;
    double processed_logs = 0.0;
    std::map<std::string, double> built_by_type;
  };

  static std::size_t ClassOf(const core::WorldState& world, const core::WorkAssignment& work) {
    if (work.kind == core::WorkKind::kFelling) {
      return kFellingRide;
    }
    if (work.kind != core::WorkKind::kHauling) {
      return kOther;
    }
    if (work.stand.value != core::kInvalidEntityIdValue) {
      return kStandLogs;
    }
    if (work.herd.value != core::kInvalidEntityIdValue) {
      return kMilkLeg;
    }
    if (work.unit.value != core::kInvalidEntityIdValue) {
      const std::uint32_t row = core::FindRow(world.units, work.unit);
      if (row != core::kNoRow && core::RickSupplyStands(world.units.rows[row])) {
        return kRickSupply;
      }
    }
    return kDeferrable;
  }

  void ReadTheClosedBook(const core::WorldState& world, std::uint32_t year) {
    if (log_grams_ == 0 || year == 0) {
      return;
    }
    Year& row = by_year_[year];
    row.closed = true;
    const auto logs = [this](core::Grams grams) {
      return static_cast<double>(grams) / static_cast<double>(log_grams_);
    };
    const core::YearLedger& closed = world.ledger.closed;
    row.built_in_logs =
        log_.value < closed.built_in.size() ? logs(closed.built_in[log_.value]) : 0.0;
    row.processed_logs =
        log_.value < closed.processed.size() ? logs(closed.processed[log_.value]) : 0.0;
    for (const core::BuiltInRow& built : closed.built_in_by_type) {
      if (built.resource != log_) {
        continue;
      }
      const std::string key = built.type.value < type_keys_.size()
                                  ? type_keys_[built.type.value]
                                  : "type" + std::to_string(built.type.value);
      row.built_by_type[key] += logs(built.grams);
    }
  }

  core::LivestockKindId horse_kind_;
  core::ResourceId log_;
  core::Grams log_grams_ = 0;
  std::vector<std::string> type_keys_;
  std::map<std::uint32_t, Year> by_year_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_WINTER_HORSE_TALLY_H_
