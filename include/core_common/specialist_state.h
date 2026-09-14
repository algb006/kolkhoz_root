/// @file
/// @brief The specialists the district sends in Epoch I — the primary teacher
/// and the librarian — while they are on the road (education design, "Эпоха I
/// числами"; social units design, «Библиотекарь»; boss, parcel 237).
/// @threading SINGLE_THREADED
/// Written by the residents' decisions sub-step (phase 3) on the sim thread:
/// a row is added on the first day of a month the district decides to send
/// someone, and removed on the day he arrives. Read nowhere in a parallel
/// phase. SAVED — a specialist on the road survives a load.
///
/// WHAT THE DISTRICT DOES, in the design's words: on the first day of a month,
/// when the school stands, there are children of the junior school band, the
/// teachers are fewer than the norm (one per `teacher_pupils_per_teacher`
/// children, rounded up) and a free house stands for him — the district sends
/// one; he comes after `limit_delivery_days`, like the district's cart, lives
/// in that house as a household of one, and is appointed on arrival. The
/// librarian the same way: the reading hut stands and has none. Free in Epoch
/// I; from Epoch II for points (STUB: not sent at all after Epoch I). No free
/// house — nobody is sent, the month passes, and the village is told so
/// (EventKind::kSpecialistNoHousing).
///
/// A FREE HOUSE IS AN EMPTY ONE THAT STANDS, and nothing else: the wedding
/// stub that raises a house from nothing (core_residents/housing.cpp) is not
/// asked, or the condition "no housing" could never be true.

#ifndef CORE_COMMON_SPECIALIST_STATE_H_
#define CORE_COMMON_SPECIALIST_STATE_H_

#include <cstdint>

#include "core_common/ids.h"
#include "core_common/state_table.h"

namespace core {

/// @brief One specialist on the road.
struct SpecialistArrivalRow {
  /// The post he comes for (tables/professions.csv): primary_teacher or
  /// librarian today.
  ProfessionId profession;

  /// The unit he is appointed to on arrival: the school or the reading hut.
  /// If it is gone by then he still comes and is appointed to nothing (the
  /// district does not turn a cart round).
  UnitId unit;

  /// The campaign day he is due: the day the district decided plus
  /// `limit_delivery_days`. From that day on he arrives on the first day a
  /// free house stands; until then the row waits.
  std::uint32_t arrive_day = 0;
};

using SpecialistArrivalTable = StateTable<SpecialistArrivalId, SpecialistArrivalRow>;

}  // namespace core

#endif  // CORE_COMMON_SPECIALIST_STATE_H_
