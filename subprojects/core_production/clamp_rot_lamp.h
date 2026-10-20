/// @file
/// @brief The lamp «В буртах сгнило N т» (kClampRot; #1005, #1068; econ's
/// clamp-rot-lamp-2026-10-17 §4c).
/// @threading SINGLE_THREADED
/// Collected with the other production alarms off the completed state.
///
/// THE DECISION IT NAMES: a clamp is the first year's place for the potato
/// (production design §1a), and a vegetable store keeps three times longer
/// for 140 man-days once. From the second year the difference is about ten
/// tonnes a year (host, 0.37.295), and until this lamp nothing said so.

#ifndef CORE_PRODUCTION_CLAMP_ROT_LAMP_H_
#define CORE_PRODUCTION_CLAMP_ROT_LAMP_H_

#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief Appends the lamp «В буртах сгнило N т» when it is lit today: from
///        the first day of May to the day before the potato's harvest window
///        opens, the potato and vegetables rotted in clamps over the last
///        year at farming.csv `clamp_rot_lamp_t` or more, and no vegetable
///        store standing, staked or with works open. Unpainted: the session
///        colours it by `days_to_loss` (PaintAlarms).
/// @note A world with no potato crop, no vegetable store type or no potato
///       resource in its tables never lights it.
void CollectClampRotAlarms(const ProductionConfig& config,
                           const WorldState& world,
                           std::vector<Alarm>& alarms);

}  // namespace core

#endif  // CORE_PRODUCTION_CLAMP_ROT_LAMP_H_
