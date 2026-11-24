/// @file
/// @brief PROBE, not for release (#2072's CPU): wall time and calls by slot, summed over the
///        process's threads; population_curve prints TIME2072 at the run's end.
/// @threading PARALLEL_WRITE
/// Relaxed atomics; a probe only.

#ifndef CORE_COMMON_TIME_PROBE_H_
#define CORE_COMMON_TIME_PROBE_H_

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace core::probe {

enum class Slot : std::uint8_t {
  kStraight = 0,
  kRoute,
  kMakeDay,
  kWithTrails,
  kTread,
  kDecay,
  kWalkRoute,
  kChoose,
  kTreadRoutes,  ///< the tread's WalkRouteKm calls (memo hits and their step copies)
  kTreadRule9,   ///< the tread's standing-crop check and pass gathering
  kTreadSort,    ///< the tread's sort and merge of the passes
  kCount,
};

inline constexpr std::size_t kSlots = static_cast<std::size_t>(Slot::kCount);
inline std::array<std::atomic<std::uint64_t>, kSlots> g_ns{};
inline std::array<std::atomic<std::uint64_t>, kSlots> g_calls{};

class Timer {
 public:
  explicit Timer(Slot slot) : slot_(slot), start_(std::chrono::steady_clock::now()) {}

  ~Timer() {
    const auto spent = std::chrono::steady_clock::now() - start_;
    const auto at = static_cast<std::size_t>(slot_);
    g_ns[at].fetch_add(static_cast<std::uint64_t>(
                           std::chrono::duration_cast<std::chrono::nanoseconds>(spent).count()),
                       std::memory_order_relaxed);
    g_calls[at].fetch_add(1, std::memory_order_relaxed);
  }

  Timer(const Timer&) = delete;
  Timer& operator=(const Timer&) = delete;

 private:
  Slot slot_;
  std::chrono::steady_clock::time_point start_;
};

}  // namespace core::probe

#endif  // CORE_COMMON_TIME_PROBE_H_
