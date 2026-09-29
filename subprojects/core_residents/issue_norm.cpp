#include "issue_norm.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/day_off.h"
#include "core_common/ids.h"
#include "core_common/order_state.h"
#include "core_common/quantities.h"
#include "family_exchange.h"

namespace core {
namespace {

/// A position under the default rule: the chairman set none, or put this
/// one back (kResetIssueNorm), or never touched it after his first order.
bool UnderDefault(const WorldState& world, std::uint32_t index) {
  return world.issue_norms.empty() || index >= world.issue_norms.size() ||
         world.issue_norms[index] == kIssueNormByDefault;
}

/// food.csv `issue_kg_per_trudoden`, grams: the norm of a position no
/// harvest gives (milk), and for a harvest position only its membership of
/// the default bundle (above nought) — its grams are not read (boss, [7]).
Grams TableGrams(const FoodConfig& config, std::uint32_t index) {
  return GramsFromKilograms(config.resources[index].issue_kg_per_trudoden);
}

bool IsFood(const FoodConfig& config, std::uint32_t index) {
  return config.resources[index].kcal_per_gram > 0.0F;
}

/// A position that goes out at all by its line: a norm above nought, or a
/// share position of the default bundle (its norm may be nought today for
/// an empty store, and it is still the category's position).
bool CarriesANorm(const FoodConfig& config, const IssueNormLine& line, std::uint32_t index) {
  if (line.basis == IssueNormBasis::kShareOfRemainder) {
    return TableGrams(config, index) > 0;
  }
  return line.grams_per_trudoden > 0;
}

Grams NormOf(Grams free_grams, float trudodni) {
  if (!(trudodni > 0.0F) || free_grams <= 0) {
    return 0;
  }
  // Capped at the bound a chairman's order may carry (order_state.h): on the
  // eve of a harvest the trudodni left to share among can be a handful, and
  // a norm the door answers must be one an order could set back.
  const auto norm = static_cast<Grams>(
      std::llround(static_cast<double>(free_grams) / static_cast<double>(trudodni)));
  return norm < kMaxIssueNormGrams ? norm : kMaxIssueNormGrams;
}

}  // namespace

float OutstandingTrudodni(const WorldState& world) {
  std::int64_t hundredths = 0;
  for (const FamilyRow& family : world.families.rows) {
    const TrudodniHundredths outstanding = family.trudodni_account - family.trudodni_redeemed;
    hundredths += outstanding > 0 ? outstanding : 0;
  }
  return static_cast<float>(hundredths) / static_cast<float>(kTrudodniScale);
}

float ForecastTrudodni(const FoodConfig& config,
                       const WorldState& world,
                       float life_speedup,
                       std::int32_t days,
                       bool& fallback) {
  fallback = false;
  if (days <= 0) {
    return 0.0F;
  }
  const SimDay today = world.calendar.day;
  // THE YEAR'S FIRST DAY reads the book still open: the books rotate in the
  // events slot (core_world, RotateLedger), after the distribution of the
  // same tick, so on that day `current` is the year just ended and `closed`
  // the one before it (static review of 0.37.29). Rotated, `closed` names
  // the year before today's.
  const bool unrotated = today > 0 && today % kDaysPerYear == 0 &&
                         world.ledger.closed.year + 1U != world.calendar.date.year;
  const YearLedger& last_year = unrotated ? world.ledger.current : world.ledger.closed;
  std::int64_t last_total = 0;
  for (const TrudodniHundredths hundredths : last_year.trudodni_by_day) {
    last_total += hundredths;
  }
  if (last_total > 0) {
    std::int64_t hundredths = 0;
    for (std::int32_t ahead = 0; ahead < days; ++ahead) {
      const SimDay day = today + static_cast<SimDay>(ahead);
      hundredths += last_year.trudodni_by_day[day % kDaysPerYear];
    }
    return static_cast<float>(hundredths) / static_cast<float>(kTrudodniScale);
  }
  // THE FIRST YEAR: no last year to read. The adults at home today, at the
  // canon's first-year rate, on the working days to the harvest.
  fallback = true;
  float adults = 0.0F;
  for (const ResidentRow& resident : world.residents.rows) {
    const float age = BiologicalAgeYears(life_speedup, resident.birth_day, today);
    adults += age >= config.consumption.adult_from_bio_years ? 1.0F : 0.0F;
  }
  float working_days = 0.0F;
  for (std::int32_t ahead = 0; ahead < days; ++ahead) {
    working_days += IsDayOffIn(world, today + static_cast<SimDay>(ahead)) ? 0.0F : 1.0F;
  }
  return adults * kFirstYearTrudodniPerAdultDay * working_days;
}

std::vector<IssueNormLine> ResolveIssueNorms(const FoodConfig& config,
                                             const std::vector<Grams>& reserve,
                                             const WorldState& world,
                                             float life_speedup) {
  const auto roster = static_cast<std::uint32_t>(config.resources.size());
  std::vector<IssueNormLine> lines(roster);
  const float outstanding = OutstandingTrudodni(world);
  // PASS 1: each position's basis, and under the share its two halves.
  for (std::uint32_t index = 0; index < roster; ++index) {
    IssueNormLine& line = lines[index];
    line.resource = DefIdFromIndex<ResourceIdTag>(index);
    if (!IsFood(config, index)) {
      // Not eaten: fodder rides along at the table's grams, as it always
      // did (RunDistribution), and no order moves it (kSetIssueNorm is
      // food only).
      line.basis = IssueNormBasis::kTableGrams;
      line.grams_per_trudoden = TableGrams(config, index);
      continue;
    }
    line.days_to_harvest =
        config.days_to_harvest_of ? config.days_to_harvest_of(world, line.resource) : -1;
    if (!UnderDefault(world, index)) {
      line.basis = IssueNormBasis::kChairman;
      line.grams_per_trudoden = world.issue_norms[index];
      continue;
    }
    if (line.days_to_harvest < 0) {
      line.basis = IssueNormBasis::kTableGrams;
      line.grams_per_trudoden = TableGrams(config, index);
      continue;
    }
    line.basis = IssueNormBasis::kShareOfRemainder;
    if (TableGrams(config, index) <= 0) {
      continue;  // not in the default bundle: nought
    }
    const float share = config.resources[index].issue_share_of_stock;
    line.free_grams =
        static_cast<Grams>(static_cast<double>(FreeIssueStock(world, reserve, line.resource)) *
                           static_cast<double>(share));
    bool fallback = false;
    line.trudodni =
        outstanding + ForecastTrudodni(config, world, life_speedup, line.days_to_harvest, fallback);
    line.forecast_fallback = fallback;
  }
  // PASS 2: the substitutes' stock joins the shortest-keeping share
  // positions of their category (the file header), and the norms.
  std::vector<bool> settled(roster, false);
  for (std::uint32_t index = 0; index < roster; ++index) {
    const IssueNormLine& line = lines[index];
    if (line.basis != IssueNormBasis::kShareOfRemainder || TableGrams(config, index) <= 0 ||
        settled[index]) {
      continue;
    }
    const FoodCategory category = config.resources[index].category;
    // The category's positions that go out, and the shortest keeping among them.
    float shortest = KeepsDays(config, index);
    for (std::uint32_t other = 0; other < roster; ++other) {
      if (category != FoodCategory::kNotFood && config.resources[other].category == category &&
          IsFood(config, other) && CarriesANorm(config, lines[other], other)) {
        shortest = std::fmin(shortest, KeepsDays(config, other));
      }
    }
    std::vector<std::uint32_t> primaries;
    std::vector<std::uint32_t> substitutes;
    for (std::uint32_t other = 0; other < roster; ++other) {
      const bool same = other == index || (category != FoodCategory::kNotFood &&
                                           config.resources[other].category == category);
      if (!same || lines[other].basis != IssueNormBasis::kShareOfRemainder ||
          TableGrams(config, other) <= 0 || settled[other]) {
        continue;
      }
      (KeepsDays(config, other) <= shortest ? primaries : substitutes).push_back(other);
    }
    if (primaries.empty()) {
      // The shortest positions of the category are the chairman's (or the
      // table's): the substitute shares its OWN remainder to its harvest,
      // and CoverBundle asks it the lesser of that and their shortfall — it
      // covers what his norm could not, never past its share, as its table
      // norm capped it before 0.37.29. While his norm is covered it is not
      // issued: that is the substitute rule (metrics §8), not a loss.
      for (const std::uint32_t own : substitutes) {
        lines[own].grams_per_trudoden = NormOf(lines[own].free_grams, lines[own].trudodni);
        settled[own] = true;
      }
      continue;
    }
    Grams joined = 0;
    for (const std::uint32_t substitute : substitutes) {
      joined += lines[substitute].free_grams;
    }
    Grams primaries_free = 0;
    for (const std::uint32_t primary : primaries) {
      primaries_free += lines[primary].free_grams;
    }
    Grams primaries_norm = 0;
    for (const std::uint32_t primary : primaries) {
      // The joined stock goes to the primaries by their own remainder, or
      // evenly when they hold none.
      const double part = primaries_free > 0 ? static_cast<double>(lines[primary].free_grams) /
                                                   static_cast<double>(primaries_free)
                                             : 1.0 / static_cast<double>(primaries.size());
      lines[primary].free_grams += static_cast<Grams>(static_cast<double>(joined) * part);
      lines[primary].grams_per_trudoden =
          NormOf(lines[primary].free_grams, lines[primary].trudodni);
      primaries_norm += lines[primary].grams_per_trudoden;
      settled[primary] = true;
    }
    for (const std::uint32_t substitute : substitutes) {
      // It covers what the primaries could not, gram for gram (CoverBundle):
      // asked at their norm, it can cover all of it.
      lines[substitute].grams_per_trudoden = primaries_norm;
      settled[substitute] = true;
    }
  }
  return lines;
}

}  // namespace core
