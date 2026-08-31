/// @file
/// @brief The world's scalar blocks and the ledger, as save sections.
/// @threading SINGLE_THREADED
/// Internal to core_save; called from the single encode/decode path.
///
/// "Scalar blocks" is everything of WorldState that is not one of the five
/// entity tables: the calendar, the weather, the epoch, the seed, the RNG,
/// the chairman, the plan and the vitals. They are one section because they
/// are one screenful and they always travel together.
///
/// The ledger (core_common/ledger_state.h) is its own section: it is the
/// largest single block of the state and the newest, so a change there
/// should show up as its own length in the file rather than swelling the
/// world's.

#ifndef CORE_SAVE_SAVE_BLOCKS_H_
#define CORE_SAVE_SAVE_BLOCKS_H_

#include "core_common/world_state.h"
#include "save_dictionary.h"

namespace core {

/// @brief Calendar, weather, epoch, seed, RNG, chairman, plan, vitals.
void WriteWorldBlocks(SaveSink& sink, const WorldState& world);

/// @brief The same, into `world`. The calendar's derived caches — day,
/// date, weekday, season — are RE-DERIVED from the tick afterwards rather
/// than trusted, so a tampered or stale date cannot enter the simulation;
/// only day_zero_weekday is authoritative, and it is range-checked.
void ReadWorldBlocks(LoadSource& source, WorldState* world);

void WriteLedger(SaveSink& sink, const LedgerState& ledger);

LedgerState ReadLedger(LoadSource& source);

}  // namespace core

#endif  // CORE_SAVE_SAVE_BLOCKS_H_
