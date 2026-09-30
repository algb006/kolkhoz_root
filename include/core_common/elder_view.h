/// @file
/// @brief «Кто из жителей — староста»: the door host raises `starosta_met`
///        by and the layer puts the face on (boss-core-start-quest-facts-
///        2026-09-30 [6]-[8]; society design §1а).
/// @threading SINGLE_THREADED
/// Plain data, built from the completed world between steps.
#ifndef CORE_COMMON_ELDER_VIEW_H_
#define CORE_COMMON_ELDER_VIEW_H_

#include "core_common/ids.h"

namespace core {

/// @brief The former elder as the world has him now.
struct ElderView {
  /// The resident (NamedCharactersState::elder); invalid when the world has
  /// none, or he is no longer among the living.
  ResidentId resident;

  /// His family's house today — `yard_21` at the start, wherever the family
  /// lives after; invalid when roofless.
  UnitId house;
};

}  // namespace core

#endif  // CORE_COMMON_ELDER_VIEW_H_
