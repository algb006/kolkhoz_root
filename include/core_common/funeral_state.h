/// @file
/// @brief A funeral (#1084; social.md «Как идут похороны»; econ's
/// funeral-third-day-2026-10-19, «Ревью целиком» R1; boss, the #1056 thread
/// [58], [59]): a death on day D is buried on D + 1, the wake the night of D.
/// @threading PARALLEL_READONLY for the reads; written by core_residents'
/// funeral step (funeral_rules.h), single-threaded.
///
/// THE RULE, in short (the numbers are STUB, social.md):
///  - THE PARTY: the dead's adult kin within two hops (mother, father, spouse;
///    from them children, siblings, grandparents, grandchildren), the dead's
///    household but small children, and one adult of every other household
///    within kNeighbourMetres of the dead's house in the dead's settlement;
///  - THE CEMETERY: the nearest to the dead's house; the same-day dead of one
///    settlement are one funeral at the cemetery of the least summed distance;
///  - THE DAY: the kin and the household off the whole burial day (the
///    cemetery, then the afternoon wake); the neighbours at the cemetery and
///    back, then at work; a burial on a day off costs no work;
///  - THE CHAIRMAN: invited by the feed (kFuneralInvitation, loud for a named
///    character, a child or a household's last adult, at most one loud burial
///    day in kLoudEveryDays); kAttendFuneral books «went».

#ifndef CORE_COMMON_FUNERAL_STATE_H_
#define CORE_COMMON_FUNERAL_STATE_H_

#include <cstdint>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/ids.h"
#include "core_common/state_table.h"

namespace core {

/// One adult of every household within this of the dead's house goes (STUB, m).
inline constexpr float kNeighbourMetres = 300.0F;

/// At most one LOUD burial day in this many game days (STUB).
inline constexpr std::uint32_t kLoudEveryDays = 14;

/// @brief The chairman and this funeral.
enum class FuneralChairman : std::uint8_t {
  kInvited = 0,           ///< Asked; no answer yet.
  kWent,                  ///< kAttendFuneral accepted: the session carries him there.
  kExcused,               ///< Away in the district, ill, or at another funeral that day.
  kMissed,                ///< A loud one, not attended while in the village (the day's end).
  kFuneralChairmanCount,  ///< NOT A VALUE: the count, for mirrors.
};

/// @brief One funeral, from the death day to the burial day's end. SAVED.
struct FuneralRow {
  /// The dead of this funeral: one, or the same-day dead of one settlement
  /// buried together.
  std::vector<ResidentId> deceased;

  /// The dead's households, parallel to `deceased`.
  std::vector<FamilyId> households;

  /// The dead's settlement (#1089).
  SettlementId settlement;

  /// The cemetery it goes to.
  UnitId cemetery;

  /// The burial day: the death day + 1.
  SimDay burial_day = 0;

  /// The invitation is loud (a named character, a child, a household's last
  /// adult, within the loud cap).
  std::uint8_t loud = 0;

  FuneralChairman chairman = FuneralChairman::kInvited;

  /// The party, by resident: kin and household (off the day) first, then
  /// the neighbours (at the cemetery and back); `kin_count` splits them.
  std::vector<ResidentId> party;
  std::uint32_t kin_count = 0;
};

using FuneralTable = StateTable<FuneralId, FuneralRow>;

}  // namespace core

#endif  // CORE_COMMON_FUNERAL_STATE_H_
