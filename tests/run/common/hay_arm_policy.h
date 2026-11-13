/// @file
/// @brief The run's chairman puts the derelict arable to hay (#307; econ's
///        hay-arm plan §9, econ/manual/proposals/hay-arm-plan-new-base-2026-10-10.md).
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// THE ARMS (econ §2 and §9), one field a year from y2, the derelict fields
/// nearest the stock (a built cattle yard or barn) first, and only a field
/// still free that year - no chain, not mown already (the plan's growing base,
/// FallowPolicy, takes its share first; a field it later takes back is
/// printed, not fought):
///   * A, clover (--hay-arm=clover): on October's first day, after the winter
///     sowing (0.37.208's lesson: never in the rye's weeks), kSetRotation with
///     clover in all three slots - ploughed now, sown next spring; the stand's
///     renewal after its last summer is StandRenewalPolicy's, as on every arm;
///   * B, mown as it lies (--hay-arm=mown): on the year's first day, the
///     meadow door's second form (kMarkMeadow on the field);
///   * A+B (--hay-arm=both): B marks a field on the year's first day and A
///     sets that same field to clover in October - one summer mown, then clover.
/// THE CANON MOWS (B) since 0.37.378 (the human, #1923, 13 Nov: «По сену на ваш выбор»; boss
/// chose econ's option 1): B is every run's default; A and A+B stay arms; --hay-arm=off is the
/// arm without it. The named miss: one village in 32 (2031) loses ~15 % of its rye over y6-y16
/// when the grain base takes back B's fields - no yield rule for now.
///
/// THE PRINT (HAY307, a village-year, base and arms alike - the gates W1, W2
/// and the red lines are read off it): hay harvested and cut, milk made, adult
/// cows and horses at the year's close, the hay lamp's lit days, kNoHorse idle
/// person-days, rye harvested, residents; and the fields the arm holds.

#ifndef TESTS_RUN_COMMON_HAY_ARM_POLICY_H_
#define TESTS_RUN_COMMON_HAY_ARM_POLICY_H_

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/calendar.h"
#include "core_common/labor_state.h"
#include "core_common/order_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "core_tables/tables.h"

namespace run {

class HayArmPolicy {
 public:
  enum class Mode : std::uint8_t { kOff, kClover, kMown, kBoth };

  static constexpr std::uint32_t kYears = 16;
  static constexpr std::uint32_t kOctober = 9;  ///< 0-based month of A's ploughing

  explicit HayArmPolicy(const core::ITableSet& tables) {
    const auto row_of = [&tables](const char* table, const char* key) {
      const core::ITable* const t = tables.FindTable(table);
      return t != nullptr ? t->FindRowByKey(key) : core::kNoTableRow;
    };
    clover_ = row_of("crops", "clover");
    cattle_yard_ = row_of("unit_types", "cattle_yard");
    barn_ = row_of("unit_types", "barn");
    hay_ = row_of("resources", "hay");
    milk_ = row_of("resources", "milk");
    rye_ = row_of("resources", "rye");
    cow_ = row_of("livestock", "cow");
    horse_ = row_of("livestock", "horse");
  }

  void SetMode(Mode mode) { mode_ = mode; }

  [[nodiscard]] Mode GetMode() const { return mode_; }

  /// @brief The fixture difference, BEFORE the run measures.
  void Declare(std::string_view run_name) const {
    if (mode_ == Mode::kMown) {
      return;  // the canon (since 0.37.378)
    }
    if (mode_ == Mode::kOff) {
      std::cout << run_name << ": ARM — --hay-arm=off (#307): the run's chairman leaves the "
                << "derelict fields unmown (the canon mows them since 0.37.378)\n";
      return;
    }
    const char* what = mode_ == Mode::kClover ? "clover (A)" : "mown, then clover (A+B)";
    std::cout << run_name << ": ARM — --hay-arm (#307): one derelict field a year from y2, the "
              << "nearest the stock first, put to hay: " << what << '\n';
  }

  /// @brief One day: the year's line at the turn, the lamp's day, at most one order.
  void RunDay(core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    const core::SimDay day = world.calendar.day;
    const std::uint32_t year = (day / core::kDaysPerYear) + 1U;
    if (day % core::kDaysPerYear == 0 && year >= 2 && year - 1U <= kYears && day != closed_day_) {
      closed_day_ = day;
      CloseYear(world, year - 1U);
    }
    CountLamp(simulation);
    if (mode_ == Mode::kOff || year < 2 || year > kYears) {
      return;
    }
    if (!candidates_read_) {
      ReadCandidates(world);
    }
    core::OrderRow order;
    const bool mow_day = day % core::kDaysPerYear == 0 && marked_year_ != year;
    const bool clover_day =
        static_cast<std::uint32_t>(world.calendar.date.month) == kOctober && clovered_year_ != year;
    if ((mode_ == Mode::kMown || mode_ == Mode::kBoth) && mow_day) {
      marked_year_ = year;
      if (NextFree(world, order.field)) {
        order.kind = core::OrderKind::kMarkMeadow;
        mown_.push_back(order.field);
        Stage(simulation, order);
        ++marked_this_year_;
      }
      return;
    }
    if ((mode_ == Mode::kClover || mode_ == Mode::kBoth) && clover_day) {
      clovered_year_ = year;
      core::FieldId field;
      const bool found = mode_ == Mode::kBoth ? OldestMown(world, field) : NextFree(world, field);
      if (found) {
        order.kind = core::OrderKind::kSetRotation;
        order.field = field;
        const core::CropId clover{static_cast<std::uint16_t>(clover_)};
        order.rotation_year0 = clover;
        order.rotation_year1 = clover;
        order.rotation_year2 = clover;
        clovered_.push_back(field);
        Stage(simulation, order);
        ++clovered_this_year_;
      }
    }
  }

  /// @brief The village's lines.
  void Print(std::ostream& out, std::uint64_t seed) const {
    std::istringstream lines(lines_.str());
    std::string line;
    while (std::getline(lines, line)) {
      out << "population_curve: HAY307 seed " << seed << ' ' << line << '\n';
    }
  }

 private:
  static core::Grams Of(const core::ResourceAmounts& amounts, std::uint32_t resource) {
    return resource < amounts.size() ? amounts[resource] : 0;
  }

  static void Stage(core::ISimulation& simulation, const core::OrderRow& order) {
    simulation.StageOrders(std::span<const core::OrderRow>(&order, 1), {});
  }

  static bool NoChain(const core::FieldRow& field) {
    return field.rotation_year0.value == core::kInvalidDefIdValue &&
           field.rotation_year1.value == core::kInvalidDefIdValue &&
           field.rotation_year2.value == core::kInvalidDefIdValue;
  }

  /// The derelict ground of the genesis, nearest a built cattle yard or barn first.
  void ReadCandidates(const core::WorldState& world) {
    candidates_read_ = true;
    std::vector<std::pair<double, core::FieldId>> by_distance;
    for (std::uint32_t row = 0; row < world.fields.rows.size(); ++row) {
      const core::FieldRow& field = world.fields.rows[row];
      if (field.kind != core::LandKind::kArable || field.overgrown == 0 || !NoChain(field)) {
        continue;
      }
      double nearest = std::numeric_limits<double>::max();
      for (const core::UnitRow& unit : world.units.rows) {
        if (unit.level > 0 && (unit.type.value == cattle_yard_ || unit.type.value == barn_)) {
          const auto dx = static_cast<double>(unit.position.x - field.center.x);
          const auto dy = static_cast<double>(unit.position.y - field.center.y);
          nearest = std::min(nearest, std::sqrt((dx * dx) + (dy * dy)));
        }
      }
      by_distance.emplace_back(nearest, world.fields.row_ids[row]);
    }
    std::ranges::stable_sort(by_distance, {}, &std::pair<double, core::FieldId>::first);
    for (const auto& [distance, id] : by_distance) {
      candidates_.push_back(id);
    }
  }

  /// The nearest candidate still free: no chain, not the arm's already.
  bool NextFree(const core::WorldState& world, core::FieldId& out) const {
    for (const core::FieldId id : candidates_) {
      if (std::ranges::find(mown_, id) != mown_.end() ||
          std::ranges::find(clovered_, id) != clovered_.end()) {
        continue;
      }
      const std::uint32_t row = core::FindRow(world.fields, id);
      if (row != core::kNoRow && NoChain(world.fields.rows[row])) {
        out = id;
        return true;
      }
    }
    return false;
  }

  /// A+B: the oldest field the arm mows that is not clover yet and still has no chain.
  bool OldestMown(const core::WorldState& world, core::FieldId& out) const {
    for (const core::FieldId id : mown_) {
      if (std::ranges::find(clovered_, id) != clovered_.end()) {
        continue;
      }
      const std::uint32_t row = core::FindRow(world.fields, id);
      if (row != core::kNoRow && NoChain(world.fields.rows[row])) {
        out = id;
        return true;
      }
    }
    return false;
  }

  void CountLamp(const core::ISimulation& simulation) {
    std::vector<core::Alarm> alarms;
    simulation.CollectAlarms(alarms);
    lamp_days_ += std::ranges::any_of(alarms,
                                      [](const core::Alarm& alarm) {
                                        return alarm.kind == core::AlarmKind::kHerdHayShortAhead;
                                      })
                      ? 1U
                      : 0U;
  }

  /// The arm's fields that the plan's base took back (a chain not the arm's).
  std::uint32_t TakenBack(const core::WorldState& world) const {
    std::uint32_t taken = 0;
    for (const core::FieldId id : mown_) {
      const std::uint32_t row = core::FindRow(world.fields, id);
      const bool clover = std::ranges::find(clovered_, id) != clovered_.end();
      taken += row != core::kNoRow && !clover && !NoChain(world.fields.rows[row]) ? 1U : 0U;
    }
    for (const core::FieldId id : clovered_) {
      const std::uint32_t row = core::FindRow(world.fields, id);
      taken += row != core::kNoRow && world.fields.rows[row].rotation_year0.value != clover_ &&
                       world.fields.rows[row].rotation_year1.value != clover_
                   ? 1U
                   : 0U;
    }
    return taken;
  }

  void CloseYear(const core::WorldState& world, std::uint32_t year) {
    const core::YearLedger& book = world.ledger.closed;
    std::uint32_t cows = 0;
    std::uint32_t horses = 0;
    for (const core::HerdRow& herd : world.herds.rows) {
      cows += herd.kind.value == cow_ ? herd.adult_count : 0U;
      horses += herd.kind.value == horse_ ? herd.adult_count : 0U;
    }
    const auto no_horse = static_cast<std::size_t>(core::IdleReason::kNoHorse);
    lines_ << "year " << year << " arm " << static_cast<int>(mode_) << " hay_harvest_kg "
           << Of(book.harvest, hay_) / 1000 << " hay_herd_kg " << Of(book.herd_produce, hay_) / 1000
           << " milk_l " << Of(book.herd_produce, milk_) / 1000 << " rye_harvest_kg "
           << Of(book.harvest, rye_) / 1000 << " cows " << cows << " horses " << horses
           << " lamp_days " << lamp_days_ << " no_horse_idle "
           << (no_horse < book.idle_person_days.size() ? book.idle_person_days[no_horse] : 0U)
           << " residents " << world.residents.rows.size() << " marked " << marked_this_year_
           << " clovered " << clovered_this_year_ << " held_mown " << mown_.size()
           << " held_clover " << clovered_.size() << " taken_back " << TakenBack(world) << '\n';
    lamp_days_ = 0;
    marked_this_year_ = 0;
    clovered_this_year_ = 0;
  }

  Mode mode_ = Mode::kMown;  // the canon's since 0.37.378 (#1923)
  std::uint32_t clover_ = core::kNoTableRow;
  std::uint32_t cattle_yard_ = core::kNoTableRow;
  std::uint32_t barn_ = core::kNoTableRow;
  std::uint32_t hay_ = core::kNoTableRow;
  std::uint32_t milk_ = core::kNoTableRow;
  std::uint32_t rye_ = core::kNoTableRow;
  std::uint32_t cow_ = core::kNoTableRow;
  std::uint32_t horse_ = core::kNoTableRow;
  bool candidates_read_ = false;
  std::vector<core::FieldId> candidates_;
  std::vector<core::FieldId> mown_;
  std::vector<core::FieldId> clovered_;
  std::uint32_t marked_year_ = 0;
  std::uint32_t clovered_year_ = 0;
  std::uint32_t marked_this_year_ = 0;
  std::uint32_t clovered_this_year_ = 0;
  std::uint32_t lamp_days_ = 0;
  core::SimDay closed_day_ = 0;
  std::ostringstream lines_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_HAY_ARM_POLICY_H_
