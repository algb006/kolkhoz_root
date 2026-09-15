/// @file
/// @brief The watchman the run's chairman appoints at the yard where the
/// village's raw material lies.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THE RUN APPOINTS THIS. A night post keeps the stores from the
/// distiller (crime design §11; the leak's body) and goes on its shift at
/// sunset (posts.h) — and no run had ever seen either, because a run has no
/// chairman to appoint one (boss, parcel 362: the prosthesis appoints a
/// watchman). The run plays the player here, as it does for the groom
/// (yard_policy.h), and says so out loud.
///
/// THE RULE IS THE RUN'S, NOT THE GAME'S: nothing before the chairman's yard
/// stands (the horses stabled); then ONE watchman, at the standing unit the
/// staff table gives a watchman whose own stock and its modules' hold the most
/// of the distiller's raw material (rye, wheat, barley, oat, potato, sugar) —
/// the food yard with its granaries, as a rule. Nothing lies anywhere guarded:
/// nobody is appointed. Once appointed he stays; a lost post is filled again.

#ifndef TESTS_RUN_COMMON_WATCHMAN_POLICY_H_
#define TESTS_RUN_COMMON_WATCHMAN_POLICY_H_

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/ids.h"
#include "core_common/order_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace run {

/// @brief Appoints one watchman where the raw material lies, and keeps the
///        post filled.
class WatchmanPolicy {
 public:
  explicit WatchmanPolicy(const core::ITableSet& tables) {
    const core::ITable* professions = tables.FindTable("professions");
    const std::uint32_t post_row =
        professions == nullptr ? core::kNoTableRow : professions->FindRowByKey("watchman");
    if (post_row != core::kNoTableRow) {
      post_ = core::ProfessionId{static_cast<std::uint16_t>(post_row)};
    }
    const core::ITable* types = tables.FindTable("unit_types");
    const core::ITable* staff = tables.FindTable("unit_staff");
    if (types != nullptr && staff != nullptr) {
      guarded_.assign(types->RowCount(), 0);
      for (std::uint32_t row = 0; row < types->RowCount(); ++row) {
        type_keys_.emplace_back(types->CellText(row, types->FindColumn("key")));
      }
      const std::uint32_t unit_col = staff->FindColumn("unit");
      const std::uint32_t post_col = staff->FindColumn("profession");
      for (std::uint32_t row = 0; row < staff->RowCount(); ++row) {
        if (unit_col == core::kNoTableColumn || post_col == core::kNoTableColumn ||
            staff->CellText(row, post_col) != "watchman") {
          continue;
        }
        const std::uint32_t type_row = types->FindRowByKey(staff->CellText(row, unit_col));
        if (type_row != core::kNoTableRow) {
          guarded_[type_row] = 1;
        }
      }
    }
    const core::ITable* resources = tables.FindTable("resources");
    for (const std::string_view key : kRawMaterial) {
      const std::uint32_t row =
          resources == nullptr ? core::kNoTableRow : resources->FindRowByKey(key);
      if (row != core::kNoTableRow) {
        raw_.push_back(core::ResourceId{static_cast<std::uint16_t>(row)});
      }
    }
    const core::ITable* life = tables.FindTable("life");
    if (life != nullptr) {
      const std::uint32_t row = life->FindRowByKey("life_speedup");
      const std::uint32_t column = life->FindColumn("value");
      if (row != core::kNoTableRow && column != core::kNoTableColumn) {
        const float value = std::strtof(std::string(life->CellText(row, column)).c_str(), nullptr);
        life_speedup_ = value > 0.0F ? value : life_speedup_;
      }
    }
  }

  /// @brief One day of the chairman's attention. Call once a day.
  void RunDay(core::ISimulation& simulation) {
    if (post_.value == core::kInvalidDefIdValue) {
      return;
    }
    const core::WorldState& world = simulation.CompletedState();
    if (world.chairman.horses_stabled == 0) {
      return;
    }
    if (cooldown_ > 0) {
      --cooldown_;
      return;
    }
    for (const core::ResidentRow& person : world.residents.rows) {
      if (person.post.profession.value == post_.value) {
        return;  // held
      }
    }
    const std::uint32_t unit_row = BestUnit(world);
    const std::uint32_t candidate = NextCandidate(world);
    if (unit_row == core::kNoRow || candidate == core::kNoRow) {
      return;
    }
    core::OrderRow order;
    order.kind = core::OrderKind::kAppoint;
    order.resident = world.residents.row_ids[candidate];
    order.unit = world.units.row_ids[unit_row];
    order.profession = post_;
    simulation.StageOrders(std::span<const core::OrderRow>(&order, 1), {});
    ++orders_;
    if (first_order_day_ < 0) {
      first_order_day_ = static_cast<std::int64_t>(world.calendar.day);
    }
    cooldown_ = kCooldownDays;
  }

  /// @brief The fixture difference, in words, BEFORE the run measures.
  static void Declare(std::string_view run_name) {
    std::cout << run_name
              << ": FIXTURE DIFFERS FROM THE START CANON — once the chairman's yard stands, the "
                 "run's chairman appoints ONE WATCHMAN at the staffed yard holding the most raw "
                 "material, and fills the post again when it is lost (boss, parcel 362)\n";
  }

  /// @brief What the fixture did, for the run to print at the end.
  void Report(const core::WorldState& world, std::string_view run_name) const {
    std::uint32_t holders = 0;
    std::string at = "-";
    for (const core::ResidentRow& person : world.residents.rows) {
      if (person.post.profession.value != post_.value) {
        continue;
      }
      ++holders;
      const std::uint32_t unit_row = core::FindRow(world.units, person.post.unit);
      const std::size_t type =
          unit_row == core::kNoRow
              ? type_keys_.size()
              : static_cast<std::size_t>(world.units.rows[unit_row].type.value);
      at = type < type_keys_.size() ? type_keys_[type] : "?";
    }
    // The CLOSED book: a run ends on a year's turn, and the open one is empty
    // there (the first cut read it and said 0 kg on every seed).
    core::Grams stolen = 0;
    for (const core::Grams grams : world.ledger.closed.stolen) {
      stolen += grams;
    }
    std::cout << run_name << ": watchman — " << orders_ << " appointment orders (first on day "
              << first_order_day_ << "); " << holders << " holding the post at the end (at " << at
              << "); " << stolen / core::kGramsPerKilogram
              << " kg carried off in the last closed year\n";
  }

 private:
  static constexpr std::array<std::string_view, 6> kRawMaterial = {
      "rye", "wheat", "barley", "oat", "potato", "sugar"};

  static constexpr std::uint32_t kCooldownDays = 2;

  /// The standing staffed unit whose own and modules' stock hold the most raw
  /// material; kNoRow when none holds any.
  std::uint32_t BestUnit(const core::WorldState& world) const {
    std::uint32_t best = core::kNoRow;
    core::Grams best_grams = 0;
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      if (unit.level == 0 || unit.type.value >= guarded_.size() || guarded_[unit.type.value] == 0) {
        continue;
      }
      const core::UnitId id = world.units.row_ids[row];
      core::Grams grams = RawIn(unit);
      for (const core::UnitRow& module : world.units.rows) {
        grams += module.parent.value == id.value ? RawIn(module) : 0;
      }
      if (grams > best_grams) {
        best_grams = grams;
        best = row;
      }
    }
    return best;
  }

  core::Grams RawIn(const core::UnitRow& unit) const {
    core::Grams grams = 0;
    for (const core::ResourceId resource : raw_) {
      grams += core::AmountOf(unit.stock, resource);
    }
    return grams;
  }

  /// An adult with no post, a different one each time the last did not take.
  std::uint32_t NextCandidate(const core::WorldState& world) {
    const std::uint32_t skip = candidate_++;
    std::uint32_t seen = 0;
    for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
      const core::ResidentRow& person = world.residents.rows[row];
      if (person.post.profession.value != core::kInvalidDefIdValue) {
        continue;
      }
      const float age =
          core::BiologicalAgeYears(life_speedup_, person.birth_day, world.calendar.day);
      if (age < kAdultYears) {
        continue;
      }
      if (seen++ < skip % (world.residents.rows.size() + 1U)) {
        continue;
      }
      return row;
    }
    candidate_ = 0;
    return core::kNoRow;
  }

  static constexpr float kAdultYears = 16.0F;

  core::ProfessionId post_;

  std::vector<std::uint8_t> guarded_;

  std::vector<std::string> type_keys_;

  std::vector<core::ResourceId> raw_;

  float life_speedup_ = 4.0F;

  std::uint32_t cooldown_ = 0;

  std::uint32_t candidate_ = 0;

  std::uint32_t orders_ = 0;

  std::int64_t first_order_day_ = -1;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_WATCHMAN_POLICY_H_
