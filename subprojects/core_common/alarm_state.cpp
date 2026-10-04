// The one function of the alarm roster: which id names an alarm's subject.
// The roster itself is data (include/core_common/alarm_state.h); this is the
// switch that turns "the kind names an id" into a number the session can
// sort by, and it lives in a .cpp so that adding a kind touches one
// translation unit instead of every one that includes the header.

#include "core_common/alarm_state.h"

#include <algorithm>

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
    case AlarmKind::kSiteWithoutCrew:
    case AlarmKind::kSiteUnreachable:
    case AlarmKind::kYardWithoutGroom:
    case AlarmKind::kProcessingStopped:
    case AlarmKind::kDemolitionStockWaiting:
      return alarm.unit.value;
    case AlarmKind::kHarvestWillNotFit:
    case AlarmKind::kHarvestWaitingOnField:
    case AlarmKind::kSeedShort:
    case AlarmKind::kSowingWillNotFit:
    case AlarmKind::kHarvestWillNotBeGathered:
    case AlarmKind::kSowingWindowClosing:
      return alarm.field.value;
    case AlarmKind::kHerdStarving:
    case AlarmKind::kHerdWithoutStable:
    case AlarmKind::kHerdAging:
    case AlarmKind::kSlaughterWaitsForRoom:
    case AlarmKind::kTeamOnHay:
    case AlarmKind::kTooFewHorses:
    // The herds' yellow stage: the registry's subject is the herd the
    // forecast underfeeds first (alarms.csv `herd_hay_short_ahead`; 0.37.56).
    case AlarmKind::kHerdHayShortAhead:
    // The cold's red and yellow: the herd in the cold, or the one the pen
    // will not cover (alarms.csv `herd_freezing`, `herd_cold_ahead`; 0.37.59).
    case AlarmKind::kHerdFreezing:
    case AlarmKind::kHerdColdAhead:
      return alarm.herd.value;
    case AlarmKind::kReserveFullNothingToEat:
    case AlarmKind::kPlanPositionShort:
    case AlarmKind::kSeedHasNoRoom:
    case AlarmKind::kMilkAllToDebt:
    case AlarmKind::kGoodsLoanOwed:
    // The elder's advice on grass: the registry's subject is the hay
    // (alarms.csv; 0.37.21 — 0.37.19 answered 0, «the farm», a word the
    // registry does not have).
    case AlarmKind::kMeadowUncutBeforeSnow:
    // The book's «засеем меньше»: the registry's subject is the seed
    // (alarms.csv `seed_area_short`; 0.37.43).
    case AlarmKind::kSeedAreaShort:
      return alarm.resource.value;
    case AlarmKind::kFamilyGoingHungry:
      return alarm.family.value;
    case AlarmKind::kFellingUnreachable:
    case AlarmKind::kPlantingUnreachable:
      return alarm.stand.value;
    case AlarmKind::kLogisticsLate:
      return alarm.logistics_task.value;
    case AlarmKind::kPlanPositionUncovered:
      // The resource, times the three years a chain lays out, plus the year:
      // one value per alarm, so two years of one position sort by the year.
      return (static_cast<std::uint32_t>(alarm.resource.value) * 3U) +
             static_cast<std::uint32_t>(alarm.amount);
    case AlarmKind::kWinterCropUnsowable:
      // The field, times four, plus the year (1..3): one field can carry the
      // mark for more than one year of its chain.
      return (static_cast<std::uint32_t>(alarm.field.value) * 4U) +
             static_cast<std::uint32_t>(alarm.amount);
  }
  return 0;
}

namespace {

/// Whether the kind is the elder's advice and carries no colour (office
/// design §13). No default label, as above: a new kind is a build error here
/// until it says whether it is a lamp.
bool IsElderAdvice(AlarmKind kind) {
  switch (kind) {
    case AlarmKind::kMeadowUncutBeforeSnow:
    case AlarmKind::kSowingWindowClosing:
      return true;
    case AlarmKind::kNone:
    case AlarmKind::kAlarmKindCount:
    case AlarmKind::kStoreFull:
    case AlarmKind::kHarvestWillNotFit:
    case AlarmKind::kHarvestWaitingOnField:
    case AlarmKind::kSeedShort:
    case AlarmKind::kHerdStarving:
    case AlarmKind::kFamilyGoingHungry:
    case AlarmKind::kSiteWithoutMaterials:
    case AlarmKind::kSiteWithoutCrew:
    case AlarmKind::kSiteUnreachable:
    case AlarmKind::kYardWithoutGroom:
    case AlarmKind::kHerdWithoutStable:
    case AlarmKind::kPlanPositionUncovered:
    case AlarmKind::kFellingUnreachable:
    case AlarmKind::kReserveFullNothingToEat:
    case AlarmKind::kSowingWillNotFit:
    case AlarmKind::kHarvestWillNotBeGathered:
    case AlarmKind::kPlanPositionShort:
    case AlarmKind::kProcessingStopped:
    case AlarmKind::kDemolitionStockWaiting:
    case AlarmKind::kSlaughterWaitsForRoom:
    case AlarmKind::kPlantingUnreachable:
    case AlarmKind::kSeedHasNoRoom:
    case AlarmKind::kMilkAllToDebt:
    case AlarmKind::kGoodsLoanOwed:
    case AlarmKind::kWinterCropUnsowable:
    case AlarmKind::kHerdAging:
    case AlarmKind::kTeamOnHay:
    case AlarmKind::kTooFewHorses:
    case AlarmKind::kSeedAreaShort:
    case AlarmKind::kHerdHayShortAhead:
    case AlarmKind::kHerdFreezing:
    case AlarmKind::kHerdColdAhead:
    case AlarmKind::kLogisticsLate:
      return false;
  }
  return false;
}

}  // namespace

std::uint16_t DaysToLossOf(std::int64_t days) {
  constexpr std::int64_t kNoLossInSight = 0xFFFF;
  return static_cast<std::uint16_t>(std::clamp<std::int64_t>(days, 0, kNoLossInSight));
}

void PaintAlarms(std::span<Alarm> alarms, std::uint16_t red_within_days) {
  for (Alarm& alarm : alarms) {
    if (IsElderAdvice(alarm.kind)) {
      alarm.colour = AlarmColour::kAdvice;
    } else {
      alarm.colour =
          alarm.days_to_loss <= red_within_days ? AlarmColour::kRed : AlarmColour::kYellow;
    }
  }
}

}  // namespace core
