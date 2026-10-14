// The porter's rules as the tables give them (core_catalog/porter_catalog.h; #671, the second
// delivery). Called from main.cpp (TestThePorterCatalog).
//
// THE KNOWN ANSWERS, by hand: labor.csv's four keys land in grams and as written; a table set
// from before the keys keeps its world - the bundle and the stretcher's head weigh the sack; the
// log's piece is resources.csv's kg_per_unit; a word that is no cargo form is refused by name; a
// table with no cargo_form column at all is a hand-made set and leaves every form kNone.
#include <cstdint>
#include <iostream>
#include <string>

#include "../../common/fake_tables.h"
#include "core_catalog/porter_catalog.h"
#include "core_common/cargo_form.h"
#include "core_common/haul.h"
#include "core_tables/tables.h"

namespace {

int ExpectCatalog(bool condition, const char* label) {
  if (condition) {
    return 0;
  }
  std::cout << "FAIL: porter catalog: " << label << '\n';
  return 1;
}

constexpr core::Grams kKg = 1000;

}  // namespace

int TestThePorterCatalog() {
  int failures = 0;
  const test::FakeTable resources({"key", "kg_per_unit", "cargo_form"},
                                  {{"hay", "1000", "rick"},
                                   {"manure", "1000", "manure"},
                                   {"log", "200", "long"},
                                   {"clay", "1000", "bulk"},
                                   {"flour", "1000", "packed"},
                                   {"calf", "150", "live"}});
  {
    const test::FakeTable labor({"key", "value"},
                                {{"carry_kg_adult", "20"},
                                 {"carry_kg_rick", "10"},
                                 {"carry_kg_stretcher_pair", "40"},
                                 {"log_pair_max_leg_m", "300"},
                                 {"log_pair_loaded_pace", "0.5"}});
    const test::FakeTableSet set({{"labor", &labor}, {"resources", &resources}});
    core::PorterRules rules;
    std::string error;
    const bool read = core::ReadPorterRules(set, rules, error);
    failures +=
        ExpectCatalog(read && rules.carry.adult == 20 * kKg && rules.carry.rick == 10 * kKg &&
                          rules.carry.stretcher_pair == 40 * kKg,
                      "the three carries come off labor.csv in grams");
    failures += ExpectCatalog(
        read && rules.log_pair_max_leg_m == 300.0F && rules.log_pair_loaded_pace == 0.5F,
        "the pair's walk and pace come off labor.csv");
    failures += ExpectCatalog(read && rules.log.value == 2 && rules.log_piece_grams == 200 * kKg,
                              "the log is resources.csv's row and its piece its kg_per_unit");
    failures += ExpectCatalog(read && rules.cargo_form.size() == 6 &&
                                  rules.cargo_form[0] == core::CargoForm::kRick &&
                                  rules.cargo_form[1] == core::CargoForm::kManure &&
                                  rules.cargo_form[2] == core::CargoForm::kLong &&
                                  rules.cargo_form[3] == core::CargoForm::kBulk &&
                                  rules.cargo_form[4] == core::CargoForm::kPacked &&
                                  rules.cargo_form[5] == core::CargoForm::kLive,
                              "every word of the cargo_form column is read as its form");
    failures +=
        ExpectCatalog(read && core::WalkerLoadGrams(rules, core::ResourceId{0}) == 10 * kKg &&
                          core::WalkerLoadGrams(rules, core::ResourceId{3}) == 20 * kKg,
                      "and a walker's load follows: hay the bundle, clay the sack");
    failures +=
        ExpectCatalog(read && rules.load_share.empty() &&
                          core::CartLoadGrams(rules, 1800.0F, core::ResourceId{0}) == 1800 * kKg,
                      "a resources table with no load_share column leaves the list empty "
                      "- every load is the cart's whole tonnage");
  }
  {
    // THE SHARE OF A CART'S TONNAGE (0.37.252): hay 0.6, an empty cell the
    // whole cart; a share of nought or above one is refused with the column.
    const test::FakeTable shared({"key", "kg_per_unit", "cargo_form", "load_share"},
                                 {{"hay", "1000", "rick", "0.6"},
                                  {"straw", "1000", "rick", "0.45"},
                                  {"clay", "1000", "bulk", ""}});
    const test::FakeTableSet set({{"resources", &shared}});
    core::PorterRules rules;
    std::string error;
    const bool read = core::ReadPorterRules(set, rules, error);
    failures +=
        ExpectCatalog(read && rules.load_share.size() == 3 &&
                          core::CartLoadGrams(rules, 1800.0F, core::ResourceId{0}) == 1080 * kKg &&
                          core::CartLoadGrams(rules, 1800.0F, core::ResourceId{1}) == 810 * kKg &&
                          core::CartLoadGrams(rules, 1800.0F, core::ResourceId{2}) == 1800 * kKg &&
                          core::CartLoadGrams(rules, 1800.0F, core::ResourceId{}) == 1800 * kKg,
                      "a cart of hay is 1.08 t and of straw 0.81 t of a 1.8 t cart; an "
                      "empty cell and a load with no resource are the whole cart");
    const test::FakeTable nought({"key", "kg_per_unit", "load_share"}, {{"hay", "1000", "0"}});
    const test::FakeTableSet bad({{"resources", &nought}});
    core::PorterRules refused;
    failures += ExpectCatalog(!core::ReadPorterRules(bad, refused, error) &&
                                  error.find("load_share") != std::string::npos,
                              "a share of nought is refused, and the column is named");
  }
  {
    // A table set written before the keys: only carry_kg_adult.
    const test::FakeTable labor({"key", "value"}, {{"carry_kg_adult", "25"}});
    const test::FakeTableSet set({{"labor", &labor}, {"resources", &resources}});
    core::PorterRules rules;
    std::string error;
    failures +=
        ExpectCatalog(core::ReadPorterRules(set, rules, error) && rules.carry.adult == 25 * kKg &&
                          rules.carry.rick == 25 * kKg && rules.carry.stretcher_pair == 50 * kKg,
                      "without the new keys the bundle is the sack and the stretcher two "
                      "sacks - the world before the forms");
  }
  {
    const test::FakeTable labor({"key", "value"}, {{"carry_kg_rick", "0"}});
    const test::FakeTableSet set({{"labor", &labor}, {"resources", &resources}});
    core::PorterRules rules;
    std::string error;
    failures += ExpectCatalog(!core::ReadPorterRules(set, rules, error) &&
                                  error.find("carry_kg_rick") != std::string::npos,
                              "a bundle of nought is refused, and the key is named");
  }
  {
    const test::FakeTable wrong({"key", "kg_per_unit", "cargo_form"}, {{"hay", "1000", "bale"}});
    const test::FakeTableSet set({{"resources", &wrong}});
    core::PorterRules rules;
    std::string error;
    failures += ExpectCatalog(!core::ReadPorterRules(set, rules, error) &&
                                  error.find("bale") != std::string::npos &&
                                  error.find("hay") != std::string::npos,
                              "a word that is no cargo form is refused with the row and the word");
  }
  {
    const test::FakeTable bare({"key", "kg_per_unit"}, {{"hay", "1000"}, {"log", "200"}});
    const test::FakeTableSet set({{"resources", &bare}});
    core::PorterRules rules;
    std::string error;
    failures +=
        ExpectCatalog(core::ReadPorterRules(set, rules, error) && rules.cargo_form.size() == 2 &&
                          rules.cargo_form[0] == core::CargoForm::kNone &&
                          core::WalkerLoadGrams(rules, core::ResourceId{0}) == 20 * kKg,
                      "a table with no cargo_form column is a hand-made set: no form, "
                      "every load a sack");
  }
  return failures;
}
