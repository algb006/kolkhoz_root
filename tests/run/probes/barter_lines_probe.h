/// @file
/// @brief Who would give what at the counter: the dry count's own working by
/// yard (ISimulation::BarterDryLines, 0.37.80), printed for the question the
/// totals of barter_probe.h could not answer — the print of 0.37.78 found
/// the village's givers and takers unmoved by unlike yards and nothing to
/// explain it by (core-boss-yards-holdings-03778-2026-10-01).
/// @threading SINGLE_THREADED
/// Test-side code: a probe a run calls after every AdvanceStep on the thread
/// that owns the simulation. It asks the door and writes lines to stdout; it
/// changes nothing.
///
/// On every kLinesEveryDays-th day, at the first step seen from hour
/// kLinesHour on (the counter's hour: after the day's milk and catch, before
/// the dinner), for the first kLinesDays days:
///   YQ,day=,resource=<index>,yards_offer=,yards_claim=,offered_g=,claimed_g=,
///      give_g=,take_g=            one line a resource anything is said of
///   YG,day=,family=<row>,eaters=,hens=<adult household hens>,goats=,
///      night_trade=<0 none, 1 the distiller's yard, ...>,drinkers=<its men
///      over 20 of alcoholism — they pay a distiller in grain>,
///      gives=<resource index:grams;... or "-">,takes=<...>,
///      offers=<...>,claims=<...>  one line a yard that would hand over or
///                                 carry home anything
/// Every amount is grams of the GRAIN EQUIVALENT, the exchange's measure.
/// A day with no line at all prints
///   YQ,day=,resource=-,yards_offer=0,...
/// so that an empty count is said, not left out.
///
/// HOW A RUN OUTSIDE THE TREE HOOKS IT (host's plan700, on the Xeon): the
/// header copied beside the run's source, included after its last include,
/// and `barter_lines_probe::Step(*world.simulation, world.State());` after
/// each AdvanceStep. env HEN_KIND and GOAT_KIND: the livestock.csv rows
/// (defaults 4 and 7).

#ifndef TESTS_RUN_PROBES_BARTER_LINES_PROBE_H_
#define TESTS_RUN_PROBES_BARTER_LINES_PROBE_H_

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "core_common/barter_view.h"
#include "core_common/calendar.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"

namespace barter_lines_probe {

inline constexpr std::int64_t kLinesDays = 96;
inline constexpr std::int64_t kLinesEveryDays = 4;
inline constexpr std::int64_t kLinesHour = 19;

inline unsigned KindFromEnv(const char* name, unsigned fallback) {
  const char* text = std::getenv(name);
  return text == nullptr ? fallback : static_cast<unsigned>(std::atoi(text));
}

inline void Step(const core::ISimulation& simulation, const core::WorldState& world) {
  static std::int64_t printed_day = -1;
  static const unsigned hen_kind = KindFromEnv("HEN_KIND", 4);
  static const unsigned goat_kind = KindFromEnv("GOAT_KIND", 7);
  const auto day = static_cast<std::int64_t>(world.calendar.day);
  const auto hour = static_cast<std::int64_t>(world.calendar.tick % core::kTicksPerDay);
  if (day >= kLinesDays || day % kLinesEveryDays != 0 || hour < kLinesHour || day == printed_day) {
    return;
  }
  printed_day = day;
  const std::vector<core::BarterYardLine> lines = simulation.BarterDryLines();

  struct Total {
    unsigned yards_offer = 0;
    unsigned yards_claim = 0;
    long long offered = 0;
    long long claimed = 0;
    long long give = 0;
    long long take = 0;
  };

  std::map<unsigned, Total> by_resource;

  struct YardText {
    std::string gives;
    std::string takes;
    std::string offers;
    std::string claims;
    bool moves = false;
  };

  std::map<std::uint32_t, YardText> by_yard;
  const auto add = [](std::string& text, unsigned resource, long long grams) {
    if (grams > 0) {
      text += std::to_string(resource) + ":" + std::to_string(grams) + ";";
    }
  };
  for (const core::BarterYardLine& line : lines) {
    Total& total = by_resource[line.resource.value];
    total.yards_offer += line.offered > 0 ? 1U : 0U;
    total.yards_claim += line.claimed > 0 ? 1U : 0U;
    total.offered += line.offered;
    total.claimed += line.claimed;
    total.give += line.would_give;
    total.take += line.would_take;
    const std::uint32_t row = core::FindRow(world.families, line.family);
    YardText& yard = by_yard[row];
    add(yard.gives, line.resource.value, line.would_give);
    add(yard.takes, line.resource.value, line.would_take);
    add(yard.offers, line.resource.value, line.offered);
    add(yard.claims, line.resource.value, line.claimed);
    yard.moves = yard.moves || line.would_give > 0 || line.would_take > 0;
  }
  if (by_resource.empty()) {
    std::printf(
        "YQ,day=%lld,resource=-,yards_offer=0,yards_claim=0,offered_g=0,claimed_g=0,"
        "give_g=0,take_g=0\n",
        static_cast<long long>(day));
  }
  for (const auto& [resource, total] : by_resource) {
    std::printf(
        "YQ,day=%lld,resource=%u,yards_offer=%u,yards_claim=%u,offered_g=%lld,"
        "claimed_g=%lld,give_g=%lld,take_g=%lld\n",
        static_cast<long long>(day),
        resource,
        total.yards_offer,
        total.yards_claim,
        total.offered,
        total.claimed,
        total.give,
        total.take);
  }
  std::vector<unsigned> eaters(world.families.rows.size(), 0U);
  // The yard's night trade, the first found (0 none, else NightTrade's value
  // — 1 is the distiller, whose yard is paid in grain for the drink), and
  // its men who drink (alcoholism over 20: they pay him).
  std::vector<unsigned> trade(world.families.rows.size(), 0U);
  std::vector<unsigned> drinkers(world.families.rows.size(), 0U);
  constexpr float kDrinksFrom = 20.0F;
  for (const core::ResidentRow& person : world.residents.rows) {
    const std::uint32_t row = core::FindRow(world.families, person.family);
    if (row != core::kNoRow) {
      ++eaters[row];
      if (trade[row] == 0U) {
        trade[row] = static_cast<unsigned>(person.night_trade);
      }
      drinkers[row] += person.alcoholism > kDrinksFrom ? 1U : 0U;
    }
  }
  std::vector<unsigned> hens(world.families.rows.size(), 0U);
  std::vector<unsigned> goats(world.families.rows.size(), 0U);
  for (const core::HerdRow& herd : world.herds.rows) {
    const std::uint32_t row = core::FindRow(world.families, herd.household);
    if (herd.household_owned == 0 || row == core::kNoRow) {
      continue;
    }
    if (herd.kind.value == hen_kind) {
      hens[row] += herd.adult_count;
    } else if (herd.kind.value == goat_kind) {
      goats[row] += herd.adult_count;
    }
  }
  const auto text = [](const std::string& value) { return value.empty() ? "-" : value.c_str(); };
  for (const auto& [row, yard] : by_yard) {
    if (!yard.moves || row == core::kNoRow) {
      continue;
    }
    std::printf(
        "YG,day=%lld,family=%u,eaters=%u,hens=%u,goats=%u,night_trade=%u,drinkers=%u,"
        "gives=%s,takes=%s,offers=%s,claims=%s\n",
        static_cast<long long>(day),
        row,
        eaters[row],
        hens[row],
        goats[row],
        trade[row],
        drinkers[row],
        text(yard.gives),
        text(yard.takes),
        text(yard.offers),
        text(yard.claims));
  }
}

}  // namespace barter_lines_probe

#endif  // TESTS_RUN_PROBES_BARTER_LINES_PROBE_H_
