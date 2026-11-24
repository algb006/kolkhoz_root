/// @file
/// @brief PROBE, not for release (#2118, boss 24 Nov: name the superlinear term): this thread's
///        CPU seconds and calls by slot - the step's six phases and its prologue, and inside
///        the decisions the suspects (the accountant's road measure and plan, the trails' day).
///        population_curve walks one village a thread with worker_count 1, so a thread's slots
///        are one village's; SCALE2118 reads and fits them by year.
/// @threading PARALLEL_WRITE
/// Thread-local; a probe only (Linux's CLOCK_THREAD_CPUTIME_ID).

#ifndef CORE_COMMON_TIME_PROBE_H_
#define CORE_COMMON_TIME_PROBE_H_

#include <time.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace core::probe {

enum class Slot : std::uint8_t {
  kPrologue = 0,     ///< the buffer copy and the staged orders
  kTimeAndWeather,   ///< phase 1
  kNeeds,            ///< phase 2
  kDecisions,        ///< phase 3, whole
  kProduction,       ///< phase 4
  kMetrics,          ///< phase 5
  kEvents,           ///< phase 6
  kMeasureRoads,     ///< labor: MeasureRoads (jobs x homes x 2 queries)
  kPlanAssignments,  ///< labor: PlanDayAssignments
  kCollect,          ///< labor: CollectCandidates
  kMakeDay,          ///< trails: the day's weights
  kWithTrails,       ///< trails: the morning's re-dating
  kTread,            ///< trails: the placed walkers' tread
  kDecay,            ///< trails: the morning's decay
  kMeasurePairs,     ///< labor: MeasureRoads' (job, home) loop alone
  kMeasureHead,      ///< labor: MeasureRoads before its pairs loop (homes, locates, yard)
  kAccessNear,       ///< roads: Accesses answered by the near table
  kAccessRing,       ///< roads: Accesses by the ring search (the table did not answer)
  kHeadHomes,        ///< MeasureRoads' head: the homes' hash
  kHeadLocate,       ///< MeasureRoads' head: the homes' Locate x 4 modes
  kHeadYard,         ///< MeasureRoads' head: the horse yard's Locate and every home's walk to it
  kCount,
};

inline constexpr std::size_t kSlots = static_cast<std::size_t>(Slot::kCount);

struct Counters {
  std::array<std::uint64_t, kSlots> ns{};
  std::array<std::uint64_t, kSlots> calls{};
  /// MeasureRoads' (job, home) pairs, summed - the quadratic's own count.
  std::uint64_t measure_pairs = 0;
};

inline Counters& Thread() {
  thread_local Counters counters;
  return counters;
}

inline std::uint64_t ThreadNanos() {
  timespec now{};
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
  return (static_cast<std::uint64_t>(now.tv_sec) * 1000000000ULL) +
         static_cast<std::uint64_t>(now.tv_nsec);
}

class Timer {
 public:
  explicit Timer(Slot slot) : slot_(slot), start_(ThreadNanos()) {}

  ~Timer() {
    const auto at = static_cast<std::size_t>(slot_);
    Thread().ns[at] += ThreadNanos() - start_;
    ++Thread().calls[at];
  }

  Timer(const Timer&) = delete;
  Timer& operator=(const Timer&) = delete;

 private:
  Slot slot_;
  std::uint64_t start_;
};

}  // namespace core::probe

#endif  // CORE_COMMON_TIME_PROBE_H_
