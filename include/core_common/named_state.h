/// @file
/// @brief The named characters the world keeps a hold of (CLAUDE.md §8,
///        «Именной персонаж»): which resident each is. SAVED.
/// @threading SINGLE_THREADED
/// Written at the world's start (genesis) and read between steps; no phase
/// writes it.
#ifndef CORE_COMMON_NAMED_STATE_H_
#define CORE_COMMON_NAMED_STATE_H_

#include "core_common/ids.h"

namespace core {

/// @brief The named characters who are residents, by role.
struct NamedCharactersState {
  /// THE FORMER ELDER (society design §1а; boss-core-start-quest-facts-
  /// 2026-09-30 [6]-[8]): the resident who is Ryabinin in every game — a
  /// man of solid age of the family the start settles in `yard_21`. The
  /// door follows the MAN, not the yard: his family may move. Invalid when
  /// the world has none (a table set with no yard_21, or before genesis).
  ResidentId elder;
};

}  // namespace core

#endif  // CORE_COMMON_NAMED_STATE_H_
