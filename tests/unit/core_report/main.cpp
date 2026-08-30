// Unit test of core_report. PLACEHOLDER until task O2 lands the
// implementation: it checks only what the ledger contract fixes at compile
// time. O2 replaces it with the real test — header and row agree on the
// column count over the shipped tables, numbers read back exactly, a
// missing table drops its group from both.

#include <iostream>

#include "core_common/ledger_state.h"
#include "core_report/ledger_csv.h"  // IWYU pragma: keep — compiles the contract

int main() {
  int failures = 0;
  const core::LedgerState ledger;
  if (ledger.closed.year != 0 || ledger.current.year != 0) {
    std::cout << "FAIL: a fresh ledger must have no closed book\n";
    ++failures;
  }
  if (ledger.current.work_days_by_kind.size() != core::kWorkKindCount) {
    std::cout << "FAIL: the labor block does not cover every WorkKind\n";
    ++failures;
  }
  if (failures == 0) {
    std::cout << "unit_core_report: contract constants hold (implementation pending, task O2)\n";
  }
  return failures;
}
