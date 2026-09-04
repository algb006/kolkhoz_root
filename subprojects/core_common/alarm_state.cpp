// The one function of the alarm roster: which id names an alarm's subject.
// The roster itself is data (include/core_common/alarm_state.h); this is the
// switch that turns "the kind names an id" into a number the session can
// sort by, and it lives in a .cpp so that adding a kind touches one
// translation unit instead of every one that includes the header.

#include "core_common/alarm_state.h"

namespace core {

std::uint32_t AlarmSubjectValue(const Alarm& alarm) {
  // No default label on purpose: -Wswitch then makes a new kind a build
  // error here, which is exactly where a new kind must declare its subject.
  switch (alarm.kind) {
    case AlarmKind::kNone:
    // Not a kind, and it has no subject any more than kNone does. Handled
    // beside it so this switch keeps no default and a genuinely new kind
    // stays a build error here — which is exactly where a new kind must
    // declare whose it is.
    case AlarmKind::kAlarmKindCount:
      return 0;
    case AlarmKind::kStoreFull:
    case AlarmKind::kSiteWithoutMaterials:
    case AlarmKind::kNoRoad:
    case AlarmKind::kYardWithoutGroom:
      return alarm.unit.value;
    case AlarmKind::kHarvestWillNotFit:
    case AlarmKind::kHarvestWaitingOnField:
    case AlarmKind::kSeedShort:
      return alarm.field.value;
    case AlarmKind::kHerdStarving:
      return alarm.herd.value;
    case AlarmKind::kFamilyGoingHungry:
      return alarm.family.value;
  }
  return 0;
}

}  // namespace core
