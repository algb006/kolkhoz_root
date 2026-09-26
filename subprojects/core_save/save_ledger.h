/// @file
/// @brief The ledger (core_common/ledger_state.h) as its own save section.
/// @threading SINGLE_THREADED
/// Internal to core_save; called from the single encode/decode path.
///
/// The ledger is its own section: it is the largest single block of the
/// state and the newest, so a change there should show up as its own length
/// in the file rather than swelling the world's. Its codec left
/// save_blocks.cpp at 0.36.32, when the why-not-placed counters took that
/// file past the thousand lines of the core's rules (§5).

#ifndef CORE_SAVE_SAVE_LEDGER_H_
#define CORE_SAVE_SAVE_LEDGER_H_

#include "core_common/ledger_state.h"
#include "save_dictionary.h"

namespace core {

/// @brief The current and the closed year's books, then the chronicle.
void WriteLedger(SaveSink& sink, const LedgerState& ledger);

/// @brief The same, back. Fails `source` on a negative column that cannot
/// be negative and on an absurd chronicle length.
LedgerState ReadLedger(LoadSource& source);

}  // namespace core

#endif  // CORE_SAVE_SAVE_LEDGER_H_
