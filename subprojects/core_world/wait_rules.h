/// @file
/// @brief The rules of every wait kind (core_world/watchdog.h, IWaitKindRules;
///        core_common/wait_state.h; routing stage B, B6): one class a kind,
///        and the table the watchdog is built over.
/// @threading SINGLE_THREADED for Emergency and Nudge (phase 6 alone);
///        PollIntervalHours and Inspect are pure reads.
///
/// THEY LIVE HERE, NOT IN THE SUBSYSTEM THAT OWNS THE KIND, and that is said
/// against watchdog.h's word «made by the subsystem that owns the kind»: both
/// kinds read and write only the world (a passenger's work, a herd's row), so
/// a factory on the labour system's interface would be an interface change
/// buying nothing. The day a kind's rules need a subsystem's own numbers, its
/// rules move there and the interface grows then.

#ifndef CORE_WORLD_WAIT_RULES_H_
#define CORE_WORLD_WAIT_RULES_H_

#include <array>
#include <memory>

#include "core_common/wait_state.h"
#include "core_world/watchdog.h"

namespace core {

/// @brief One rules object a wait kind, in WaitKind's order — what
///        CreateWatchdog takes.
std::array<std::unique_ptr<IWaitKindRules>, kWaitKindCount> CreateWaitRules();

/// @brief The amount of a kWatchdogFired event (event_state.h): the game hours
///        stood (bits 0-31), the verdict (32-39), the kind (40-47) and the
///        agents nudged (48-63). One home for the packing; the journal's
///        reader unpacks by the same widths.
std::int64_t PackWatchdogAmount(const WatchdogLine& line);

}  // namespace core

#endif  // CORE_WORLD_WAIT_RULES_H_
