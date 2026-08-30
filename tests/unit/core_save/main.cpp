// Unit test of core_save. PLACEHOLDER until task O1 lands the
// implementation: it checks only what the contract fixes at compile time.
// O1 replaces it with the real test — the round trip on unchanged tables
// (memberwise equality, vector lengths included), the remap over reordered
// tables, and every refusal path of DecodeWorld.

#include <iostream>

#include "core_save/save.h"

int main() {
  int failures = 0;
  // Magic 8, format u32, header_size u16, three u16 versions, four u64.
  if (core::kSaveHeaderSize != 8 + 4 + 2 + (3 * 2) + (4 * 8)) {
    std::cout << "FAIL: the header size constant does not add up\n";
    ++failures;
  }
  if (core::kSaveMagic.size() != 8) {
    std::cout << "FAIL: the magic is not eight bytes\n";
    ++failures;
  }
  if (failures == 0) {
    std::cout << "unit_core_save: contract constants hold (implementation pending, task O1)\n";
  }
  return failures;
}
