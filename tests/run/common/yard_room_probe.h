/// @file
/// @brief YARD2311 (#2311, econ's check of production.md §1): the utility yard's OWN room by
///        day - mean fill share, days full (>= 99 %), first full day, the stock's share by
///        resource (resource-days in tonnes), and the first day a goods store stands; per seed,
///        per year. Read off the completed state once a day.
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.
#ifndef TESTS_RUN_COMMON_YARD_ROOM_PROBE_H_
#define TESTS_RUN_COMMON_YARD_ROOM_PROBE_H_

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "core_tables/tables.h"

namespace run {

class YardRoomProbe {
 public:
  explicit YardRoomProbe(const core::ITableSet& tables) {
    if (const core::ITable* const types = tables.FindTable("unit_types")) {
      yard_ = types->FindRowByKey("utility_yard");
      goods_store_ = types->FindRowByKey("goods_store");
    }
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      for (std::uint32_t row = 0; row < resources->RowCount(); ++row) {
        names_.emplace_back(resources->CellText(row, 0));
      }
      // econ's lot check: three goods with no church_store home.
      for (const char* const key : {"coal", "shoes_summer", "hide"}) {
        watched_.push_back(Watched{.name = key, .row = resources->FindRowByKey(key)});
      }
    }
    // econ's lot door: the two lots the 24 Sep STUB was for.
    if (const core::ITable* const lots = tables.FindTable("limit_catalog")) {
      for (std::size_t i = 0; i < kLots; ++i) {
        lot_rows_[i] = lots->FindRowByKey(kLotKeys[i]);
      }
    }
    if (const core::ITable* const levels = tables.FindTable("unit_levels")) {
      const std::uint32_t unit_col = levels->FindColumn("unit");
      const std::uint32_t level_col = levels->FindColumn("level");
      const std::uint32_t room_col = levels->FindColumn("storage_capacity_t");
      if (unit_col != core::kNoTableColumn && level_col != core::kNoTableColumn &&
          room_col != core::kNoTableColumn) {
        for (std::uint32_t row = 0; row < levels->RowCount(); ++row) {
          if (levels->CellText(row, unit_col) != "utility_yard") {
            continue;
          }
          const std::string room(levels->CellText(row, room_col));
          const auto level =
              static_cast<std::uint32_t>(std::stoul(std::string(levels->CellText(row, level_col))));
          if (!room.empty()) {
            if (tonnes_by_level_.size() <= level) {
              tonnes_by_level_.resize(level + 1U, 0.0);
            }
            tonnes_by_level_[level] = std::stod(room);
          }
        }
      }
    }
  }

  /// @brief Once a day after the step.
  void CountDayEnd(const core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    const std::uint32_t day = world.calendar.day;
    const std::uint32_t year = day / core::kDaysPerYear;
    if (years_.size() <= year) {
      years_.resize(year + 1U);
    }
    Year& line = years_[year];
    // THE LOT DOOR (econ): days the office's own answer for the lot is kNowhereToStore (an
    // order would be refused at the door), and the lots actually bought (new delivery rows).
    const core::LimitBook book = simulation.OfficeLimit();
    for (const core::LimitLotLine& lot_line : book.catalogue) {
      for (std::size_t i = 0; i < kLots; ++i) {
        if (lot_line.lot.value == lot_rows_[i]) {
          line.lot_door_refused_days[i] +=
              lot_line.orderable == core::OrderRefusal::kNowhereToStore ? 1U : 0U;
          line.lot_door_open_days[i] += lot_line.orderable == core::OrderRefusal::kNone ? 1U : 0U;
          // Any third answer (no price, gate, balance) is its own class, never silence.
          line.lot_door_other_days[i] +=
              lot_line.orderable != core::OrderRefusal::kNone &&
                      lot_line.orderable != core::OrderRefusal::kNowhereToStore
                  ? 1U
                  : 0U;
        }
      }
    }
    const core::LimitDeliveryTable& deliveries = world.limit_deliveries;
    for (std::size_t row = 0; row < deliveries.rows.size(); ++row) {
      if (deliveries.row_ids[row].value < seen_delivery_id_) {
        continue;
      }
      for (std::size_t i = 0; i < kLots; ++i) {
        line.lots_bought[i] += deliveries.rows[row].lot.value == lot_rows_[i] ? 1U : 0U;
      }
    }
    seen_delivery_id_ = deliveries.next_id_value;
    double held = 0.0;
    double room = 0.0;
    bool goods_store = false;
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.dead != 0 || unit.level == 0) {
        continue;
      }
      if (unit.type.value == goods_store_) {
        goods_store = true;
      }
      if (unit.type.value != yard_ || unit.level >= tonnes_by_level_.size()) {
        continue;
      }
      room += tonnes_by_level_[unit.level];
      for (std::size_t resource = 0; resource < unit.stock.size(); ++resource) {
        const double tonnes = static_cast<double>(unit.stock[resource]) / kGramsPerTonne;
        if (tonnes > 0.0) {
          held += tonnes;
          line.by_resource[resource] += tonnes;
        }
      }
    }
    if (goods_store && goods_store_day_ == kNever) {
      goods_store_day_ = day;
    }
    // The delivery's lost path (ledger lost_no_room, this year's book) for the watched goods:
    // days it grew, tonnes at the year's end, the first day of any loss.
    for (std::size_t i = 0; i < watched_.size() && i < kWatched; ++i) {
      Watched& good = watched_[i];
      const core::ResourceAmounts& lost = world.ledger.current.lost_no_room;
      const core::Grams now = good.row < lost.size() ? lost[good.row] : 0;
      if (now > good.last) {
        ++line.lost_days[i];
        if (good.first_day == kNever) {
          good.first_day = day;
        }
      }
      good.last = now;
      line.lost_t[i] = static_cast<double>(now) / kGramsPerTonne;
    }
    if (room <= 0.0) {
      return;
    }
    ++line.yard_days;
    line.fill_sum += std::min(1.0, held / room);
    if (held >= kFullShare * room) {
      ++line.full_days;
      if (first_full_day_ == kNever) {
        first_full_day_ = day;
      }
    }
  }

  /// @brief YARD2311 lines for this seed.
  void Print(std::ostream& out, std::uint64_t seed) const {
    out << "YARD2311 seed " << seed << " first_full_day " << DayText(first_full_day_)
        << " goods_store_day " << DayText(goods_store_day_);
    for (const Watched& good : watched_) {
      out << " first_lost_" << good.name << " "
          << (good.row == core::kNoTableRow ? std::string("no_resource") : DayText(good.first_day));
    }
    out << "\n";
    for (std::size_t year = 0; year < years_.size(); ++year) {
      out << "YARD2311 seed " << seed << " year " << (year + 1U) << " lots";
      for (std::size_t i = 0; i < kLots; ++i) {
        out << " " << kLotKeys[i]
            << (lot_rows_[i] == core::kNoTableRow ? std::string(" no_lot") : std::string())
            << " door_refused_days " << years_[year].lot_door_refused_days[i] << " door_open_days "
            << years_[year].lot_door_open_days[i] << " door_other_refusal_days "
            << years_[year].lot_door_other_days[i] << " bought " << years_[year].lots_bought[i];
      }
      out << "\n";
      out << "YARD2311 seed " << seed << " year " << (year + 1U) << " lost";
      for (std::size_t i = 0; i < watched_.size() && i < kWatched; ++i) {
        out << " " << watched_[i].name << " days " << years_[year].lost_days[i] << " t "
            << years_[year].lost_t[i];
      }
      out << "\n";
    }
    for (std::size_t year = 0; year < years_.size(); ++year) {
      const Year& line = years_[year];
      out << "YARD2311 seed " << seed << " year " << (year + 1U) << " yard_days " << line.yard_days
          << " fill_mean " << (line.yard_days == 0 ? 0.0 : line.fill_sum / line.yard_days)
          << " full_days " << line.full_days << " t_days";
      std::vector<std::pair<double, std::size_t>> ranked;
      for (const auto& [resource, tonnes] : line.by_resource) {
        ranked.emplace_back(tonnes, resource);
      }
      std::sort(ranked.rbegin(), ranked.rend());
      for (const auto& [tonnes, resource] : ranked) {
        out << " " << (resource < names_.size() ? names_[resource] : std::to_string(resource))
            << " " << static_cast<std::int64_t>(tonnes);
      }
      out << "\n";
    }
  }

 private:
  static constexpr std::uint32_t kNever = 0xFFFFFFFFU;
  static constexpr double kGramsPerTonne = 1.0e6;
  static constexpr double kFullShare = 0.99;

  static constexpr std::size_t kWatched = 3;
  static constexpr std::size_t kLots = 2;
  static constexpr std::array<const char*, kLots> kLotKeys = {"coal_lot", "consumer_goods_lot"};

  std::array<std::uint32_t, kLots> lot_rows_{core::kNoTableRow, core::kNoTableRow};
  std::uint32_t seen_delivery_id_ = 0;

  struct Year {
    std::array<std::uint32_t, kLots> lot_door_refused_days{};
    std::array<std::uint32_t, kLots> lot_door_open_days{};
    std::array<std::uint32_t, kLots> lot_door_other_days{};
    std::array<std::uint32_t, kLots> lots_bought{};
    std::uint32_t yard_days = 0;
    std::uint32_t full_days = 0;
    double fill_sum = 0.0;
    std::map<std::size_t, double> by_resource;  // tonne-days held in the yard
    std::array<std::uint32_t, kWatched> lost_days{};
    std::array<double, kWatched> lost_t{};  // this year's book at its last counted day
  };

  struct Watched {
    std::string name;
    std::uint32_t row = core::kNoTableRow;
    std::uint32_t first_day = kNever;
    core::Grams last = 0;
  };

  std::vector<Watched> watched_;

  static std::string DayText(std::uint32_t day) {
    return day == kNever ? std::string("never") : std::to_string(day);
  }

  std::uint32_t yard_ = kNever;
  std::uint32_t goods_store_ = kNever;
  std::vector<std::string> names_;
  std::vector<double> tonnes_by_level_;
  std::vector<Year> years_;
  std::uint32_t first_full_day_ = kNever;
  std::uint32_t goods_store_day_ = kNever;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_YARD_ROOM_PROBE_H_
