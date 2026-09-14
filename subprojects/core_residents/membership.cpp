// Membership of the organizations and the forming of ideology
// (core_residents/membership.h).

#include "membership.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "core_catalog/table_value.h"
#include "core_common/calendar.h"
#include "core_common/emit_event.h"
#include "core_residents/residents_system.h"
#include "core_tables/tables.h"
#include "life_config.h"

namespace core {
namespace {

/// The world_params.csv keys, in the order of the knob list in the parse.
constexpr std::array<std::string_view, 27> kMembershipWorldParamKeys = {
    "membership_autumn_wave_month",
    "membership_spring_wave_month",
    "pioneer_age_from_years",
    "pioneer_age_to_years",
    "pioneer_autumn_grade_min",
    "pioneer_spring_grade_min",
    "komsomol_age_from_years",
    "komsomol_age_to_years",
    "komsomol_ideology_min",
    "komsomol_pioneer_ideology_min",
    "komsomol_mood_min",
    "party_age_from_years",
    "party_ideology_min",
    "party_mood_min",
    "party_seniority_age_years",
    "party_early_ideology_min",
    "ideology_birth_spread",
    "ideology_lock_age_years",
    "ideology_school_year_gain",
    "ideology_pioneer_year_gain",
    "ideology_hungry_year_loss",
    "ideology_hungry_satiety_below",
    "ideology_status_gain_pioneer",
    "ideology_status_gain_komsomol",
    "ideology_status_gain_party",
    "ideology_status_cap",
    "specialist_komsomol_age_max_years"};

constexpr float kMetricLow = 0.0F;
constexpr float kMetricHigh = 100.0F;

/// The oldest age any band of the file may name: past it the cell is a typo.
constexpr float kOldestYears = 120.0F;

/// The largest yearly gain or loss of ideology a row may name.
constexpr float kLargestYearStep = 50.0F;

float ClampMetric(float value) {
  return std::clamp(value, kMetricLow, kMetricHigh);
}

/// Crime closes every organization, even the pioneers (society §5): both
/// metrics at zero.
bool CrimeFree(const ResidentRow& person) {
  return person.crime_inclination <= 0.0F && person.offense_count == 0;
}

bool InBand(float age_years, float from_years, float to_years) {
  return age_years >= from_years && age_years < to_years;
}

bool PioneerAdmits(const MembershipConfig& config,
                   const ResidentRow& person,
                   float age_years,
                   MembershipWave wave) {
  if (!InBand(age_years, config.pioneer_age_from_years, config.pioneer_age_to_years) ||
      !CrimeFree(person)) {
    return false;
  }
  // STUB of the grades: a grade the core does not keep yet (0) is not a
  // good pupil's, so it waits for the spring wave, which takes "the rest".
  if (person.current_grade <= 0.0F) {
    return wave == MembershipWave::kSpring;
  }
  const float grade_min = wave == MembershipWave::kAutumn ? config.pioneer_autumn_grade_min
                                                          : config.pioneer_spring_grade_min;
  return person.current_grade >= grade_min;
}

bool KomsomolAdmits(const MembershipConfig& config, const ResidentRow& person, float age_years) {
  const float ideology_min = person.social_status == SocialStatus::kPioneer
                                 ? config.komsomol_pioneer_ideology_min
                                 : config.komsomol_ideology_min;
  return InBand(age_years, config.komsomol_age_from_years, config.komsomol_age_to_years) &&
         CrimeFree(person) && person.ideology >= ideology_min &&
         person.mood >= config.komsomol_mood_min;
}

bool PartyAdmits(const MembershipConfig& config, const ResidentRow& person, float age_years) {
  const bool seniority = age_years >= config.party_seniority_age_years ||
                         person.ideology >= config.party_early_ideology_min;
  return age_years >= config.party_age_from_years && CrimeFree(person) &&
         person.ideology >= config.party_ideology_min && person.mood >= config.party_mood_min &&
         seniority;
}

float StatusGain(const MembershipConfig& config, SocialStatus status) {
  switch (status) {
    case SocialStatus::kPioneer:
      return config.ideology_status_gain_pioneer;
    case SocialStatus::kKomsomol:
      return config.ideology_status_gain_komsomol;
    case SocialStatus::kParty:
      return config.ideology_status_gain_party;
    default:
      return 0.0F;
  }
}

/// A wave over the whole village; `announce` raises kSocialStatusChanged for
/// every status that moved. Defined below the public entry points.
void ApplyWave(const MembershipConfig& config,
               float life_speedup,
               MembershipWave wave,
               bool announce,
               WorldState& current);

}  // namespace

std::span<const std::string_view> MembershipWorldParamKeys() {
  return kMembershipWorldParamKeys;
}

bool ParseMembershipConfig(const ITableSet& tables, MembershipConfig& config, std::string& error) {
  const ITable* const world = tables.FindTable("world_params");
  if (world == nullptr) {
    return true;
  }
  auto autumn = static_cast<float>(config.autumn_wave_month);
  auto spring = static_cast<float>(config.spring_wave_month);
  const Range months{.low = 1.0F, .high = static_cast<float>(kMonthsPerYear)};
  const Range ages{.low = 0.0F, .high = kOldestYears};
  const Range grades{.low = 0.0F, .high = 5.0F};
  const Range metric{.low = kMetricLow, .high = kMetricHigh};
  const Range step{.low = 0.0F, .high = kLargestYearStep};
  const std::array<ScalarKnob, kMembershipWorldParamKeys.size()> knobs = {{
      {.key = kMembershipWorldParamKeys[0], .value = &autumn, .range = months},
      {.key = kMembershipWorldParamKeys[1], .value = &spring, .range = months},
      {.key = kMembershipWorldParamKeys[2], .value = &config.pioneer_age_from_years, .range = ages},
      {.key = kMembershipWorldParamKeys[3], .value = &config.pioneer_age_to_years, .range = ages},
      {.key = kMembershipWorldParamKeys[4],
       .value = &config.pioneer_autumn_grade_min,
       .range = grades},
      {.key = kMembershipWorldParamKeys[5],
       .value = &config.pioneer_spring_grade_min,
       .range = grades},
      {.key = kMembershipWorldParamKeys[6],
       .value = &config.komsomol_age_from_years,
       .range = ages},
      {.key = kMembershipWorldParamKeys[7], .value = &config.komsomol_age_to_years, .range = ages},
      {.key = kMembershipWorldParamKeys[8],
       .value = &config.komsomol_ideology_min,
       .range = metric},
      {.key = kMembershipWorldParamKeys[9],
       .value = &config.komsomol_pioneer_ideology_min,
       .range = metric},
      {.key = kMembershipWorldParamKeys[10], .value = &config.komsomol_mood_min, .range = metric},
      {.key = kMembershipWorldParamKeys[11], .value = &config.party_age_from_years, .range = ages},
      {.key = kMembershipWorldParamKeys[12], .value = &config.party_ideology_min, .range = metric},
      {.key = kMembershipWorldParamKeys[13], .value = &config.party_mood_min, .range = metric},
      {.key = kMembershipWorldParamKeys[14],
       .value = &config.party_seniority_age_years,
       .range = ages},
      {.key = kMembershipWorldParamKeys[15],
       .value = &config.party_early_ideology_min,
       .range = metric},
      {.key = kMembershipWorldParamKeys[16],
       .value = &config.ideology_birth_spread,
       .range = metric},
      {.key = kMembershipWorldParamKeys[17],
       .value = &config.ideology_lock_age_years,
       .range = ages},
      {.key = kMembershipWorldParamKeys[18],
       .value = &config.ideology_school_year_gain,
       .range = step},
      {.key = kMembershipWorldParamKeys[19],
       .value = &config.ideology_pioneer_year_gain,
       .range = step},
      {.key = kMembershipWorldParamKeys[20],
       .value = &config.ideology_hungry_year_loss,
       .range = step},
      {.key = kMembershipWorldParamKeys[21],
       .value = &config.ideology_hungry_satiety_below,
       .range = metric},
      {.key = kMembershipWorldParamKeys[22],
       .value = &config.ideology_status_gain_pioneer,
       .range = step},
      {.key = kMembershipWorldParamKeys[23],
       .value = &config.ideology_status_gain_komsomol,
       .range = step},
      {.key = kMembershipWorldParamKeys[24],
       .value = &config.ideology_status_gain_party,
       .range = step},
      {.key = kMembershipWorldParamKeys[25], .value = &config.ideology_status_cap, .range = metric},
      {.key = kMembershipWorldParamKeys[26],
       .value = &config.specialist_komsomol_age_max_years,
       .range = ages},
  }};
  if (!ReadKnobs(*world, "world_params", knobs, error)) {
    return false;
  }
  if (std::floor(autumn) != autumn || std::floor(spring) != spring) {
    error = "world_params: a membership wave month is not a whole number";
    return false;
  }
  if (config.pioneer_age_from_years >= config.pioneer_age_to_years ||
      config.komsomol_age_from_years >= config.komsomol_age_to_years) {
    error = "world_params: a pioneer or komsomol age band ends before it begins";
    return false;
  }
  config.autumn_wave_month = static_cast<std::uint8_t>(autumn);
  config.spring_wave_month = static_cast<std::uint8_t>(spring);
  return true;
}

MembershipWave WaveOfDay(const MembershipConfig& config, SimDay day) {
  const std::uint32_t day_of_year = day % kDaysPerYear;
  if (day_of_year % kDaysPerMonth != 0) {
    return MembershipWave::kNone;
  }
  const std::uint32_t month = (day_of_year / kDaysPerMonth) + 1U;
  if (month == config.autumn_wave_month) {
    return MembershipWave::kAutumn;
  }
  if (month == config.spring_wave_month) {
    return MembershipWave::kSpring;
  }
  return MembershipWave::kNone;
}

SocialStatus StatusAfterWave(const MembershipConfig& config,
                             const ResidentRow& person,
                             float age_years,
                             MembershipWave wave) {
  if (wave == MembershipWave::kNone) {
    return person.social_status;
  }
  switch (person.social_status) {
    case SocialStatus::kParty:
      return SocialStatus::kParty;  // for life
    case SocialStatus::kKomsomol:
      if (PartyAdmits(config, person, age_years)) {
        return SocialStatus::kParty;
      }
      return age_years < config.komsomol_age_to_years ? SocialStatus::kKomsomol
                                                      : SocialStatus::kNone;
    case SocialStatus::kPioneer:
      if (KomsomolAdmits(config, person, age_years)) {
        return SocialStatus::kKomsomol;
      }
      return age_years < config.pioneer_age_to_years ? SocialStatus::kPioneer : SocialStatus::kNone;
    default:
      break;
  }
  if (PartyAdmits(config, person, age_years)) {
    return SocialStatus::kParty;
  }
  if (KomsomolAdmits(config, person, age_years)) {
    return SocialStatus::kKomsomol;
  }
  if (PioneerAdmits(config, person, age_years, wave)) {
    return SocialStatus::kPioneer;
  }
  return SocialStatus::kNone;
}

void RunMembershipWave(const MembershipConfig& config,
                       float life_speedup,
                       MembershipWave wave,
                       WorldState& current) {
  ApplyWave(config, life_speedup, wave, /*announce=*/true, current);
}

bool ApplyStartMembership(const ITableSet& tables,
                          float life_speedup,
                          WorldState& world,
                          std::string& error) {
  MembershipConfig config;
  if (!ParseMembershipConfig(tables, config, error)) {
    return false;
  }
  ApplyWave(config, life_speedup, MembershipWave::kSpring, /*announce=*/false, world);
  return true;
}

namespace {

void ApplyWave(const MembershipConfig& config,
               float life_speedup,
               MembershipWave wave,
               bool announce,
               WorldState& current) {
  if (wave == MembershipWave::kNone) {
    return;
  }
  for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
    ResidentRow& person = current.residents.rows[row];
    const float age_years =
        BiologicalAgeYears(life_speedup, person.birth_day, current.calendar.day);
    const SocialStatus after = StatusAfterWave(config, person, age_years, wave);
    if (after == person.social_status) {
      continue;
    }
    const bool joined = after != SocialStatus::kNone;
    person.social_status = after;
    if (joined) {
      // "Приняли — значит человек взялся за ум" (society §5).
      person.crime_inclination = 0.0F;
      person.offense_count = 0;
    }
    if (!announce) {
      continue;
    }
    SimEvent& changed =
        EmitEvent(current, EventKind::kSocialStatusChanged, EventSeverity::kRoutine);
    changed.resident = current.residents.row_ids[row];
    changed.family = person.family;
    changed.amount = static_cast<std::int64_t>(after);
  }
}

}  // namespace

float BirthIdeology(const MembershipConfig& config,
                    RngState& rng,
                    const ResidentRow& mother,
                    const ResidentRow* father) {
  const float parents =
      father == nullptr ? mother.ideology : (mother.ideology + father->ideology) / 2.0F;
  // One draw, as the uniform roll it replaced took: the stream's later
  // decisions keep their places.
  const float offset = ((NextRandomUnitFloat(rng) * 2.0F) - 1.0F) * config.ideology_birth_spread;
  return ClampMetric(parents + offset);
}

void TurnIdeologyYear(const MembershipConfig& config,
                      float life_speedup,
                      float school_from_years,
                      float school_to_years,
                      WorldState& current) {
  const bool hungry_year =
      current.vitals.satiety_year_means.back() < config.ideology_hungry_satiety_below;
  for (ResidentRow& person : current.residents.rows) {
    const float age_years =
        BiologicalAgeYears(life_speedup, person.birth_day, current.calendar.day);
    float ideology = person.ideology;
    if (age_years < config.ideology_lock_age_years) {
      if (InBand(age_years, school_from_years, school_to_years)) {
        ideology += config.ideology_school_year_gain;
      }
      if (person.social_status == SocialStatus::kPioneer) {
        ideology += config.ideology_pioneer_year_gain;
      }
      if (hungry_year) {
        ideology -= config.ideology_hungry_year_loss;
      }
    }
    const float status_gain = StatusGain(config, person.social_status);
    if (status_gain > 0.0F && ideology < config.ideology_status_cap) {
      ideology = std::min(ideology + status_gain, config.ideology_status_cap);
    }
    person.ideology = ClampMetric(ideology);
  }
}

}  // namespace core
