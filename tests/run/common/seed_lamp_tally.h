/// @file
/// @brief #1394's instrument: does the «seed short» lamp cry wolf? By village
/// and seed, over years 1-6: the lamp's lit days, its episodes, the sowings
/// that closed short, and which of them a lamp had named before.
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.
///
/// host #1393 read 128 of 135 village-years lit for barley and 0 short sowings
/// on its runs; the lamp counts held seed and reaped heaps against the next
/// sowing's need and never the crop still standing (production_alarms.cpp,
/// SeedShortfall). An episode is a run of lit days on one field; it CAME TRUE
/// when the field's next sowing closed short (FieldRow::sown_share below
/// 1 - kShortShare, the lamp's own share); a short sowing no lamp named before
/// it is MISSED. Lit days while this year's loan of the seed stands booked
/// (PlanState::goods_loan_taken) are counted apart: host #1397's relight.

#ifndef TESTS_RUN_COMMON_SEED_LAMP_TALLY_H_
#define TESTS_RUN_COMMON_SEED_LAMP_TALLY_H_

#include <cstdint>
#include <map>
#include <ostream>
#include <string>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/calendar.h"
#include "core_common/land_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "core_tables/tables.h"

namespace run {

class SeedLampTally {
 public:
  /// The years read, 1..kYears.
  static constexpr std::uint32_t kYears = 6;

  /// A sowing closed below 1 - kShortShare of its field is short: the lamp's
  /// own share (production_alarms.cpp kSeedShortLampShare, 0.05).
  static constexpr float kShortShare = 0.05F;

  explicit SeedLampTally(const core::ITableSet& tables) {
    const core::ITable* const crops = tables.FindTable("crops");
    const core::ITable* const resources = tables.FindTable("resources");
    if (crops == nullptr || resources == nullptr) {
      return;
    }
    const std::uint32_t column = crops->FindColumn("resource");
    for (std::uint32_t row = 0; row < crops->RowCount(); ++row) {
      crop_seed_.push_back(column == core::kNoTableColumn
                               ? core::kNoTableRow
                               : resources->FindRowByKey(crops->CellText(row, column)));
    }
    for (std::uint32_t row = 0; row < resources->RowCount(); ++row) {
      resource_keys_.emplace_back(resources->CellText(row, 0));
    }
  }

  /// @brief #1394's control: `resource` was emptied on `from_day`; the first
  ///        day before `to_day` (absolute) a lamp of it is lit is printed as
  ///        SLCONTROL, or NOT LIT.
  void SetControl(std::uint16_t resource, std::uint32_t from_day, std::uint32_t to_day) {
    control_ = true;
    control_resource_ = resource;
    control_from_ = from_day;
    control_to_ = to_day;
  }

  /// @brief One closed day: the day's alarms and the fields' sowings.
  void CountDay(const core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    const std::uint32_t year = (world.calendar.day / core::kDaysPerYear) + 1U;
    if (year > kYears) {
      return;
    }
    alarms_.clear();
    simulation.CollectAlarms(alarms_);
    std::vector<bool> lit_today(world.fields.rows.size(), false);
    for (const core::Alarm& alarm : alarms_) {
      if (alarm.kind != core::AlarmKind::kSeedShort || alarm.lamp == 0) {
        continue;
      }
      const std::uint32_t row = core::FindRow(world.fields, alarm.field);
      if (row == core::kNoRow) {
        continue;
      }
      lit_today[row] = true;
      if (control_ && control_first_lit_ < 0 && alarm.resource.value == control_resource_ &&
          world.calendar.day >= control_from_ && world.calendar.day < control_to_) {
        control_first_lit_ = static_cast<std::int64_t>(world.calendar.day);
      }
      Seed& seed = seeds_[alarm.resource.value];
      ++seed.lit_days;
      seed.lit_years |= 1U << (year - 1U);
      if (alarm.resource.value < world.plan.goods_loan_taken.size() &&
          world.plan.goods_loan_taken[alarm.resource.value] > 0) {
        ++seed.lit_after_loan_days;
      }
      Field& field = Grow(row);
      if (!field.lit_yesterday) {
        ++seed.episodes;
      }
      field.named = true;
      field.named_seed = alarm.resource.value;
    }
    for (std::uint32_t row = 0; row < world.fields.rows.size(); ++row) {
      const core::FieldRow& now = world.fields.rows[row];
      Field& field = Grow(row);
      field.lit_yesterday = lit_today[row];
      // The sowing CLOSES when the phase leaves kSowing (FinishSowing sets
      // sown_share then).
      const bool closed =
          field.phase == core::FieldPhase::kSowing && now.phase != core::FieldPhase::kSowing;
      field.phase = now.phase;
      if (!closed || now.crop.value >= crop_seed_.size() ||
          crop_seed_[now.crop.value] == core::kNoTableRow) {
        continue;
      }
      const auto resource = static_cast<std::uint16_t>(crop_seed_[now.crop.value]);
      Seed& seed = seeds_[resource];
      ++seed.sowings;
      if (now.sown_share < 1.0F - kShortShare) {
        ++seed.short_sowings;
        ++(field.named && field.named_seed == resource ? seed.came_true : seed.missed);
      }
      field.named = false;
    }
  }

  /// @brief The village's line: one group a seed the lamp lit or a sowing
  ///        took, «SL seed S <key> lit_years N lit_days N episodes N
  ///        came_true N short_sowings N missed N sowings N lit_after_loan_days N».
  void Print(std::ostream& out, std::uint64_t seed) const {
    out << "population_curve: SL seed " << seed << " years 1-" << kYears;
    if (seeds_.empty()) {
      out << " NO SEED LAMP AND NO SOWING (NOT MEASURED)";
    }
    for (const auto& [resource, line] : seeds_) {
      const std::string key =
          resource < resource_keys_.size() ? resource_keys_[resource] : std::to_string(resource);
      std::uint32_t years = 0;
      for (std::uint32_t bits = line.lit_years; bits != 0; bits >>= 1U) {
        years += bits & 1U;
      }
      out << ' ' << key << "_lit_years " << years << ' ' << key << "_lit_days " << line.lit_days
          << ' ' << key << "_episodes " << line.episodes << ' ' << key << "_came_true "
          << line.came_true << ' ' << key << "_short_sowings " << line.short_sowings << ' ' << key
          << "_missed " << line.missed << ' ' << key << "_sowings " << line.sowings << ' ' << key
          << "_lit_after_loan_days " << line.lit_after_loan_days;
    }
    out << '\n';
    if (control_) {
      out << "population_curve: SLCONTROL seed " << seed << " resource " << control_resource_
          << " emptied day " << control_from_ << " window closes day " << control_to_;
      if (control_first_lit_ < 0) {
        out << " NOT LIT\n";
      } else {
        out << " lit day " << control_first_lit_ << " (+" << control_first_lit_ - control_from_
            << " days)\n";
      }
    }
  }

 private:
  struct Seed {
    std::uint32_t lit_years = 0;  ///< A bit a year with a lit day.
    std::uint32_t lit_days = 0;   ///< Field-days lit.
    std::uint32_t episodes = 0;
    std::uint32_t came_true = 0;
    std::uint32_t short_sowings = 0;
    std::uint32_t missed = 0;
    std::uint32_t sowings = 0;
    std::uint32_t lit_after_loan_days = 0;
  };

  struct Field {
    core::FieldPhase phase = core::FieldPhase::kIdle;
    bool lit_yesterday = false;
    bool named = false;  ///< A lamp lit on it since its last sowing closed.
    std::uint16_t named_seed = 0;
  };

  Field& Grow(std::uint32_t row) {
    if (row >= fields_.size()) {
      fields_.resize(row + 1U);
    }
    return fields_[row];
  }

  std::vector<std::uint32_t> crop_seed_;
  std::vector<std::string> resource_keys_;
  std::map<std::uint16_t, Seed> seeds_;
  std::vector<Field> fields_;
  std::vector<core::Alarm> alarms_;
  bool control_ = false;
  std::uint16_t control_resource_ = 0;
  std::uint32_t control_from_ = 0;
  std::uint32_t control_to_ = 0;
  std::int64_t control_first_lit_ = -1;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_SEED_LAMP_TALLY_H_
