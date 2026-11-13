/// @file
/// @brief #1681 (econ's option A for «a hungry village beside a full church»: the game names the
///        seed fund, nothing locked; resources.md §6): three year-1 prints on the canon, once a
///        village.
/// @threading SINGLE_THREADED
/// Test-side code, read once a day from the thread that owns the simulation.
///
///   * G1: kilograms of a seed handed out of the stores (issue, ration, advance) while the stores
///     held less of it than the seed fund (SeedRungLeft, fund_ladder.h) - before the spring sowing
///     closes in year 1 (the last spring crop's sow_to_month). Expected 0: the issue reads the
///     rung.
///   * G2: the share of the yards' own cows (household herds) hungry, by day, in year 1's first
///     pasture month (FarmingConfig::pasture_from_month, world: «May»): its largest day and its
///     mean.
///   * G3: family-days with the hunger lamp's red stage (kFamilyGoingHungry, not its forecast) in
///     year 1's months 1-3, and the families lit on the worst day.

#ifndef TESTS_RUN_COMMON_SEED_FUND_TALLY_H_
#define TESTS_RUN_COMMON_SEED_FUND_TALLY_H_

#include <algorithm>
#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "core_catalog/seed_norm_catalog.h"
#include "core_common/alarm_state.h"
#include "core_common/calendar.h"
#include "core_common/fund_ladder.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "core_tables/tables.h"

namespace run {

class SeedFundTally {
 public:
  SeedFundTally(const core::ITableSet& tables, std::uint64_t seed) : seed_(seed) {
    const core::ITable* const crops = tables.FindTable("crops");
    const core::ITable* const resources = tables.FindTable("resources");
    if (crops != nullptr && resources != nullptr) {
      std::string error;
      if (!core::ReadSeedNorms(*crops, resources, seed_norms_, error)) {
        seed_norms_.clear();
      }
      resource_count_ = resources->RowCount();
    }
    for (const core::SeedNorm& norm : seed_norms_) {
      if (norm.sowing_norm_kg_per_ha > 0.0F && !norm.is_winter &&
          norm.sow_to_month != core::kNoSowingMonth) {
        spring_closes_month_ = std::max<std::uint32_t>(spring_closes_month_, norm.sow_to_month);
      }
    }
    if (const core::ITable* const kinds = tables.FindTable("livestock")) {
      cow_ = kinds->FindRowByKey("cow");
    }
    if (const core::ITable* const farming = tables.FindTable("farming")) {
      const std::uint32_t row = farming->FindRowByKey("pasture_from_month");
      const std::uint32_t value = farming->FindColumn("value");
      if (row != core::kNoTableRow && value != core::kNoTableColumn) {
        // farming.csv writes human months 1..12; the calendar's are 0-based.
        const float month = farming->CellReal(row, value).value_or(5.0F);
        pasture_month_ = month >= 1.0F ? static_cast<std::uint32_t>(month) - 1U : 4U;
      }
    }
  }

  void CountDayEnd(const core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    const core::SimDay day = world.calendar.day;
    if (day >= core::kDaysPerYear) {
      return;  // year 1 only
    }
    const auto month = static_cast<std::uint32_t>(world.calendar.date.month);
    // G1: what the stores handed out today below the seed rung.
    const core::YearLedger& book = world.ledger.current;
    if (!seed_norms_.empty() && month <= spring_closes_month_) {
      const core::ResourceAmounts rung = core::SeedRungLeft(world, seed_norms_, resource_count_);
      if (handed_last_.size() < resource_count_) {
        handed_last_.resize(resource_count_, 0);
      }
      for (std::size_t r = 0; r < rung.size(); ++r) {
        const core::Grams handed = Of(book.issued, r) + Of(book.ration, r) + Of(book.advance, r);
        const core::Grams today = handed > handed_last_[r] ? handed - handed_last_[r] : 0;
        handed_last_[r] = handed;
        if (today <= 0 || rung[r] <= 0) {
          continue;
        }
        core::Grams held = 0;
        for (const core::UnitRow& unit : world.units.rows) {
          held += Of(unit.stock, r);
        }
        if (held < rung[r]) {
          g1_grams_ += std::min(today, rung[r] - held);
          ++g1_days_;
        }
      }
    }
    // G2, RE-DEFINED (boss seq 2 item 6): the thin cows «in the yards» are the
    // KOLKHOZ herd quartered there (more heads than roof places), not family
    // cows - y1's yards keep none. So: the kolkhoz's cows, in sheds and
    // quartered alike (household_owned 0), hungry in the first pasture month.
    // The first form read the families' cows and found none in 64 of 64.
    if (month == pasture_month_) {
      std::int64_t cows = 0;
      std::int64_t hungry = 0;
      for (const core::HerdRow& herd : world.herds.rows) {
        if (herd.household_owned != 0 || herd.kind.value != cow_) {
          continue;
        }
        cows += herd.adult_count;
        hungry += herd.fed_days_in_a_row == 0 ? herd.adult_count : 0;
      }
      const double share = cows > 0 ? static_cast<double>(hungry) / static_cast<double>(cows) : 0.0;
      g2_max_ = std::max(g2_max_, share);
      g2_sum_ += share;
      ++g2_days_;
      g2_cows_ = std::max(g2_cows_, cows);
    }
    // G3: the hunger lamp's red stage, family-days, months 1-3 (0-based 0..2).
    if (month <= 2) {
      std::vector<core::Alarm> alarms;
      simulation.CollectAlarms(alarms);
      std::uint32_t lit = 0;
      for (const core::Alarm& alarm : alarms) {
        lit +=
            alarm.kind == core::AlarmKind::kFamilyGoingHungry && alarm.days_to_loss <= 0 ? 1U : 0U;
      }
      g3_family_days_ += lit;
      g3_worst_ = std::max(g3_worst_, lit);
    }
  }

  void Print(std::ostream& out) const {
    out << "population_curve: SEED1681 seed " << seed_ << " g1_seed_kg_handed_below_fund "
        << g1_grams_ / 1000 << " g1_days " << g1_days_ << " spring_closes_month "
        << spring_closes_month_ + 1U << " g2_kolkhoz_cows_hungry_max_pct "
        << static_cast<int>(g2_max_ * 100.0 + 0.5) << " g2_mean_pct "
        << (g2_days_ > 0 ? static_cast<int>(g2_sum_ / g2_days_ * 100.0 + 0.5) : -1)
        << " g2_kolkhoz_cows " << g2_cows_ << " g3_hungry_family_days_m1m3 " << g3_family_days_
        << " g3_worst_day_families " << g3_worst_ << '\n';
  }

 private:
  static core::Grams Of(const core::ResourceAmounts& amounts, std::size_t resource) {
    return resource < amounts.size() ? amounts[resource] : 0;
  }

  std::uint64_t seed_ = 0;
  std::vector<core::SeedNorm> seed_norms_;
  std::size_t resource_count_ = 0;
  std::uint32_t spring_closes_month_ = 0;
  std::uint32_t pasture_month_ = 4;
  std::uint32_t cow_ = core::kNoTableRow;
  core::ResourceAmounts handed_last_;
  core::Grams g1_grams_ = 0;
  std::uint32_t g1_days_ = 0;
  double g2_max_ = 0.0;
  double g2_sum_ = 0.0;
  std::uint32_t g2_days_ = 0;
  std::int64_t g2_cows_ = 0;
  std::uint32_t g3_family_days_ = 0;
  std::uint32_t g3_worst_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_SEED_FUND_TALLY_H_
