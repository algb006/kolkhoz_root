#include "start_literacy.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_catalog/table_value.h"
#include "core_common/calendar.h"
#include "core_common/random.h"
#include "core_common/state_table_ops.h"
#include "core_log/log.h"

namespace core {
namespace {

constexpr std::array<std::string_view, 8> kStartLiteracyKeys = {"start_literate_men_30plus",
                                                                "start_literate_women_30plus",
                                                                "start_literate_men_16_29",
                                                                "start_literate_women_16_29",
                                                                "start_literate_min_total",
                                                                "start_literate_min_men_30plus",
                                                                "start_literate_min_yards",
                                                                "start_literate_max_per_yard"};

/// The age of schooling at all, and where the older band begins (education
/// design §2: the parish school before 1917 for thirty and over).
constexpr float kLiterateFromYears = 16.0F;
constexpr float kOlderBandFromYears = 30.0F;

/// The counter-hash salt of the founders' schooling (the order among equals)
/// and of the grade drawn for one who reads.
constexpr std::uint64_t kLiteracyOrderSalt = 0x4C495400ULL;
constexpr std::uint64_t kLiteracyGradeSalt = 0x4C494700ULL;

/// The grade band of a founder at primary school, as the lot drew it.
constexpr float kGradeMin = 3.0F;
constexpr float kGradeMax = 5.0F;

enum class Band : std::uint8_t { kMenOlder, kMenYounger, kWomenOlder, kWomenYounger, kAny };

bool InBand(Band band, Sex sex, float age) {
  if (age < kLiterateFromYears) {
    return false;
  }
  const bool older = age >= kOlderBandFromYears;
  switch (band) {
    case Band::kMenOlder:
      return sex == Sex::kMale && older;
    case Band::kMenYounger:
      return sex == Sex::kMale && !older;
    case Band::kWomenOlder:
      return sex == Sex::kFemale && older;
    case Band::kWomenYounger:
      return sex == Sex::kFemale && !older;
    case Band::kAny:
      return true;
  }
  return false;
}

std::uint32_t Count(float value) {
  return value > 0.0F ? static_cast<std::uint32_t>(value + 0.5F) : 0U;
}

}  // namespace

std::span<const std::string_view> StartLiteracyWorldParamKeys() {
  return kStartLiteracyKeys;
}

StartLiteracy ReadStartLiteracy(const ITableSet& tables) {
  const ITable* const world = tables.FindTable("world_params");
  if (world == nullptr) {
    return StartLiteracy{};
  }
  const StartLiteracy defaults;
  std::array<float, kStartLiteracyKeys.size()> values = {
      static_cast<float>(defaults.men_30plus),
      static_cast<float>(defaults.women_30plus),
      static_cast<float>(defaults.men_16_29),
      static_cast<float>(defaults.women_16_29),
      static_cast<float>(defaults.min_total),
      static_cast<float>(defaults.min_men_30plus),
      static_cast<float>(defaults.min_yards),
      static_cast<float>(defaults.max_per_yard)};
  const Range people{.low = 0.0F, .high = 1000.0F};
  const Range per_yard{.low = 1.0F, .high = 100.0F};
  std::array<ScalarKnob, kStartLiteracyKeys.size()> knobs{};
  for (std::size_t index = 0; index < knobs.size(); ++index) {
    knobs[index] = ScalarKnob{.key = kStartLiteracyKeys[index],
                              .value = &values[index],
                              .range = index + 1 == knobs.size() ? per_yard : people};
  }
  std::string trouble;
  if (!ReadKnobs(*world, "world_params", knobs, trouble)) {
    LogError("genesis: " + trouble + " — the documented founders' schooling is used");
    return defaults;
  }
  return StartLiteracy{.men_30plus = Count(values[0]),
                       .women_30plus = Count(values[1]),
                       .men_16_29 = Count(values[2]),
                       .women_16_29 = Count(values[3]),
                       .min_total = Count(values[4]),
                       .min_men_30plus = Count(values[5]),
                       .min_yards = Count(values[6]),
                       .max_per_yard = Count(values[7])};
}

void SeedStartLiteracy(const StartLiteracy& start,
                       float life_speedup,
                       std::uint64_t world_seed,
                       WorldState& world) {
  const auto people = static_cast<std::uint32_t>(world.residents.rows.size());
  std::vector<float> ages(people, 0.0F);
  std::vector<std::uint32_t> yard_of(people, kNoRow);
  std::vector<std::uint32_t> readers_in_yard(world.families.rows.size(), 0);
  for (std::uint32_t row = 0; row < people; ++row) {
    const ResidentRow& person = world.residents.rows[row];
    ages[row] = BiologicalAgeYears(life_speedup, person.birth_day, world.calendar.day);
    yard_of[row] = FindRow(world.families, person.family);
  }
  std::vector<bool> reads(people, false);
  // One founder at a time: of the band's people who do not read yet and
  // whose yard is under the ceiling, the one whose yard has the fewest who
  // read, ties by the counter hash. Into an empty yard only, for the yards'
  // guarantee.
  const auto take_one = [&](Band band, bool into_empty_yard = false) {
    std::uint32_t best = kNoRow;
    std::uint32_t best_yard_count = 0;
    std::uint64_t best_order = 0;
    for (std::uint32_t row = 0; row < people; ++row) {
      const ResidentRow& person = world.residents.rows[row];
      const std::uint32_t yard = yard_of[row];
      if (reads[row] || yard == kNoRow || !InBand(band, person.sex, ages[row]) ||
          readers_in_yard[yard] >= start.max_per_yard ||
          (into_empty_yard && readers_in_yard[yard] > 0)) {
        continue;
      }
      const std::uint64_t order =
          CounterHashBits(world_seed, 0, world.residents.row_ids[row].value, kLiteracyOrderSalt);
      if (best == kNoRow || readers_in_yard[yard] < best_yard_count ||
          (readers_in_yard[yard] == best_yard_count && order < best_order)) {
        best = row;
        best_yard_count = readers_in_yard[yard];
        best_order = order;
      }
    }
    if (best == kNoRow) {
      return false;
    }
    reads[best] = true;
    ++readers_in_yard[yard_of[best]];
    return true;
  };
  const std::array<std::pair<Band, std::uint32_t>, 4> bands = {
      {{Band::kMenOlder, start.men_30plus},
       {Band::kMenYounger, start.men_16_29},
       {Band::kWomenOlder, start.women_30plus},
       {Band::kWomenYounger, start.women_16_29}}};
  std::uint32_t total = 0;
  std::uint32_t men_older = 0;
  for (const auto& [band, count] : bands) {
    for (std::uint32_t taken = 0; taken < count && take_one(band); ++taken) {
      ++total;
      men_older += band == Band::kMenOlder ? 1U : 0U;
    }
  }
  // The guarantees. Men of thirty first — they are the parish school's —
  // then anybody of sixteen, up to the minimum, then anybody of sixteen in a
  // yard with nobody who reads, up to the yards' minimum (0.37.33's review: it
  // was only checked, and a village the bands had bunched was logged short
  // with readers still to give).
  while (men_older < start.min_men_30plus && take_one(Band::kMenOlder)) {
    ++men_older;
    ++total;
  }
  while (total < start.min_total && take_one(Band::kAny)) {
    ++total;
  }
  std::uint32_t yards = 0;
  for (const std::uint32_t readers : readers_in_yard) {
    yards += readers > 0 ? 1U : 0U;
  }
  while (yards < start.min_yards && take_one(Band::kAny, true)) {
    ++yards;
    ++total;
  }
  if (total < start.min_total || men_older < start.min_men_30plus || yards < start.min_yards) {
    LogError("genesis: the founders' schooling misses a guarantee — " + std::to_string(total) +
             " read, " + std::to_string(men_older) + " men of thirty and over, in " +
             std::to_string(yards) + " yards; the village has too few of them to give");
  }
  for (std::uint32_t row = 0; row < people; ++row) {
    if (!reads[row]) {
      continue;
    }
    ResidentRow& person = world.residents.rows[row];
    person.education_stage = EducationStage::kPrimary;
    person.education_grade =
        kGradeMin + (CounterHashUnitFloat(
                         world_seed, 0, world.residents.row_ids[row].value, kLiteracyGradeSalt) *
                     (kGradeMax - kGradeMin));
  }
}

}  // namespace core
