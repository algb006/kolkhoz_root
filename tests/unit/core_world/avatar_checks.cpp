// The checks of the avatar key door (avatar_checks.h).

#include "avatar_checks.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "../../common/fake_tables.h"
#include "core_common/calendar.h"
#include "core_common/ids.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

struct Expected {
  std::string_view key;
  float reputation;
  float crime;
  float electrification;
};

/// The shipped table's eight rows, by the Райком sign (avatars.md «Пути»):
/// «++» 61, «+» 55, «0» and «—» 50, «−» 45, «−−» 41.
constexpr std::array<Expected, 8> kShipped = {{{"villager", 45.0F, 0.0F, 1.0F},
                                               {"worker", 50.0F, 0.0F, 1.0F},
                                               {"student", 50.0F, 0.0F, 1.0F},
                                               {"ex_chairman", 45.0F, 0.0F, 1.0F},
                                               {"promoted", 61.0F, 0.0F, 0.75F},
                                               {"old_fighter", 55.0F, -20.0F, 1.0F},
                                               {"dealer", 41.0F, 0.0F, 1.0F},
                                               {"acting", 45.0F, 0.0F, 1.0F}}};

int CheckTheShippedTable() {
  int failures = 0;
  std::string error;
  const auto tables = core::LoadTableSet(KOLKHOZ_TABLES_DIR, &error);
  if (tables == nullptr || tables->FindTable("avatars") == nullptr) {
    return Expect(false, "avatar door: the shipped tables load and carry avatars.csv");
  }
  for (const Expected& row : kShipped) {
    core::WorldState world;
    std::string refusal;
    const bool applied = core::ApplyStartAvatar(world, *tables, row.key, &refusal);
    const bool right = applied && world.chairman.avatar.value != core::kInvalidDefIdValue &&
                       world.chairman.raikom_reputation == row.reputation &&
                       world.chairman.crime_modifier == row.crime &&
                       world.chairman.electrification_threshold_factor == row.electrification;
    if (!right) {
      std::cout << "  avatar " << row.key << ": applied " << applied << ", reputation "
                << world.chairman.raikom_reputation << ", crime " << world.chairman.crime_modifier
                << ", electrification " << world.chairman.electrification_threshold_factor << " "
                << refusal << '\n';
    }
    failures +=
        Expect(right, "avatar door: each shipped key gives its sign's reputation and factors");
    // «Страда без выходных» starts off for the student alone (#635), on for
    // every other avatar - the struct's default.
    const bool harvest_on = world.chairman.harvest_without_days_off == 1;
    failures += Expect(harvest_on == (row.key != "student"),
                       "avatar door: the harvest standing order starts off for the student and "
                       "on for every other");
  }
  return failures;
}

int CheckTheEmptyAndTheUnknown() {
  int failures = 0;
  const test::FakeTable avatars(
      {"key", "start_reputation", "crime_modifier", "electrification_threshold_factor"},
      {{"promoted", "61", "0", "0.75"}, {"dealer", "41", "0", "1"}});
  const test::FakeTableSet tables("avatars", avatars);
  core::WorldState empty;
  failures += Expect(core::ApplyStartAvatar(empty, tables, "", nullptr) &&
                         empty.chairman.avatar.value == core::kInvalidDefIdValue &&
                         empty.chairman.raikom_reputation == 50.0F &&
                         empty.chairman.crime_modifier == 0.0F &&
                         empty.chairman.electrification_threshold_factor == 1.0F,
                     "avatar door: the empty key succeeds and changes nothing - 50, no avatar");
  core::WorldState dealer;
  failures +=
      Expect(core::ApplyStartAvatar(dealer, tables, "dealer", nullptr) &&
                 dealer.chairman.avatar.value == 1 && dealer.chairman.raikom_reputation == 41.0F,
             "avatar door: the avatar is the ROW of its key, the second here");
  core::WorldState unknown;
  std::string refusal;
  failures += Expect(!core::ApplyStartAvatar(unknown, tables, "kulak", &refusal) &&
                         refusal.find("kulak") != std::string::npos &&
                         unknown.chairman.avatar.value == core::kInvalidDefIdValue &&
                         unknown.chairman.raikom_reputation == 50.0F,
                     "avatar door: an unknown key is refused by name and the world is untouched");
  const test::FakeTableSet none;
  std::string no_table;
  failures += Expect(!core::ApplyStartAvatar(unknown, none, "promoted", &no_table) &&
                         no_table.find("avatars.csv") != std::string::npos,
                     "avatar door: a key with no avatars.csv is refused, not served 50");
  const test::FakeTable bad(
      {"key", "start_reputation", "crime_modifier", "electrification_threshold_factor"},
      {{"promoted", "161", "0", "0.75"}});
  const test::FakeTableSet bad_tables("avatars", bad);
  failures += Expect(!core::ApplyStartAvatar(unknown, bad_tables, "promoted", nullptr),
                     "avatar door: a start reputation past 100 is a table error, not a clamp");
  return failures;
}

/// THE ACTING CHAIRMAN'S THREE TRAINED MILKMAIDS (#1417; avatars.md l.184):
/// the first three adult women by row holding no post get skill 75. Here the
/// rows are: a man, an adult woman, a girl, a woman with a post, two adult
/// women, a fourth adult woman - the trained are rows 1, 4 and 5. With one
/// such woman only, one is trained and the start does not refuse.
int CheckTheTrainedMilkmaids() {
  int failures = 0;
  const test::FakeTable avatars({"key", "start_reputation", "start_trained_milkmaids"},
                                {{"acting", "45", "3"}, {"dealer", "41", ""}});
  const test::FakeTableSet tables("avatars", avatars);
  constexpr std::int32_t kAdult = -30 * static_cast<std::int32_t>(core::kDaysPerYear);
  constexpr std::int32_t kGirl = -5 * static_cast<std::int32_t>(core::kDaysPerYear);
  const auto start = [&](std::span<const std::pair<core::Sex, std::int32_t>> people,
                         std::size_t posted_row) {
    core::WorldState world;
    for (std::size_t index = 0; index < people.size(); ++index) {
      core::ResidentRow person;
      person.sex = people[index].first;
      person.birth_day = people[index].second;
      if (index == posted_row) {
        person.post.profession = core::ProfessionId{0};
        person.post.unit = core::UnitId{1};
      }
      core::AppendRow(world.residents, person);
    }
    return world;
  };
  const std::array<std::pair<core::Sex, std::int32_t>, 7> village = {
      {{core::Sex::kMale, kAdult},
       {core::Sex::kFemale, kAdult},
       {core::Sex::kFemale, kGirl},
       {core::Sex::kFemale, kAdult},
       {core::Sex::kFemale, kAdult},
       {core::Sex::kFemale, kAdult},
       {core::Sex::kFemale, kAdult}}};
  core::WorldState acting = start(village, 3);
  failures += Expect(core::ApplyStartAvatar(acting, tables, "acting", nullptr),
                     "trained milkmaids: the acting chairman's start applies");
  std::string trained;
  for (const core::ResidentRow& person : acting.residents.rows) {
    trained += person.skill_agriculture_earned == 75.0F ? '1' : '0';
  }
  failures += Expect(trained == "0100110",
                     "trained milkmaids: the first three adult women holding no post get skill 75 "
                     "- not the man, the girl, the posted woman or the fourth");
  core::WorldState dealer = start(village, 3);
  bool none = core::ApplyStartAvatar(dealer, tables, "dealer", nullptr);
  for (const core::ResidentRow& person : dealer.residents.rows) {
    none = none && person.skill_agriculture_earned == 0.0F;
  }
  failures += Expect(none, "trained milkmaids: a chairman with no such path trains nobody");
  const std::array<std::pair<core::Sex, std::int32_t>, 2> small = {
      {{core::Sex::kMale, kAdult}, {core::Sex::kFemale, kAdult}}};
  core::WorldState few = start(small, 99);
  failures +=
      Expect(core::ApplyStartAvatar(few, tables, "acting", nullptr) &&
                 few.residents.rows[1].skill_agriculture_earned == 75.0F,
             "trained milkmaids: a start with one such woman trains her, and does not refuse");
  return failures;
}

int CheckTheAssembly() {
  int failures = 0;
  std::string error;
  const auto tables = core::LoadTableSet(KOLKHOZ_TABLES_DIR, &error);
  if (tables == nullptr) {
    return Expect(false, "avatar door: the shipped tables load");
  }
  core::StandardSimulationConfig config;
  config.tables = tables.get();
  config.world_seed = 1931;
  config.avatar_key = "promoted";
  const auto promoted = core::CreateStandardSimulation(config);
  failures +=
      Expect(promoted != nullptr && promoted->CompletedState().chairman.raikom_reputation == 61.0F,
             "avatar door: the assembly starts the promoted man at 61");
  config.avatar_key = "kulak";
  failures += Expect(core::CreateStandardSimulation(config) == nullptr,
                     "avatar door: the assembly refuses an unknown key - no default chairman");
  return failures;
}

}  // namespace

int CheckTheAvatarDoor() {
  int failures = 0;
  failures += CheckTheShippedTable();
  failures += CheckTheEmptyAndTheUnknown();
  failures += CheckTheTrainedMilkmaids();
  failures += CheckTheAssembly();
  return failures;
}
