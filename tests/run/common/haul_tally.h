/// @file
/// @brief How long a reaped load lay in its field, and on what, day by day:
/// the instrument for "why does the harvest not come in" (boss, task 4 b).
/// @threading SINGLE_THREADED
/// Test-side instrument, read from the thread that owns the simulation.
///
/// WHY (boss, parcels 405 and 421). With a clamp by the fields the harvest
/// has a home from the first day, and still 97 t of potatoes and vegetables
/// went to the snow on seed 1936 in four years. What is left is carrying.
/// Every tonne lying out at a day's end is booked to one reason: the stores
/// could not take it (no demand was written), it was a day off, somebody
/// carried part of it, or a demand stood and nobody came — split by whether
/// hands stood idle at noon. And every load is followed to its end: carried
/// off, or written off by the snow.

#ifndef TESTS_RUN_COMMON_HAUL_TALLY_H_
#define TESTS_RUN_COMMON_HAUL_TALLY_H_

#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/labor_state.h"
#include "core_common/land_state.h"
#include "core_common/ledger_state.h"
#include "core_common/world_state.h"
#include "core_world/world.h"

namespace run {

class HaulTally {
 public:
  /// @brief The noon reading: how many hands stood about and how many were
  /// hauling. Call once a day at the working day's middle.
  void CountNoon(const core::ISimulation& simulation) {
    const core::WorkforceCount force = simulation.Workforce();
    idle_at_noon_ = force.idle;
    haulers_at_noon_ = 0;
    for (const core::ResidentRow& resident : simulation.CompletedState().residents.rows) {
      haulers_at_noon_ += resident.work.kind == core::WorkKind::kHauling ? 1U : 0U;
    }
  }

  /// @brief The day's end: every load against yesterday's. Call once a day
  /// after the day's last step and before any policy stages an order.
  void CountDay(const core::WorldState& world) {
    Year& year = years_.empty() ? years_.emplace_back() : years_.back();
    year.hauler_days += haulers_at_noon_;
    if (lying_.size() < world.fields.rows.size()) {
      lying_.resize(world.fields.rows.size());
    }
    // THE DAY JUST LIVED, NOT THE CALENDAR'S. After a day's last step the
    // calendar already names the next day, and a first draft of this tally
    // read the day off from it: every Sunday's idle hands were booked to the
    // Monday, and the Sundays showed up as "idle hands not sent".
    const core::SimDay lived = world.calendar.day > 0 ? world.calendar.day - 1 : 0;
    const bool day_off = core::IsRestDay(lived, world.calendar.day_zero_weekday, world.epoch);
    core::Grams snowed_today = 0;
    for (std::size_t row = 0; row < world.fields.rows.size(); ++row) {
      const core::FieldRow& field = world.fields.rows[row];
      Load& load = lying_[row];
      const core::Grams before = load.grams;
      const core::Grams now = field.reaped_grams;
      if (before > 0) {
        Reason reason = Reason::kNoHands;
        if (now < before && now == 0 && Snowed(world, load)) {
          reason = Reason::kSnow;
        } else if (now < before) {
          reason = Reason::kCarried;
        } else if (!(load.demand_days > 0.0F)) {
          reason = Reason::kNoRoom;
        } else if (day_off) {
          reason = Reason::kDayOff;
        } else if (idle_at_noon_ > 0) {
          reason = Reason::kIdleHandsNotSent;
        }
        year.tonne_days[static_cast<std::size_t>(reason)] += static_cast<double>(before) / 1.0e6;
        if (now == 0) {
          const std::uint32_t lay = lived - load.first_day;
          if (reason == Reason::kSnow) {
            ++year.loads_snowed;
            year.snowed_tonnes += static_cast<double>(before) / 1.0e6;
            snow_lines_.push_back(SnowLine{.day = lived,
                                           .reaped_day = load.first_day,
                                           .sown_day = field.sown_day,
                                           .tonnes = static_cast<double>(before) / 1.0e6,
                                           .demand_days = load.demand_days,
                                           .resource = load.resource.value});
            snowed_today += before;
          } else {
            ++year.loads_cleared;
            year.days_to_clear += lay;
            year.longest_clear = lay > year.longest_clear ? lay : year.longest_clear;
          }
        }
      }
      if (before <= 0 && now > 0) {
        load.first_day = lived;
      }
      load.grams = now;
      load.resource = field.reaped_resource;
      load.demand_days = field.haul_days_remaining;
    }
    // EVERY WRITE-OFF OF THE DAY, not only the snow on a lying load: a crop
    // can be reaped and lost the same day, a store can come down, a pantry
    // can go with its last household — none of them a load this tally sees.
    const core::ResourceAmounts& lost = world.ledger.current.lost_no_room;
    core::Grams written_today = 0;
    for (std::size_t index = 0; index < lost.size(); ++index) {
      const core::Grams yesterday =
          world.ledger.current.year == ledger_year_ && index < lost_yesterday_.size()
              ? lost_yesterday_[index]
              : 0;
      written_today += lost[index] - yesterday;
      if (lost[index] - yesterday > kOtherLineGrams) {
        other_resource_ = static_cast<std::uint16_t>(index);
      }
    }
    year.written_off_tonnes += static_cast<double>(written_today) / 1.0e6;
    if (written_today - snowed_today > kOtherLineGrams) {
      other_lines_.push_back(
          OtherLine{.day = lived,
                    .tonnes = static_cast<double>(written_today - snowed_today) / 1.0e6,
                    .resource = other_resource_});
    }
    lost_yesterday_ = world.ledger.current.lost_no_room;
    ledger_year_ = world.ledger.current.year;
  }

  /// @brief Opens the next year's row. Call at each year's end.
  void CloseYear() { years_.emplace_back(); }

  void Report(std::string_view run_name, std::uint32_t last_year) const {
    for (std::uint32_t index = 0; index < years_.size() && index < last_year; ++index) {
      const Year& year = years_[index];
      std::cout << run_name << ": HAUL year " << index + 1 << " — load t-days";
      for (std::size_t reason = 0; reason < kReasonCount; ++reason) {
        std::cout << ' ' << kReasonNames[reason] << ' '
                  << static_cast<std::int64_t>(year.tonne_days[reason]);
      }
      std::cout << "; loads cleared " << year.loads_cleared << " (mean "
                << (year.loads_cleared > 0 ? year.days_to_clear / year.loads_cleared : 0)
                << " days, longest " << year.longest_clear << "), snowed " << year.loads_snowed
                << " (" << static_cast<std::int64_t>(year.snowed_tonnes) << " t); all written off "
                << static_cast<std::int64_t>(year.written_off_tonnes) << " t; hauler-days "
                << year.hauler_days << "\n";
    }
    for (const SnowLine& line : snow_lines_) {
      if (line.day / core::kDaysPerYear >= last_year) {
        break;
      }
      std::cout << run_name << ": HAUL snow took " << line.tonnes << " t of resource "
                << line.resource << " on day " << line.day << ", sown on day " << line.sown_day
                << ", reaped on day " << line.reaped_day
                << ", its carrying demand the evening before " << line.demand_days << " man-days\n";
    }
    for (const OtherLine& line : other_lines_) {
      if (line.day / core::kDaysPerYear >= last_year) {
        break;
      }
      std::cout << run_name << ": HAUL written off NOT from a lying load: " << line.tonnes
                << " t (resource " << line.resource << ") on day " << line.day << "\n";
    }
  }

 private:
  enum class Reason : std::uint8_t {
    kCarried,
    kNoRoom,
    kDayOff,
    kIdleHandsNotSent,
    kNoHands,
    kSnow,
  };
  static constexpr std::size_t kReasonCount = 6;
  static constexpr std::array<const char*, kReasonCount> kReasonNames = {
      "carried", "no-room", "day-off", "idle-hands-not-sent", "no-free-hands", "snowed"};

  struct Load {
    core::Grams grams = 0;
    core::ResourceId resource;
    float demand_days = 0.0F;
    core::SimDay first_day = 0;
  };

  struct Year {
    std::array<double, kReasonCount> tonne_days{};
    std::uint32_t loads_cleared = 0;
    std::uint32_t loads_snowed = 0;
    std::uint32_t days_to_clear = 0;
    std::uint32_t longest_clear = 0;
    double snowed_tonnes = 0.0;
    double written_off_tonnes = 0.0;
    std::uint32_t hauler_days = 0;
  };

  /// A load that went to zero on a day the book wrote its whole weight off.
  bool Snowed(const core::WorldState& world, const Load& load) const {
    const std::uint16_t index = load.resource.value;
    const core::ResourceAmounts& lost = world.ledger.current.lost_no_room;
    const core::Grams today = index < lost.size() ? lost[index] : 0;
    const core::Grams yesterday =
        world.ledger.current.year == ledger_year_ && index < lost_yesterday_.size()
            ? lost_yesterday_[index]
            : 0;
    return today - yesterday >= load.grams;
  }

  struct SnowLine {
    core::SimDay day = 0;
    core::SimDay reaped_day = 0;
    core::SimDay sown_day = 0;
    double tonnes = 0.0;
    float demand_days = 0.0F;
    std::uint16_t resource = 0;
  };

  struct OtherLine {
    core::SimDay day = 0;
    double tonnes = 0.0;
    std::uint16_t resource = 0;
  };

  /// A tonne: below it a day's write-off is straw and hay, not a harvest.
  static constexpr core::Grams kOtherLineGrams = 1'000'000;

  std::vector<Year> years_;
  std::vector<SnowLine> snow_lines_;
  std::vector<OtherLine> other_lines_;
  std::uint16_t other_resource_ = 0;
  std::vector<Load> lying_;
  core::ResourceAmounts lost_yesterday_;
  std::uint16_t ledger_year_ = 0;
  std::uint32_t idle_at_noon_ = 0;
  std::uint32_t haulers_at_noon_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_HAUL_TALLY_H_
