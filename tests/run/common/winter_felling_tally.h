/// @file
/// @brief WFELL2072 (boss 24 Nov, after econ's winter-log-stock lever came back null): what
///        becomes of a MARKED stand, by the month. Every day, every stand with a mark and work
///        left: was its felling offered to the hands today - the core's own answer
///        (ISimulation::FellingCanBeManned, the door the accountant's offer uses) - and when not,
///        which of the offer's three questions refused it (home_reach.h, FellingCanBeMannedToday):
///        the log cart past the road limit, no hand reaches it in today's light (HandReachToday
///        kNone), or only a ride would and the door still said no (no horse left for it). Per
///        village-year, Nov-Feb and Mar-Oct apart: the marked stand-days, offered, refused by
///        each reason, the marks that were new and the marks released unreached (marked
///        yesterday with unreached days, unmarked today), the mean walk hours one way of the
///        refused stand-days and the mean daylight of the refused days.
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.
///
/// GROUND, printed: the offer is the door's; the REASON is this tally's own reading of the
/// door's three questions with the rule read off the same tables (transport.csv's pedestrian
/// and horse_trot speeds, labor.csv's travel_limit_hours and min_usable_hours). Where the
/// reading and the door disagree on a stand-day the reading claims as decided (offered on foot,
/// or refused by the cart or by no reach), it is counted as a MISMATCH and printed - the
/// reading is then not the door's and its reasons do not hold.

#ifndef TESTS_RUN_COMMON_WINTER_FELLING_TALLY_H_
#define TESTS_RUN_COMMON_WINTER_FELLING_TALLY_H_

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <ostream>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/home_reach.h"
#include "core_common/road_route.h"
#include "core_common/timber_state.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "core_tables/tables.h"

namespace run {

class WinterFellingTally {
 public:
  explicit WinterFellingTally(const core::ITableSet& tables) {
    const auto real = [&tables](const char* table, const char* key, const char* column) {
      std::optional<float> value;
      if (const core::ITable* const found = tables.FindTable(table)) {
        value = found->CellReal(found->FindRowByKey(key), found->FindColumn(column));
      }
      return value;
    };
    const std::optional<float> walk = real("transport", "pedestrian", "speed_kmh");
    const std::optional<float> ride = real("transport", "horse_trot", "speed_kmh");
    const std::optional<float> limit = real("labor", "travel_limit_hours", "value");
    const std::optional<float> usable = real("labor", "min_usable_hours", "value");
    ready_ = walk && ride && limit && usable;
    if (ready_) {
      rule_ = core::ReachRule{.walk_speed_kmh = *walk,
                              .ride_speed_kmh = *ride,
                              .travel_limit_hours = *limit,
                              .min_usable_hours = *usable};
    }
  }

  /// @brief Once a day, after the day's step.
  void CountDay(const core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    const std::uint32_t year = (world.calendar.day / core::kDaysPerYear) + 1U;
    const std::uint32_t month = (world.calendar.day % core::kDaysPerYear) / core::kDaysPerMonth;
    Half& half = by_year_[year][month >= 10U || month <= 1U ? 0 : 1];
    const std::size_t stands = world.stands.rows.size();
    was_marked_.resize(stands, 0);
    was_unreached_.resize(stands, 0);
    for (std::size_t row = 0; row < stands; ++row) {
      const core::TimberStandRow& stand = world.stands.rows[row];
      const bool marked = stand.marked_m3 > 0.0F && stand.work_days_remaining > 0.0F;
      if (!marked && was_marked_[row] != 0 && was_unreached_[row] != 0) {
        ++half.released;
      }
      half.new_marks += marked && was_marked_[row] == 0 ? 1U : 0U;
      was_marked_[row] = marked ? 1 : 0;
      was_unreached_[row] = stand.unreached_days > 0 ? 1 : 0;
      if (!marked) {
        continue;
      }
      ++half.stand_days;
      const bool offered = simulation.FellingCanBeManned(stand.position);
      if (!ready_) {
        half.offered += offered ? 1U : 0U;
        continue;
      }
      const float cart_hours = core::NearestHomeTravelHours(
          world, stand.position, rule_.ride_speed_kmh, core::TravelMode::kLogCart);
      const bool cart_beyond = cart_hours < 0.0F || cart_hours > rule_.travel_limit_hours;
      const core::HandReach reach = core::HandReachToday(world, stand.position, rule_);
      // The reading's own verdict where it decides alone (the ride's horse it cannot see).
      const bool decided_no = cart_beyond || reach == core::HandReach::kNone;
      const bool decided_yes = !cart_beyond && reach == core::HandReach::kOnFoot;
      if ((decided_no && offered) || (decided_yes && !offered)) {
        ++half.mismatch;
      }
      if (offered) {
        ++half.offered;
        continue;
      }
      if (cart_beyond) {
        ++half.cart_beyond;
      } else if (reach == core::HandReach::kNone) {
        ++half.no_reach;
      } else if (reach == core::HandReach::kByRide) {
        ++half.ride_no_horse;
      }
      const float walk_hours = core::NearestHomeTravelHours(
          world, stand.position, rule_.walk_speed_kmh, core::TravelMode::kWalk);
      half.refused_walk_hours += walk_hours > 0.0F ? walk_hours : 0.0F;
      half.refused_daylight += world.weather.daylight_hours;
    }
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    for (const auto& [year, halves] : by_year_) {
      out << "population_curve: WFELL2072 seed " << seed << " year " << year
          << " ground=marked_stand_days;offer=ISimulation::FellingCanBeManned;reason=home_reach"
             "_reading"
          << (ready_ ? "" : "_NOT_READ(rule_tables_missing)");
      for (std::size_t at = 0; at < halves.size(); ++at) {
        const Half& half = halves[at];
        const std::uint32_t refused = half.stand_days - half.offered;
        out << (at == 0 ? " nov_feb" : " mar_oct") << "(stand_days/offered/cart_beyond/no_reach/"
            << "ride_no_horse/new_marks/released/walk_h_refused/daylight_refused/mismatch) "
            << half.stand_days << '/' << half.offered << '/' << half.cart_beyond << '/'
            << half.no_reach << '/' << half.ride_no_horse << '/' << half.new_marks << '/'
            << half.released << '/'
            << (refused > 0 ? half.refused_walk_hours / static_cast<float>(refused) : 0.0F) << '/'
            << (refused > 0 ? half.refused_daylight / static_cast<float>(refused) : 0.0F) << '/'
            << half.mismatch << (half.mismatch > 0 ? " WFELL_READING_MISMATCH" : "");
      }
      out << '\n';
    }
  }

 private:
  struct Half {
    std::uint32_t stand_days = 0;
    std::uint32_t offered = 0;
    std::uint32_t cart_beyond = 0;
    std::uint32_t no_reach = 0;
    std::uint32_t ride_no_horse = 0;
    std::uint32_t new_marks = 0;
    std::uint32_t released = 0;
    std::uint32_t mismatch = 0;
    float refused_walk_hours = 0.0F;
    float refused_daylight = 0.0F;
  };

  bool ready_ = false;
  core::ReachRule rule_;
  std::vector<char> was_marked_;
  std::vector<char> was_unreached_;
  std::map<std::uint32_t, std::array<Half, 2>> by_year_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_WINTER_FELLING_TALLY_H_
