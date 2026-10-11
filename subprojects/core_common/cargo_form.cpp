// The cargo form's two rules (core_common/cargo_form.h; transport §11,
// «Форма груза»).

#include "core_common/cargo_form.h"

namespace core {

bool CargoFormSeatsRiders(CargoForm form) {
  switch (form) {
    case CargoForm::kNone:  // an empty leg: «на порожнем плече — всегда»
    case CargoForm::kBulk:
    case CargoForm::kPacked:
    case CargoForm::kRick:  // «да, на возу — сверху, как в жизни»
      return true;
    case CargoForm::kManure:
    case CargoForm::kLong:
    case CargoForm::kLive:
    case CargoForm::kPeople:  // seats by its own six places, not by this rule
    case CargoForm::kCargoFormCount:
      break;
  }
  return false;
}

bool CargoFormsMix(CargoForm first, CargoForm added, bool same_resource, bool food_with_fuel) {
  if (first != added || first == CargoForm::kNone || first == CargoForm::kPeople ||
      first == CargoForm::kCargoFormCount) {
    return false;
  }
  switch (first) {
    case CargoForm::kBulk:
    case CargoForm::kRick:
      return same_resource;  // «только тот же ресурс» / «только сено или только солома»
    case CargoForm::kPacked:
      return !food_with_fuel;  // «любая тара; еда — не с горючим»
    case CargoForm::kManure:
    case CargoForm::kLong:
    case CargoForm::kLive:
      return true;  // «только тот же» form
    default:
      break;
  }
  return false;
}

}  // namespace core
