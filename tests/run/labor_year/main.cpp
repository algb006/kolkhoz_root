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
//     barn care) against the norms of the tables, each within a band. The
//     hay share is stage 6's: the meadows are the canon's 15% of the map,
//     not the reference runs' "half the arable", and mowing them fills a
//     summer that used to stand idle;
//   * the barn is served every day — undone care would be silent otherwise;
//   * the road costs what the twelve-kilometre layout says it costs: the
//     arable lies inside the walking leg and the meadows inside the harness
//     leg (the land-placement rule of 1 September 2026), and the share of a
//     spring day each takes follows from that;
//   * worker count does not change any of it (the sub-step is sequential, and
//     that must stay true through the parallel phases around it).

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "../common/run_harness.h"
#include "core_common/calendar.h"
#include "core_common/land_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace {

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

/// 12 km/h in harness under the same chronometer: one game hour per
/// straight-line kilometre. Hay is carted, not carried (labor model §4).
constexpr double kHarnessHoursPerKm = 1.0;

/// The two legs of the land-placement rule (decision of 1 September 2026,
/// measured the same way by `tools/db.py check`): the arable is reached on
/// foot within 1.5 km of the village, the meadows in harness within 3.3 km.
/// The 200 ha of hay do not fit inside the walking ring on this map — the
/// ring holds 394 ha of dry usable land and the arable takes 160 of it — so
/// the walking anchor on hay was a surplus requirement, not canon.
constexpr double kArableLegKm = 1.5;

constexpr double kMeadowLegKm = 3.3;

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
  failures += run::Expect(houses == 21, "the start village is 21 households");
  if (houses > 0) {
    village.x /= static_cast<float>(houses);
    village.y /= static_cast<float>(houses);
  }
  // The arable is walked to, the meadows are ridden to: two legs, two
  // speeds, measured apart (the land-placement rule; land_state.h LandKind).
  double total_km = 0.0;
  double far_arable_km = 0.0;
  double far_meadow_km = 0.0;
  std::uint32_t arable = 0;
  for (const core::FieldRow& field : start.fields.rows) {
    const double km = DistanceKm(village, field.center);
    const bool meadow =
        field.kind == core::LandKind::kMeadow || field.kind == core::LandKind::kFloodplainMeadow;
    if (meadow) {
      far_meadow_km = km > far_meadow_km ? km : far_meadow_km;
      continue;
    }
    far_arable_km = km > far_arable_km ? km : far_arable_km;
    total_km += km;
    ++arable;
  }
  const double mean_km = arable > 0 ? total_km / arable : 0.0;
  const double mean_share = 2.0 * mean_km * kWalkHoursPerKm / kSpringDayHours;
  const double far_arable_share = 2.0 * far_arable_km * kWalkHoursPerKm / kSpringDayHours;
  const double far_meadow_share = 2.0 * far_meadow_km * kHarnessHoursPerKm / kSpringDayHours;
  std::cout << "labor_year: mean arable " << mean_km << " km, farthest arable " << far_arable_km
            << " km on foot, farthest meadow " << far_meadow_km << " km in harness; the road takes "
            << 100.0 * mean_share << "%, " << 100.0 * far_arable_share << "% and "
            << 100.0 * far_meadow_share << "% of a spring day\n";
  // The mean has no canon number of its own: the twelve-kilometre layout
  // packs the arable by mask inside the walking leg and measures 0.81 km,
  // nearer than the ten-kilometre map's 1.0. The band says "under a
  // kilometre and not on the doorstep"; the legs are the rule itself.
  failures += ExpectBand(mean_km, 0.6, 1.1, "the arable averages under a kilometre");
  failures += ExpectBand(
      far_arable_km, 0.5, kArableLegKm, "every arable contour lies inside the walking leg");
  failures +=
      ExpectBand(far_meadow_km, 0.5, kMeadowLegKm, "every meadow lies inside the harness leg");
  failures += ExpectBand(
      mean_share, 0.22, 0.41, "the road to the mean field costs its share of a spring day");
  failures += ExpectBand(far_arable_share,
                         0.1,
                         2.0 * kArableLegKm * kWalkHoursPerKm / kSpringDayHours,
                         "the road to the far field costs at most the walking leg's share");
  failures += ExpectBand(far_meadow_share,
                         0.1,
                         2.0 * kMeadowLegKm * kHarnessHoursPerKm / kSpringDayHours,
                         "the road to the far meadow costs at most the harness leg's share");
  return failures;
}

}  // namespace

int main() {
  int failures = 0;
  const run::Simulation started = run::Start(1930);
  if (!started) {
    return 1;
  }
  core::ISimulation* simulation = started.simulation.get();

  failures += CheckTheRoad(simulation->CompletedState());

  // --- one year of work ----------------------------------------------------
  std::unordered_map<std::uint32_t, LastSeen> seen;
  LaborTally tally;
  double care_left = 0.0;
  double accounts_before_burn = 0.0;
  double accrued_before_burn = 0.0;
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
        accrued_before_burn = static_cast<double>(world.ledger.current.trudodni_accrued) /
                              static_cast<double>(core::kTrudodniScale);
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

  // The first year ploughs 73.5 ha: 66.5 sown in spring, the 3.5 ha of
  // rotation fallow (fallow is ploughed, farming design §7), and the same
  // 3.5 again in the autumn for the winter rye that follows the fallow
  // (start canon §8 — "winter rye goes in the autumn of the same year").
  // The rye after grass waits for the second year: a stand sown in spring
  // is not cut until next June. Plowing and harrowing are one norm for any
  // land: 73.5 x 10/7 and 73.5 x 3/7. The derelict ninety hectares get
  // nothing: they are not fallow.
  failures += ExpectBand(plowing, 100.0, 110.0, "plowing costs the raised land's norm");
  failures += ExpectBand(harrowing, 30.0, 33.0, "harrowing costs its norm on the same land");
  // Sowing is per crop, and the mix is the start canon's suggested rotation
  // (start canon §8, in the core since task O2b): potatoes 21 ha x 12 real
  // man-days, wheat 10 x 3, barley 7.5 x 3, oats 10.5 x 3, grasses 10.5 x 2,
  // cabbage 7 x 5 — 392 real man-days, 56 game ones. It costs more than the
  // old genesis mix because the canon sows three times the potatoes, and
  // potatoes are the crop that takes hands.
  // ...plus the autumn's 3.5 ha of winter rye at 3 real man-days.
  failures += ExpectBand(sowing, 55.0, 61.0, "sowing costs the crop mix's norm");
  // Harvest is the heavy half: grain 8, potato 25, flax 60, hay 8 per hectare.
  // A field lost to snow takes its own harvest with it, so the band is wide
  // downward.
  //
  // The hay share doubled at stage 6, and not because anything broke. The
  // reference runs mowed 80 ha because they took the meadow area as half the
  // arable; the design does no such thing — the map gives about 15% of its
  // hundred square kilometres to grass (terrain design §1), and the herd
  // needs some 190 ha mown to be fed at all. Genesis now lays out 200 ha, so
  // haymaking takes the summer the reference run left idle. That is the
  // design's own reading of it: hay is a decision, not a given.
  failures += ExpectBand(harvest, 340.0, 400.0, "the harvest of the mix plus 200 ha of hay");
  // 39 cows at 32 real man-days a head a year.
  // 39 cows at 32 real man-days a year is 178 game man-days — the norm the
  // barn WOULD cost a herd that never changed. Since stage 6 the herd is
  // alive: it eats, it calves, it loses heads to age and to a hungry winter,
  // and the year's care follows the heads that actually stood there. The
  // band's floor is therefore the honest one, not the arithmetic one.
  failures += ExpectBand(care, 150.0, 180.0, "the barn costs the cow herd's yearly norm");
  failures += ExpectBand(
      total, 650.0, 760.0, "the year's labor matches the reference run plus the meadows");
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
  std::cout << "labor_year: " << accrued_before_burn << " trudodni accrued over the year, "
            << accounts_before_burn << " still on the accounts the last evening, " << after_burn
            << " after the year's turn\n";
  // THE LEDGER IS THE WITNESS, not the sum of the accounts. A household's
  // account is a GROSS counter of what that household ever earned this year,
  // and a household can dissolve: its people marry out, its last member dies.
  // What it is owed passes to the neighbours (residents_system.cpp), but the
  // history of what it once earned goes with the row, so the accounts add up
  // to a little less than the year delivered — by a tenth in the first year,
  // which is a village of young households marrying.
  //
  // The ledger's own accrual counter has no rows to lose, which is exactly
  // why the reconciliation is done on it.
  failures += ExpectBand(accrued_before_burn,
                         total * 0.98,
                         total * 1.02,
                         "the ledger's accrual holds the year's delivered man-days");
  failures += run::Expect(accounts_before_burn > total * 0.85,
                          "and the accounts hold all of it but the dissolved households' history");
  failures += run::Expect(after_burn == 0.0, "the economic year's turn burns what was not spent");

  // --- the same year with three workers ------------------------------------
  const run::Simulation parallel = run::Start(1930, 3);
  if (!parallel) {
    return failures + 1;
  }
  std::unordered_map<std::uint32_t, LastSeen> parallel_seen;
  LaborTally parallel_tally;
  for (std::uint32_t tick = 0; tick < core::kTicksPerYear; ++tick) {
    parallel->AdvanceStep();
    SampleDay(parallel.State(), parallel_seen, parallel_tally);
  }
  bool same = true;
  for (std::uint32_t kind = 0; kind < tally.by_kind.size(); ++kind) {
    same = same && tally.by_kind[kind] == parallel_tally.by_kind[kind];
  }
  failures += run::Expect(same, "1 worker and 3 workers deliver the same man-days, bit for bit");
  const core::WorldState& many = parallel->CompletedState();
  bool people_same = many.residents.rows.size() == state.residents.rows.size();
  for (std::uint32_t row = 0; row < many.residents.rows.size() && people_same; ++row) {
    people_same = many.residents.rows[row].rest == state.residents.rows[row].rest &&
                  many.residents.rows[row].health == state.residents.rows[row].health;
  }
  failures += run::Expect(people_same, "and leave every worker in the same state");

  if (failures == 0) {
    std::cout << "labor_year: all checks passed\n";
  }
  return failures;
}
