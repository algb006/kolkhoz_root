/// @file
/// @brief The run chairman's seed play for the fallow blocks (#725 (f); econ,
///        fields-grow-with-eaters «24 Oct ~03:00»; boss, core-boss-725-fallow
///        [40], [42]): in January-March, a crop whose projected stock falls
///        under 1.10 x (due + next year's seed) is bargained in the district
///        through the existing plan-trade door.
/// @threading SINGLE_THREADED
/// Run code: one chairman, called from the run's day loop.
///
/// TWO ENTRIES, BECAUSE THE DOOR HAS TWO HOURS. The plan trade is taken only
/// while the chairman is in the district (district_trip.cpp OrderTradePlan,
/// 8:00-20:00 of his trip day), and the run's day loop calls the chairman
/// once, after the day's last tick. So the day entry decides and books the
/// trip; the district-hour entry, called by the run at an hour he is away,
/// stages the trade it decided and nothing else - a whole second day of the
/// chairman would give every other away-hour order (a limit lot) a door it
/// does not have in the canon.

#ifndef RUN_COMMON_FALLOW_SEED_PLAY_H_
#define RUN_COMMON_FALLOW_SEED_PLAY_H_

#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/chairman_away.h"
#include "core_common/ids.h"
#include "core_common/order_state.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"

namespace run {

/// @brief One plan position as the seed play reads it, grams.
struct SeedPlayPosition {
  core::ResourceId resource;
  std::string key;
  double projected_g = 0.0;  ///< Stock now - this spring's seed + the harvest - last year's use.
  double need_g = 0.0;  ///< The due + next year's seed of every field with the crop in its slot.
  core::CropId crop;    ///< A crop yielding this resource: the swap's target when it is the cover.
};

/// @brief Decides, books the trip, and bargains in the district.
class SeedPlay {
 public:
  /// STUB 1.10 (econ, «24 Oct ~03:00»): the margin over due + seed.
  static constexpr double kMargin = 1.10;

  /// @brief The day entry: in January-March of a year with a block open,
  ///        with no trade made this year, picks the worst short position and
  ///        books the trip. Prints what it read and what it chose.
  /// @param positions This year's plan positions with their book (FallowPolicy).
  void RunDay(core::ISimulation& simulation,
              const core::WorldState& world,
              bool block_open,
              const std::vector<SeedPlayPosition>& positions) {
    const std::uint32_t day_of_year = world.calendar.day % core::kDaysPerYear;
    const std::uint32_t year = world.calendar.day / core::kDaysPerYear + 1U;
    if (pending_.kind == core::OrderKind::kTradePlan && taken_year_ != year &&
        world.chairman.plan_traded_year == world.calendar.date.year) {
      taken_year_ = year;
      ++trades_;
      rep_spent_ += rep_before_ - world.chairman.raikom_reputation;
      std::cout << "fallow_policy: year " << year << " seed play TRADED " << pending_key_ << " ("
                << (pending_.rotation_year0.value != core::kInvalidDefIdValue ? "swap" : "-10 %")
                << "), reputation " << rep_before_ << " -> " << world.chairman.raikom_reputation
                << '\n';
      pending_ = core::OrderRow{};
    }
    if (day_of_year == core::kDaysPerYear - 1U) {
      pending_ = core::OrderRow{};  // a trade not made by the year's end is dropped
    }
    const std::uint32_t month = day_of_year / core::kDaysPerMonth;
    if (!block_open || month > 2 || decided_year_ == year || world.plan.announced == 0 ||
        world.chairman.plan_traded_year == world.calendar.date.year) {
      return;
    }
    decided_year_ = year;
    const SeedPlayPosition* worst = nullptr;
    double worst_gap = 0.0;
    std::cout << "fallow_policy: year " << year << " seed play (t, projected / 1.10 x need):";
    for (const SeedPlayPosition& position : positions) {
      std::cout << ' ' << position.key << ' ' << position.projected_g / 1e6 << '/'
                << kMargin * position.need_g / 1e6;
      const double gap = (kMargin * position.need_g) - position.projected_g;
      if (gap > worst_gap) {
        worst_gap = gap;
        worst = &position;
      }
    }
    if (worst == nullptr) {
      std::cout << " - none short\n";
      return;
    }
    core::OrderRow trade;
    trade.kind = core::OrderKind::kTradePlan;
    trade.resource = worst->resource;
    trade.amount = -1;
    const SeedPlayPosition* cover = nullptr;
    for (const SeedPlayPosition& position : positions) {
      const double surplus = position.projected_g - (kMargin * position.need_g);
      if (&position != worst && position.crop.value != core::kInvalidDefIdValue &&
          surplus >= worst_gap &&
          (cover == nullptr || surplus > cover->projected_g - (kMargin * cover->need_g))) {
        cover = &position;
      }
    }
    if (cover != nullptr) {
      trade.rotation_year0 = cover->crop;
      trade.amount = 0;
    }
    std::cout << " - " << worst->key << " short by " << worst_gap / 1e6
              << " t: " << (cover != nullptr ? "swap to " + cover->key : std::string("cut 10 %"))
              << '\n';
    pending_ = trade;
    pending_key_ = worst->key;
    rep_before_ = world.chairman.raikom_reputation;
    traded_years_.push_back(year);
    traded_keys_.push_back(worst->key);
    core::OrderRow trip;
    trip.kind = core::OrderKind::kTripToDistrict;
    simulation.StageOrders(std::span<const core::OrderRow>(&trip, 1), {});
  }

  /// @brief The district-hour entry: stages the decided trade, and nothing
  ///        else, while the chairman is in the district. The run calls it at
  ///        an hour between the trip's departure and return; on any other
  ///        hour or day it does nothing.
  void RunDistrictHour(core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    if (pending_.kind != core::OrderKind::kTradePlan || !core::ChairmanAway(world) ||
        staged_day_ == world.calendar.day) {
      return;
    }
    staged_day_ = world.calendar.day;
    simulation.StageOrders(std::span<const core::OrderRow>(&pending_, 1), {});
  }

  /// @brief The pair's prints: trades, the reputation they spent, by year.
  void Report() const {
    std::cout << "fallow_policy: seed play trades " << trades_ << ", reputation spent "
              << rep_spent_ << ", decided in years:";
    for (std::size_t i = 0; i < traded_years_.size(); ++i) {
      std::cout << ' ' << traded_years_[i] << ':' << traded_keys_[i];
    }
    std::cout << (traded_years_.empty() ? " none\n" : "\n");
  }

 private:
  core::OrderRow pending_;
  std::string pending_key_;
  float rep_before_ = 0.0F;
  std::uint32_t decided_year_ = 0;
  std::uint32_t taken_year_ = 0;
  core::SimDay staged_day_ = static_cast<core::SimDay>(-1);
  std::uint32_t trades_ = 0;
  float rep_spent_ = 0.0F;
  std::vector<std::uint32_t> traded_years_;
  std::vector<std::string> traded_keys_;
};

}  // namespace run

#endif  // RUN_COMMON_FALLOW_SEED_PLAY_H_
