// One game agent's wait (wait_state.h).

#include "core_common/wait_state.h"

namespace core {

// A term of nought would be a wait already over the moment it began: read as
// one game hour, the shortest the dog can see (it walks hourly).
WaitRecord::WaitRecord(WaitKind kind, Tick since, std::uint32_t term_hours, WaitTarget target)
    : kind(kind),
      since(since),
      term_hours(term_hours == 0 ? 1U : term_hours),
      last_polled(since),
      due(since + this->term_hours),
      target(target) {}

}  // namespace core
