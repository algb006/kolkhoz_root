// Unit test of core_boundary. PLACEHOLDER until the implementing task of
// the boundary lands: it checks only what the contract fixes at compile
// time. That task replaces it with the real test — an order issued, applied
// before phase 1 and consumed; the promised id matching the row's; a cancel
// while pending and a refused cancel once active; the outbox drained and
// cleared; AdvanceUntil stopping on each outcome; the journal round trip;
// and the determinism check with orders in play: 1 worker == N workers.

#include <iostream>

#include "core_boundary/session.h"

int main() {
  int failures = 0;
  if (core::kJournalMagic.size() != 8) {
    std::cout << "FAIL: the journal magic is not eight bytes\n";
    ++failures;
  }
  // The order book and the outbox are members of the world state, so a
  // copy of the world carries both — the double buffer's precondition.
  const core::WorldState world;
  if (!world.orders.rows.empty() || !world.step_events.empty()) {
    std::cout << "FAIL: a fresh world has orders or events\n";
    ++failures;
  }
  if (failures == 0) {
    std::cout << "unit_core_boundary: contract constants hold (implementation pending)\n";
  }
  return failures;
}
