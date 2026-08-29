// Simulation run: the first labor year over the standard wiring. This is the
// stage-5 criterion (plan §7, restated with boss): the composition of the
// year's work must agree with the reference runs, and the road must cost the
// share of the day the start map says it costs.
//
// The original criterion — "labor load reaches 80-90% with a full job list" —
// sleeps until construction, hauling and odd jobs exist; on fields and the
// barn alone about a quarter of the adults are busy, which is expected, not
// a failure.
//
// What is checked here, against sim_v8_start.py / 49-simulations §2:
//   * man-days by operation over year 1 (plowing, harrowing, sowing, harvest,
//     barn care) against the norms of the tables, each within a band;
//   * the barn is served every day — undone care would be silent otherwise;
//   * the road eats 37-56% of a spring day for the mean and the far field,
//     the numbers the v9 start-map run measured;
//   * worker count does not change any of it (the sub-step is sequential, and
//     that must stay true through the parallel phases around it).

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace {

int Expect(bool condition, const char* label) {
  if (condition) {
    return 0;
  }
  std::cout << "FAIL: " << label << '\n';
  return 1;
}

int ExpectBand(double value, double low, double high, const char* label) {
  if (value >= low && value <= high) {
    return 0;
  }
  std::cout << "FAIL: " << label << " — got " << value << ", wanted " << low << ".." << high
            << '\n';
  return 1;
}

/// What one resident was doing at the previous sample, so that the norm-days
/// he adds between two ticks can be attributed to the right kind. The
/// assignment is settled (and zeroed) the moment his job ends, which is why
/// the delta has to be read every tick and booked to the PREVIOUS kind.
struct LastSeen {
  std::uint32_t kind = 0;
  float worked = 0.0F;
};

/// Man-days delivered per work kind over the run, plus the total hours the
/// village spent away from home.
struct LaborTally {
  std::vector<double> by_kind = std::vector<double>(6, 0.0);

  double hours_away = 0.0;
};

void SampleDay(const core::WorldState& world,
               std::unordered_map<std::uint32_t, LastSeen>& seen,
               LaborTally& tally) {
  for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
    const core::ResidentRow& resident = world.residents.rows[row];
    LastSeen& last = seen[world.residents.row_ids[row].value];
    const float worked = resident.work.worked_norm_days_today;
    if (worked > last.worked) {
      tally.by_kind[last.kind] += static_cast<double>(worked - last.worked);
    }
    last.worked = worked;
    last.kind = static_cast<std::uint32_t>(resident.work.kind);
  }
}

/// Straight-line distance in kilometres.
double DistanceKm(core::Vec2 from, core::Vec2 to) {
  const double dx = static_cast<double>(from.x - to.x) / 1000.0;
  const double dy = static_cast<double>(from.y - to.y) / 1000.0;
  return std::sqrt((dx * dx) + (dy * dy));
}

/// 5 km/h under the x12 chronometer at path factor 1.0: 2.4 game hours per
/// straight-line kilometre — the same arithmetic behind the design's "two
/// hours is about 800 m".
constexpr double kWalkHoursPerKm = 2.4;

constexpr double kSpringDayHours = 13.0;  // sim_v9_startmap.py DAY_H

int CheckTheRoad(const core::WorldState& start) {
  int failures = 0;
  core::Vec2 village{.x = 0.0F, .y = 0.0F};
  std::uint32_t houses = 0;
  for (const core::UnitRow& unit : start.units.rows) {
    if (unit.household.value != core::kInvalidEntityIdValue) {
      village.x += unit.position.x;
      village.y += unit.position.y;
      ++houses;
    }
  }
  failures += Expect(houses == 21, "the start village is 21 households");
  if (houses > 0) {
    village.x /= static_cast<float>(houses);
    village.y /= static_cast<float>(houses);
  }
  double total_km = 0.0;
  double far_km = 0.0;
  std::uint32_t arable = 0;
  for (const core::FieldRow& field : start.fields.rows) {
    const double km = DistanceKm(village, field.center);
    far_km = km > far_km ? km : far_km;
    // The meadows are the standing grass; the arable is everything else.
    if (field.phase != core::FieldPhase::kGrowing) {
      total_km += km;
      ++arable;
    }
  }
  const double mean_km = arable > 0 ? total_km / arable : 0.0;
  const double mean_share = 2.0 * mean_km * kWalkHoursPerKm / kSpringDayHours;
  const double far_share = 2.0 * far_km * kWalkHoursPerKm / kSpringDayHours;
  std::cout << "labor_year: mean arable " << mean_km << " km, farthest " << far_km
            << " km; the road takes " << 100.0 * mean_share << "% and " << 100.0 * far_share
            << "% of a spring day\n";
  failures += ExpectBand(mean_km, 0.9, 1.1, "the arable averages the start map's 1.0 km");
  failures += ExpectBand(far_km, 1.3, 1.5, "nothing lies beyond the start map's 1.5 km");
  failures += ExpectBand(
      mean_share, 0.33, 0.42, "the road to the mean field costs the v9 share of a spring day");
  failures += ExpectBand(
      far_share, 0.50, 0.60, "the road to the far field costs the v9 share of a spring day");
  return failures;
}

}  // namespace

int main() {
  int failures = 0;
  std::string error;
  const auto tables = core::LoadTableSet("tables", &error);
  if (tables == nullptr) {
    std::cout << "FAIL: tables/ did not load (" << error << ") — run from the repo root\n";
    return 1;
  }

  core::StandardSimulationConfig config;
  config.tables = tables.get();
  config.world_seed = 1930;
  config.worker_count = 1;
  const auto simulation = core::CreateStandardSimulation(config);
  if (simulation == nullptr) {
    std::cout << "FAIL: the simulation did not assemble\n";
    return 1;
  }

  failures += CheckTheRoad(simulation->CompletedState());

  // --- one year of work ----------------------------------------------------
  std::unordered_map<std::uint32_t, LastSeen> seen;
  LaborTally tally;
  double care_left = 0.0;
  double accounts_before_burn = 0.0;
  for (std::uint32_t tick = 0; tick < core::kTicksPerYear; ++tick) {
    simulation->AdvanceStep();
    const core::WorldState& world = simulation->CompletedState();
    SampleDay(world, seen, tally);
    // Hour 22, not 23: the day's close clears the assignments, and with them
    // the hours the family spent out.
    if (core::HourFromTick(world.calendar.tick) == 22) {
      for (const core::ResidentRow& resident : world.residents.rows) {
        tally.hours_away += static_cast<double>(resident.work.hours_away_today);
      }
      for (const core::HerdRow& herd : world.herds.rows) {
        care_left += static_cast<double>(herd.care_days_remaining);
      }
      if (world.calendar.day + 1 == core::kDaysPerYear) {
        for (const core::FamilyRow& family : world.families.rows) {
          accounts_before_burn += static_cast<double>(family.trudodni_account) /
                                  static_cast<double>(core::kTrudodniScale);
        }
      }
    }
  }
  const core::WorldState& state = simulation->CompletedState();

  const double plowing = tally.by_kind[1];
  const double harrowing = tally.by_kind[2];
  const double sowing = tally.by_kind[3];
  const double harvest = tally.by_kind[4];
  const double care = tally.by_kind[5];
  const double total = plowing + harrowing + sowing + harvest + care;
  std::cout << "labor_year: game man-days — plowing " << plowing << ", harrowing " << harrowing
            << ", sowing " << sowing << ", harvest " << harvest << ", barn " << care << ", total "
            << total << "; " << tally.hours_away << " hours away from home\n";

  // The first-year plan sows 70 ha (49-simulations §2): plowing and harrowing
  // are one norm for any land — 70 x 10/7 and 70 x 3/7 game man-days.
  failures += ExpectBand(plowing, 90.0, 105.0, "plowing costs the 70 sown hectares' norm");
  failures += ExpectBand(harrowing, 27.0, 32.0, "harrowing costs its norm on the same land");
  // Sowing is per crop: grain 3, potato 12, flax 2, fodder 2 real man-days/ha.
  failures += ExpectBand(sowing, 40.0, 48.0, "sowing costs the crop mix's norm");
  // Harvest is the heavy half: grain 8, potato 25, flax 60, hay 8 per hectare.
  // A field lost to snow takes its own harvest with it, so the band is wide
  // downward.
  failures += ExpectBand(harvest, 200.0, 245.0, "the harvest of the mix plus 80 ha of hay");
  // 39 cows at 32 real man-days a head a year.
  failures += ExpectBand(care, 170.0, 180.0, "the barn costs the cow herd's yearly norm");
  failures += ExpectBand(total, 540.0, 620.0, "the year's labor matches the reference run");
  std::cout << "labor_year: " << care_left << " game man-days of barn care left undone\n";
  failures += ExpectBand(care_left, 0.0, 2.0, "the barn is served, day in and day out");

  // Trudodni are the same quantity seen from the pay side: the rate is 1.0
  // across Epoch-I hand work, so the accounts on the last evening of the year
  // must hold what the year delivered — and nothing after the turn burns them.
  double after_burn = 0.0;
  for (const core::FamilyRow& family : state.families.rows) {
    after_burn +=
        static_cast<double>(family.trudodni_account) / static_cast<double>(core::kTrudodniScale);
  }
  std::cout << "labor_year: " << accounts_before_burn << " trudodni on the accounts the last "
            << "evening, " << after_burn << " after the year's turn\n";
  failures += ExpectBand(accounts_before_burn,
                         total * 0.95,
                         total * 1.02,
                         "the family accounts hold the year's delivered man-days");
  failures += Expect(after_burn == 0.0, "the economic year's turn burns what was not spent");

  // --- the same year with three workers ------------------------------------
  core::StandardSimulationConfig parallel_config = config;
  parallel_config.worker_count = 3;
  const auto parallel = core::CreateStandardSimulation(parallel_config);
  if (parallel == nullptr) {
    std::cout << "FAIL: the parallel simulation did not assemble\n";
    return failures + 1;
  }
  std::unordered_map<std::uint32_t, LastSeen> parallel_seen;
  LaborTally parallel_tally;
  for (std::uint32_t tick = 0; tick < core::kTicksPerYear; ++tick) {
    parallel->AdvanceStep();
    SampleDay(parallel->CompletedState(), parallel_seen, parallel_tally);
  }
  bool same = true;
  for (std::uint32_t kind = 0; kind < tally.by_kind.size(); ++kind) {
    same = same && tally.by_kind[kind] == parallel_tally.by_kind[kind];
  }
  failures += Expect(same, "1 worker and 3 workers deliver the same man-days, bit for bit");
  const core::WorldState& many = parallel->CompletedState();
  bool people_same = many.residents.rows.size() == state.residents.rows.size();
  for (std::uint32_t row = 0; row < many.residents.rows.size() && people_same; ++row) {
    people_same = many.residents.rows[row].rest == state.residents.rows[row].rest &&
                  many.residents.rows[row].health == state.residents.rows[row].health;
  }
  failures += Expect(people_same, "and leave every worker in the same state");

  if (failures == 0) {
    std::cout << "labor_year: all checks passed\n";
  }
  return failures;
}
