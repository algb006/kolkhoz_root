/// @file
/// @brief #2264 (boss, econ's field-work coordinator, before any pair): FIELD2264, y1-y3 - the
///        prints M1-M4 by the crop's RESOURCE (crops.csv `resource`; vegetables are one).
///
/// THE GROUND, printed with the lines:
/// - M1 hectares: every field seen in kGrowing in the year, area_ga x sown_share, once a field.
/// - M2 worker-days and horse-days: at hour 13 (the noon sample, as strada_tally.h) every
///   resident on kPlowing / kHarrowing / kSowing / kHarvest / kHauling with a field target,
///   one a resident-day, a horse-day when WorkAssignment::rides_horse. Worker-days at noon, not
///   norm man-days. A hauling hand is booked to the heap's reaped_resource (the field's crop is
///   cleared at payout), a field hand to the field's crop.
/// - M3 the haul gap: a field's day its phase left kHarvest with a heap, to the first noon its
///   heap read 0, in days. Heap out: the sum of noon-to-noon falls of reaped_grams. The year's
///   closed book gives hauled_to_stores and lost_to_snow by resource; rot = out - hauled - snow,
///   the REMAINDER, not a counter (any other door off a heap lands in it too).
/// - M4 spring (human months 3-5): horse-days of plough + harrow + carting at noon, against the
///   adult kolkhoz horses (DraughtHorsesOf) summed over the same noons.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.

#ifndef TESTS_RUN_COMMON_FIELD_WORK_TALLY_H_
#define TESTS_RUN_COMMON_FIELD_WORK_TALLY_H_

#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/labor_state.h"
#include "core_common/land_state.h"
#include "core_common/ledger_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/work_seam.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

class FieldWorkTally {
 public:
  static constexpr std::uint32_t kYears = 3;
  static constexpr std::uint32_t kNoonHour = 13;

  explicit FieldWorkTally(const core::ITableSet& tables) {
    std::map<std::string, std::uint32_t> resource_rows;
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      for (std::uint32_t row = 0; row < resources->RowCount(); ++row) {
        resource_names_.emplace_back(resources->CellText(row, 0));
        resource_rows[std::string(resources->CellText(row, 0))] = row;
      }
    }
    if (const core::ITable* const crops = tables.FindTable("crops")) {
      const std::uint32_t resource = crops->FindColumn("resource");
      for (std::uint32_t row = 0; row < crops->RowCount(); ++row) {
        const auto found = resource == core::kNoTableColumn
                               ? resource_rows.end()
                               : resource_rows.find(std::string(crops->CellText(row, resource)));
        crop_resource_.push_back(found == resource_rows.end() ? kNone : found->second);
      }
    }
    if (const core::ITable* const kinds = tables.FindTable("livestock")) {
      horse_kind_ = kinds->FindRowByKey("horse");
    }
  }

  /// @brief At hour 13 of every day.
  void CountNoon(const core::WorldState& world) {
    const std::uint32_t year = world.calendar.date.year;
    if (year < 1 || year > kYears) {
      return;
    }
    Year& book = years_[year - 1];
    const core::SimDay day = world.calendar.day;
    for (std::size_t row = 0; row < world.fields.rows.size(); ++row) {
      const core::FieldRow& field = world.fields.rows[row];
      const std::uint32_t id = world.fields.row_ids[row].value;
      if (field.phase == core::FieldPhase::kGrowing && CropResource(field.crop) != kNone &&
          book.seen_growing.insert(id).second) {
        book.hectares[CropResource(field.crop)] +=
            static_cast<double>(field.area_ga) * static_cast<double>(field.sown_share);
      }
      FieldMemory& memory = fields_[id];
      const std::uint32_t heap_resource =
          field.reaped_grams > 0 ? field.reaped_resource.value : memory.heap_resource;
      if (field.reaped_grams < memory.heap_grams && heap_resource != kNone) {
        book.heap_out_grams[heap_resource] +=
            static_cast<double>(memory.heap_grams - field.reaped_grams);
      }
      if (memory.phase == core::FieldPhase::kHarvest && field.phase != core::FieldPhase::kHarvest &&
          field.reaped_grams > 0) {
        memory.harvest_closed_day = day;
        memory.waiting = true;
      }
      if (memory.waiting && field.reaped_grams == 0) {
        memory.waiting = false;
        book.haul_gap_days[memory.heap_resource].push_back(day - memory.harvest_closed_day);
      }
      memory.phase = field.phase;
      memory.heap_grams = field.reaped_grams;
      if (field.reaped_grams > 0) {
        memory.heap_resource = field.reaped_resource.value;
      }
    }
    const auto month = static_cast<std::uint32_t>(world.calendar.date.month) + 1U;  // human
    const bool spring = month >= 3 && month <= 5;
    for (const core::ResidentRow& resident : world.residents.rows) {
      const core::WorkAssignment& work = resident.work;
      const auto kind = static_cast<std::uint32_t>(work.kind);
      const bool field_kind =
          work.kind == core::WorkKind::kPlowing || work.kind == core::WorkKind::kHarrowing ||
          work.kind == core::WorkKind::kSowing || work.kind == core::WorkKind::kHarvest ||
          work.kind == core::WorkKind::kHauling;
      if (!field_kind || work.field.value == core::kInvalidDefIdValue) {
        continue;
      }
      const std::uint32_t field_row = core::FindRow(world.fields, work.field);
      if (field_row == core::kNoRow) {
        continue;
      }
      const core::FieldRow& field = world.fields.rows[field_row];
      const std::uint32_t resource =
          work.kind == core::WorkKind::kHauling
              ? (field.reaped_grams > 0 ? field.reaped_resource.value : kNone)
              : CropResource(field.crop);
      Cell& cell = book.work[{resource, kind}];
      cell.worker_days += 1.0;
      cell.horse_days += work.rides_horse != 0 ? 1.0 : 0.0;
      if (spring && work.rides_horse != 0) {
        book.spring_horse_days += 1.0;
      }
    }
    if (spring && horse_kind_ != core::kNoTableRow) {
      book.spring_draught_horse_days += static_cast<double>(core::DraughtHorsesOf(
          world, core::DefIdFromIndex<core::LivestockKindIdTag>(horse_kind_)));
    }
  }

  /// @brief The year's closed book, at the year turn.
  void CloseYear(const core::YearLedger& book, std::uint32_t year) {
    if (year < 1 || year > kYears) {
      return;
    }
    Year& tally = years_[year - 1];
    const auto read = [](const core::ResourceAmounts& amounts, std::uint32_t resource) {
      return resource < amounts.size() ? static_cast<double>(amounts[resource]) : 0.0;
    };
    for (const auto& [resource, out] : tally.heap_out_grams) {
      tally.hauled_grams[resource] = read(book.hauled_to_stores, resource);
      tally.snow_grams[resource] = read(book.lost_to_snow, resource);
      tally.harvest_grams[resource] = read(book.harvest, resource);
    }
    tally.closed = true;
  }

  /// @brief The village's lines.
  void Print(std::ostream& out, std::uint64_t seed) const {
    for (std::uint32_t index = 0; index < kYears; ++index) {
      const Year& book = years_[index];
      const std::string head = "population_curve: FIELD2264 seed " + std::to_string(seed) +
                               " year " + std::to_string(index + 1);
      out << head << " M1 ground=area_ga_x_sown_share_of_fields_seen_growing ha";
      for (const auto& [resource, hectares] : book.hectares) {
        out << ' ' << Name(resource) << ' ' << hectares;
      }
      out << '\n'
          << head
          << " M2 ground=residents_at_noon_by_crop_resource_x_kind (worker_days/horse_days)";
      for (const auto& [key, cell] : book.work) {
        out << ' ' << Name(key.first) << ':' << KindName(key.second) << ' ' << cell.worker_days
            << '/' << cell.horse_days;
      }
      out << '\n' << head << " M3 ground=heap_out_by_noon_falls; rot=out-hauled-snow (a remainder)";
      if (!book.closed) {
        out << " NOT MEASURED: the year's book did not close";
      }
      for (const auto& [resource, grams] : book.heap_out_grams) {
        const double hauled =
            book.hauled_grams.contains(resource) ? book.hauled_grams.at(resource) : 0.0;
        const double snow = book.snow_grams.contains(resource) ? book.snow_grams.at(resource) : 0.0;
        const double harvest =
            book.harvest_grams.contains(resource) ? book.harvest_grams.at(resource) : 0.0;
        out << " | " << Name(resource) << " harvest_kg " << harvest / 1000.0 << " heap_out_kg "
            << grams / 1000.0 << " hauled_kg " << hauled / 1000.0 << " snow_kg " << snow / 1000.0
            << " rot_kg " << (grams - hauled - snow) / 1000.0;
        const auto gaps = book.haul_gap_days.find(resource);
        if (gaps != book.haul_gap_days.end() && !gaps->second.empty()) {
          std::uint32_t sum = 0;
          std::uint32_t worst = 0;
          for (const std::uint32_t gap : gaps->second) {
            sum += gap;
            worst = gap > worst ? gap : worst;
          }
          out << " haul_gap_days fields " << gaps->second.size() << " mean "
              << static_cast<double>(sum) / static_cast<double>(gaps->second.size()) << " worst "
              << worst;
        } else {
          out << " haul_gap_days NONE CLOSED (a heap still lying at the year's end, or no reaping)";
        }
      }
      out << '\n'
          << head << " M4 ground=spring_human_months_3_5_at_noon horse_days_plough_harrow_cart "
          << book.spring_horse_days << " draught_horse_days " << book.spring_draught_horse_days
          << " share "
          << (book.spring_draught_horse_days > 0.0
                  ? book.spring_horse_days / book.spring_draught_horse_days
                  : 0.0)
          << '\n';
    }
  }

 private:
  static constexpr std::uint32_t kNone = 0xFFFFFFFFU;

  struct Cell {
    double worker_days = 0.0;
    double horse_days = 0.0;
  };

  struct Year {
    std::set<std::uint32_t> seen_growing;
    std::map<std::uint32_t, double> hectares;
    std::map<std::pair<std::uint32_t, std::uint32_t>, Cell> work;
    std::map<std::uint32_t, double> heap_out_grams;
    std::map<std::uint32_t, double> hauled_grams;
    std::map<std::uint32_t, double> snow_grams;
    std::map<std::uint32_t, double> harvest_grams;
    std::map<std::uint32_t, std::vector<std::uint32_t>> haul_gap_days;
    double spring_horse_days = 0.0;
    double spring_draught_horse_days = 0.0;
    bool closed = false;
  };

  struct FieldMemory {
    core::FieldPhase phase = core::FieldPhase::kIdle;
    core::Grams heap_grams = 0;
    std::uint32_t heap_resource = kNone;
    core::SimDay harvest_closed_day = 0;
    bool waiting = false;
  };

  [[nodiscard]] std::uint32_t CropResource(core::CropId crop) const {
    return crop.value < crop_resource_.size() ? crop_resource_[crop.value] : kNone;
  }

  [[nodiscard]] std::string Name(std::uint32_t resource) const {
    return resource < resource_names_.size() ? resource_names_[resource] : "unknown";
  }

  static std::string_view KindName(std::uint32_t kind) {
    switch (static_cast<core::WorkKind>(kind)) {
      case core::WorkKind::kPlowing:
        return "plough";
      case core::WorkKind::kHarrowing:
        return "harrow";
      case core::WorkKind::kSowing:
        return "sow";
      case core::WorkKind::kHarvest:
        return "harvest";
      case core::WorkKind::kHauling:
        return "haul";
      default:
        return "other";
    }
  }

  std::vector<std::string> resource_names_;
  std::vector<std::uint32_t> crop_resource_;
  std::uint32_t horse_kind_ = core::kNoTableRow;
  std::array<Year, kYears> years_{};
  std::map<std::uint32_t, FieldMemory> fields_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_FIELD_WORK_TALLY_H_
