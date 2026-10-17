/// @file ring_policy.h
/// @brief THE REFERENCE RING for the derelict fields: econ's joint feed plan
///        (econ/manual/proposals/joint-feed-plan-reference-rotation-2026-10-17.md
///        §3a), as boss set it in boss-core-reference-ring-2026-10-17 [4], [6].
///        Shared by the core's canon runs and host's plan700 `--ring` arm.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// THE RING: S1 oat, S2 clover, S3 clover, S4 rye_winter (sown the August
/// before), S5 oat, S6 barley; a field moves one slot a year. The core's chain
/// has three slots, and a kSetRotation given to a standing grass stand ENDS it
/// (SetRotation; farming design «Обновление — тот же приказ севооборота»),
/// so the ring is two three-year windows, each given only AFTER an annual crop
/// is reaped (the core's C1 and C2, the ring thread [5]):
///   W1 = [clover, clover, rye_winter], given when the field enters S2;
///   W2 = [oat, barley, oat], given when it enters S5.
/// The clover ends at its S3 cut by the chain's own progression, and the rye
/// opens that August (field_work.cpp, the reaping's end; TrySowWinter).
///
/// THE ENTRIES (econ §3a): _3 -> year 2 at S5, _1 -> year 2 at S1, _4 -> year 3
/// at S5, _2 -> year 3 at S1, _6 -> year 4 at S5; _5 -> year 4 at S4 through
/// [fallow, rye_winter, oat] given in year 3's summer (the core's B3). A field
/// entering at S5 or S1 is given its first window on the entry year's first
/// day: a derelict field has no autumn furrow (ZyabDue reads a reaping), so
/// it is ploughed in the spring (the ring thread [4], B2).
///
/// PRINTED, not judged: per field and year the slot, the window given (entry,
/// W1, W2 or none) and its day; and «RING C1 FAIL» where a field in S3 does not
/// stand in rye when the rye's window has closed (the S3 cut ran past it).

#ifndef TESTS_RUN_COMMON_RING_POLICY_H_
#define TESTS_RUN_COMMON_RING_POLICY_H_

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/ids.h"
#include "core_common/land_state.h"
#include "core_common/order_state.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "core_tables/tables.h"

namespace run {

class RingPolicy {
 public:
  /// @brief Reads the ring's crops off crops.csv and the derelict fields'
  ///        places off start_layout.csv.
  explicit RingPolicy(const core::ITableSet& tables) {
    const core::ITable* const crops = tables.FindTable("crops");
    const core::ITable* const layout = tables.FindTable("start_layout");
    if (crops == nullptr || layout == nullptr) {
      return;
    }
    const auto crop = [crops](const char* key) {
      const std::uint32_t row = crops->FindRowByKey(key);
      return row == core::kNoTableRow ? core::CropId{} : core::DefIdFromIndex<core::CropIdTag>(row);
    };
    ring_ = {crop("oat"),
             crop("clover"),
             crop("clover"),
             crop("rye_winter"),
             crop("oat"),
             crop("barley")};
    for (const core::CropId slot : ring_) {
      ready_ = ready_ || slot.value != core::kInvalidDefIdValue;
    }
    for (const Entry& entry : kEntries) {
      const std::uint32_t row = layout->FindRowByKey(entry.key);
      Field field;
      field.entry = entry;
      if (row != core::kNoTableRow) {
        field.x = Cell(*layout, row, "x_m");
        field.y = Cell(*layout, row, "y_m");
        field.known = true;
      }
      fields_.push_back(field);
    }
  }

  /// @brief The fixture difference, in words, before anything is measured.
  static void Declare(std::string_view run) {
    std::cout << run
              << ": FIXTURE DIFFERS FROM THE START CANON - the run's chairman raises the six "
                 "derelict fields into the REFERENCE RING (S1 oat, S2 clover, S3 clover, S4 "
                 "rye_winter, S5 oat, S6 barley; econ's joint feed plan §3a) by two three-year "
                 "windows given after an annual crop is reaped (boss, the ring thread [4], [6])\n";
  }

  /// @brief One day of the ring: binds the fields once, gives the windows due,
  ///        prints the year's line on its last day. Call once a day.
  void RunDay(core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    if (!ready_) {
      return;
    }
    Bind(world);
    const core::SimDay day = world.calendar.day;
    const core::SimDay doy = day % core::kDaysPerYear;
    const auto year = static_cast<std::int32_t>(day / core::kDaysPerYear) + 1;
    std::vector<core::OrderRow> orders;
    for (Field& field : fields_) {
      const std::uint32_t row = core::FindRow(world.fields, field.id);
      if (row == core::kNoRow) {
        continue;
      }
      const core::FieldRow& state = world.fields.rows[row];
      if (field.given_year == year) {
        continue;  // one window a field a year
      }
      std::array<core::CropId, 3> window{};
      const char* what = nullptr;
      if (!field.entered) {
        // THE ENTRY: the rye's field in the summer before (doy 24, June's
        // first day, so the August window finds it); every other on the
        // entry year's first day, for the spring's own opening.
        const bool rye_entry = field.entry.slot == kRyeSlot;
        const std::int32_t when_year = rye_entry ? field.entry.year - 1 : field.entry.year;
        const core::SimDay when_doy = rye_entry ? kRyeEntryDayOfYear : 0U;
        if (year != when_year || doy != when_doy) {
          continue;
        }
        if (rye_entry) {
          window = {core::CropId{}, ring_[kRyeSlot], ring_[(kRyeSlot + 1) % kSlots]};
        } else {
          window = {ring_[field.entry.slot],
                    ring_[(field.entry.slot + 1) % kSlots],
                    ring_[(field.entry.slot + 2) % kSlots]};
        }
        what = "entry";
        field.entered = true;
      } else {
        // AFTER AN ANNUAL IS REAPED (C2): this year's crop off the field, the
        // field idle; the window of the slot it enters next year.
        const bool reaped_this_year =
            state.reaped_day != core::kNeverReapedDay &&
            state.reaped_day / core::kDaysPerYear == day / core::kDaysPerYear;
        if (!reaped_this_year || state.phase != core::FieldPhase::kIdle) {
          continue;
        }
        const std::uint32_t next = (SlotIn(field, year) + 1U) % kSlots;
        if (next == kCloverFirstSlot) {
          window = {ring_[1], ring_[2], ring_[3]};
          what = "W1";
        } else if (next == kOatAfterRyeSlot) {
          window = {ring_[4], ring_[5], ring_[0]};
          what = "W2";
        } else {
          continue;
        }
      }
      core::OrderRow order;
      order.kind = core::OrderKind::kSetRotation;
      order.field = field.id;
      order.rotation_year0 = window[0];
      order.rotation_year1 = window[1];
      order.rotation_year2 = window[2];
      orders.push_back(order);
      field.given_year = year;
      field.given_what = what;
      field.given_day = day;
    }
    if (!orders.empty()) {
      simulation.StageOrders(std::span<const core::OrderRow>(orders), {});
    }
    // C1's CONDITION, PRINTED: a field in S3 stands in rye once the rye's
    // window (Aug-Sep, October's grace) has closed.
    if (doy == kAfterRyeWindowDayOfYear) {
      for (const Field& field : fields_) {
        const std::uint32_t row = core::FindRow(world.fields, field.id);
        if (!field.entered || row == core::kNoRow || SlotIn(field, year) != kCloverLastSlot) {
          continue;
        }
        const core::FieldRow& state = world.fields.rows[row];
        if (state.crop.value != ring_[kRyeSlot].value) {
          ++c1_failures_;
          std::cout << "RING C1 FAIL, year " << year << ": " << field.entry.key
                    << " in S3 holds no rye when the rye's window has closed (crop "
                    << state.crop.value << ", phase " << static_cast<int>(state.phase) << ", slots "
                    << state.rotation_year0.value << '/' << state.rotation_year1.value << '/'
                    << state.rotation_year2.value << ", skips "
                    << static_cast<int>(state.rotation_skips_turn) << ", reaped day "
                    << state.reaped_day << ", last cut " << state.last_cut_day << ")\n";
        }
      }
    }
    if (doy == core::kDaysPerYear - 1U) {
      for (Field& field : fields_) {
        std::cout << "RING,year=" << year << ",field=" << field.entry.key
                  << ",slot=" << (field.entered ? std::to_string(SlotIn(field, year) + 1U) : "-")
                  << ",given=" << (field.given_year == year ? field.given_what : "none")
                  << ",day=" << (field.given_year == year ? std::to_string(field.given_day) : "-")
                  << '\n';
      }
    }
  }

  /// @brief The ring's count: fields bound, and C1's failures.
  void Report(std::string_view run) const {
    std::uint32_t bound = 0;
    for (const Field& field : fields_) {
      bound += field.id.value != core::kInvalidEntityIdValue ? 1U : 0U;
    }
    std::cout << run << ": the reference ring - " << bound << " of " << fields_.size()
              << " derelict fields bound, C1 failed " << c1_failures_ << " time(s)\n";
  }

 private:
  struct Entry {
    const char* key;
    std::int32_t year;   ///< the campaign year (1-based) the field first stands in the ring
    std::uint32_t slot;  ///< its slot that year, 0-based (S1 = 0)
  };

  struct Field {
    Entry entry{};
    float x = 0.0F;
    float y = 0.0F;
    bool known = false;
    core::FieldId id;
    bool entered = false;
    std::int32_t given_year = 0;
    const char* given_what = "none";
    core::SimDay given_day = 0;
  };

  static constexpr std::uint32_t kSlots = 6;
  static constexpr std::uint32_t kCloverFirstSlot = 1;  // S2
  static constexpr std::uint32_t kCloverLastSlot = 2;   // S3
  static constexpr std::uint32_t kRyeSlot = 3;          // S4
  static constexpr std::uint32_t kOatAfterRyeSlot = 4;  // S5
  /// June's first day, the summer before a rye entry (B3).
  static constexpr core::SimDay kRyeEntryDayOfYear = 6U * core::kDaysPerMonth;
  /// November's first day: the rye's window (to September, October's grace)
  /// has closed (crops.csv rye_winter; field_work.cpp, WinterLastSowingMonth).
  static constexpr core::SimDay kAfterRyeWindowDayOfYear = 10U * core::kDaysPerMonth;
  /// A layout point binds the field whose centre lies within this many metres.
  static constexpr float kBindMetres = 5.0F;

  static constexpr std::array<Entry, 6> kEntries = {{{"field_derelict_3", 2, 4},
                                                     {"field_derelict_1", 2, 0},
                                                     {"field_derelict_4", 3, 4},
                                                     {"field_derelict_2", 3, 0},
                                                     {"field_derelict_6", 4, 4},
                                                     {"field_derelict_5", 4, 3}}};

  static float Cell(const core::ITable& table, std::uint32_t row, const char* column) {
    const std::string text(table.CellText(row, table.FindColumn(column)));
    return text.empty() ? 0.0F : std::strtof(text.c_str(), nullptr);
  }

  /// The field's ring slot in `year` (0-based), from its entry.
  static std::uint32_t SlotIn(const Field& field, std::int32_t year) {
    const std::int32_t moved = year - field.entry.year;
    const auto slot = static_cast<std::int32_t>(field.entry.slot) + moved;
    return static_cast<std::uint32_t>(
        ((slot % static_cast<std::int32_t>(kSlots)) + static_cast<std::int32_t>(kSlots)) %
        static_cast<std::int32_t>(kSlots));
  }

  /// The fields, bound once by their layout point.
  void Bind(const core::WorldState& world) {
    if (bound_) {
      return;
    }
    bound_ = true;
    for (Field& field : fields_) {
      if (!field.known) {
        continue;
      }
      for (std::uint32_t row = 0; row < world.fields.rows.size(); ++row) {
        const core::Vec2 centre = world.fields.rows[row].center;
        if (std::hypot(centre.x - field.x, centre.y - field.y) <= kBindMetres) {
          field.id = world.fields.row_ids[row];
          break;
        }
      }
    }
  }

  std::array<core::CropId, kSlots> ring_{};
  std::vector<Field> fields_;
  bool ready_ = false;
  bool bound_ = false;
  std::uint32_t c1_failures_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_RING_POLICY_H_
