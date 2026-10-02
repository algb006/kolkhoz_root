// The checks of the cart of the compressed year (cart_load_checks.h).

#include "cart_load_checks.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "core_tables/tables.h"
#include "labor_config.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

/// A table set with one table, transport.csv, of the text given.
bool ParseWith(const char* name, const std::string& transport, core::LaborConfig& config) {
  const std::filesystem::path root = std::filesystem::temp_directory_path() / name;
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  std::ofstream(root / "transport.csv") << transport;
  std::string error;
  const auto tables = core::LoadTableSet(root.string(), &error);
  return tables != nullptr && core::ParseLaborConfig(*tables, config, error);
}

bool Near(float left, float right) {
  return std::fabs(left - right) < 0.01F;
}

}  // namespace

int CheckTheCartOfTheCompressedYear() {
  int failures = 0;
  // THE SCALE MULTIPLIES THE LOAD: 0.75 t by 2.4 is a cart of 1.8 t.
  core::LaborConfig scaled;
  const bool scaled_ok = ParseWith("unit_core_labor_cart_scaled",
                                   "key,speed_kmh,load_tonnes,load_scale\n"
                                   "pedestrian,5,,\n"
                                   "horse_trot,12,,\n"
                                   "cart_loaded,9,0.75,2.4\n",
                                   scaled);
  std::cout << "  the cart of the compressed year, labour: 0.75 t by 2.4 reads "
            << scaled.cart_load_kg << " kg\n";
  failures += Expect(scaled_ok && Near(scaled.cart_load_kg, 1800.0F),
                     "cart load, labour: the table's tonnes times its scale - 1.8 t");
  // NO COLUMN, NO SCALE: a table written before the key reads as it did.
  core::LaborConfig plain;
  const bool plain_ok = ParseWith("unit_core_labor_cart_plain",
                                  "key,speed_kmh,load_tonnes\n"
                                  "pedestrian,5,\n"
                                  "horse_trot,12,\n"
                                  "cart_loaded,9,0.75\n",
                                  plain);
  failures += Expect(plain_ok && Near(plain.cart_load_kg, 750.0F),
                     "cart load, labour: a table with no scale column keeps the cart at its "
                     "tonnes");
  // A SCALE BELOW ONE IS A TYPO, NOT A CART: refused, by name.
  core::LaborConfig refused;
  failures += Expect(!ParseWith("unit_core_labor_cart_small",
                                "key,speed_kmh,load_tonnes,load_scale\n"
                                "pedestrian,5,,\n"
                                "horse_trot,12,,\n"
                                "cart_loaded,9,0.75,0.5\n",
                                refused),
                     "cart load, labour: a scale below one is refused");
  return failures;
}
