/// @file
/// @brief The couples ready to marry who wait for a free house (life-cycle
/// design §11–12; boss, parcels 240, 253, 257).
/// @threading SINGLE_THREADED
/// Written by the residents' decisions sub-step (phase 3) on the sim thread:
/// a row is added the day a ready couple finds no free house, and removed the
/// day it marries or falls apart. Read nowhere in a parallel phase. SAVED —
/// a waiting couple survives a load.
///
/// THE RULE, in the design's words: one family, one house, and no moving in
/// with the parents; "a wedding takes place only when a free house is ready
/// for the couple". A couple that is ready and has no house WAITS: it looks
/// for nobody else, and the first free house goes to the couple that has
/// waited longest. It falls apart when either of the two dies or leaves.
///
/// WHAT IS NOT HERE YET, named: the chairman's decision (§13 — the
/// application, the approval, a date on a non-working day, the house
/// reserved) is a STUB — the couple first in the queue marries on the day a
/// free house stands. It comes as its own move, with the order to approve a
/// wedding (boss, parcel 257).

#ifndef CORE_COMMON_WEDDING_STATE_H_
#define CORE_COMMON_WEDDING_STATE_H_

#include <cstdint>

#include "core_common/ids.h"
#include "core_common/state_table.h"

namespace core {

/// @brief One couple waiting for a house.
struct WeddingWaitRow {
  ResidentId bride;

  ResidentId groom;

  /// The campaign day the couple first found no free house: the queue's
  /// order, oldest first.
  std::uint32_t since_day = 0;
};

using WeddingWaitTable = StateTable<WeddingWaitId, WeddingWaitRow>;

}  // namespace core

#endif  // CORE_COMMON_WEDDING_STATE_H_
